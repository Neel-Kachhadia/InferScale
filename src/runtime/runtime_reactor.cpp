#include "inferscale/runtime/runtime_reactor.hpp"
#include "inferscale/scheduler/fifo.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace inferscale {

RuntimeReactor::RuntimeReactor(const Clock& clock, std::uint32_t max_sequences,
                               std::size_t event_capacity, std::uint32_t context_limit,
                               std::uint32_t max_output_piece_bytes)
    : clock_(clock), events_(event_capacity), allocator_(max_sequences),
      context_limit_(context_limit), max_output_piece_bytes_(max_output_piece_bytes) {
  if (max_output_piece_bytes_ == 0) throw std::invalid_argument("output piece bound must be positive");
}

bool RuntimeReactor::post(RuntimeEvent event) {
  if (std::holds_alternative<NewRequestEvent>(event)) return events_.try_push(std::move(event));
  return events_.push_wait(std::move(event));
}

bool RuntimeReactor::post_completion(RuntimeEvent event) { return events_.push_wait(std::move(event)); }

void RuntimeReactor::assert_owner() const {
  const auto current = std::this_thread::get_id();
  if (owner_ == std::thread::id{}) owner_ = current;
  if (owner_ != current) throw std::logic_error("RuntimeReactor mutation from non-owner thread");
}

bool RuntimeReactor::run_one() {
  assert_owner();
  auto event = events_.try_pop();
  if (!event) return false;
  handle(std::move(*event));
  return true;
}

bool RuntimeReactor::wait_one_for(std::chrono::milliseconds timeout) {
  assert_owner();
  auto event = events_.wait_pop_for(timeout);
  if (!event) return false;
  handle(std::move(*event));
  return true;
}

std::size_t RuntimeReactor::run_ready() {
  assert_owner();
  std::size_t count = 0;
  while (run_one()) ++count;
  return count;
}

void RuntimeReactor::seal_events() {
  assert_owner();
  events_.close();
  run_ready();
}

void RuntimeReactor::handle(RuntimeEvent event) {
  std::visit([this](auto&& value) {
    using Event = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<Event, NewRequestEvent>) {
      handle_new(std::move(value));
    } else if constexpr (std::is_same_v<Event, CancelEvent>) {
      handle_cancel(value.key, RequestState::CancelPendingRetire);
    } else if constexpr (std::is_same_v<Event, DeadlineEvent>) {
      handle_cancel(value.key, RequestState::FailedPendingRetire);
    } else if constexpr (std::is_same_v<Event, StreamWritableEvent>) {
      if (auto* request = find(value.key)) request->backpressured = false;
      else ++stale_events_;
    } else if constexpr (std::is_same_v<Event, BatchCompletedEvent>) {
      handle_completion(std::move(value));
    } else if constexpr (std::is_same_v<Event, SequenceClearedEvent>) {
      process_clear(value);
    } else if constexpr (std::is_same_v<Event, ShutdownEvent>) {
      begin_shutdown();
    }
  }, std::move(event));
}

void RuntimeReactor::handle_new(NewRequestEvent event) {
  auto& spec = event.spec;
  const auto reject = [&event] {
    if (event.admission) event.admission->store(AdmissionResult::Rejected, std::memory_order_release);
  };
  const auto generation = last_generation_.find(spec.id);
  const auto next = generation == last_generation_.end()
                        ? (event.generation_hint == 0 ? 1U : event.generation_hint)
                        : (event.generation_hint == 0 || generation->second ==
                               std::numeric_limits<std::uint32_t>::max()
                               ? 0U : generation->second + 1U);
  if (shutting_down_ || fatal_error_ || spec.id == 0 || spec.prompt_tokens == 0 || spec.max_output_tokens == 0 ||
      spec.stream_capacity_bytes < max_output_piece_bytes_ || next == 0 ||
      (spec.prompt_token_ids && spec.prompt_token_ids->size() != spec.prompt_tokens) ||
      static_cast<std::uint64_t>(spec.prompt_tokens) + spec.max_output_tokens > context_limit_ ||
      (event.generation_hint != 0 && event.generation_hint != next) ||
      (event.output_stream && event.output_stream->capacity() != spec.stream_capacity_bytes)) {
    reject();
    return;
  }
  const RequestKey key{spec.id, static_cast<std::uint32_t>(next)};
  const auto sequence = allocator_.allocate(key);
  if (!sequence) {
    reject();
    return;
  }
  RequestRecord request;
  request.spec = std::move(spec);
  request.key = key;
  request.sequence = *sequence;
  request.stream = event.output_stream ? std::move(event.output_stream)
                                       : std::make_shared<StreamBuffer>(request.spec.stream_capacity_bytes);
  if (!transition(request.state, RequestState::PrefillReady)) {
    reject();
    return;
  }
  requests_.emplace(key, std::move(request));
  if (event.generation_hint != 0) last_generation_[key.id] = key.generation;
  fifo_.push_back(key);
  if (event.admission) event.admission->store(AdmissionResult::Accepted, std::memory_order_release);
}

void RuntimeReactor::handle_cancel(RequestKey key, RequestState terminal_state) {
  auto* request = find(key);
  if (!request) {
    ++stale_events_;
    return;
  }
  if (terminal_intent(request->state) || request->state == RequestState::Terminal) return;
  note_terminal(*request, terminal_state);
}

void RuntimeReactor::note_terminal(RequestRecord& request, RequestState state) {
  if (!transition(request.state, state)) return;
  request.reserved_output_bytes = 0;
  const auto end = state == RequestState::CompletePendingRetire ? StreamEnd::Complete
                    : state == RequestState::CancelPendingRetire ? StreamEnd::Cancelled
                                                                  : StreamEnd::Failed;
  request.stream->close(end, {request.spec.prompt_tokens, request.generated_tokens});
  maybe_queue_clear(request);
}

bool RuntimeReactor::is_ready(const RequestRecord& request) const noexcept {
  return request.state == RequestState::PrefillReady || request.state == RequestState::DecodeReady;
}

WorkPlan RuntimeReactor::make_fifo_plan(std::uint32_t max_tokens, std::uint32_t max_sequences,
                                        std::uint32_t max_prefill_chunk) {
  assert_owner();
  std::vector<ReadyRequest> ready;
  ready.reserve(fifo_.size());
  for (const auto key : fifo_) {
    auto* request = find(key);
    if (request == nullptr || !is_ready(*request)) continue;
    if (request->spec.deadline.has_value() && *request->spec.deadline <= clock_.now()) {
      note_terminal(*request, RequestState::FailedPendingRetire);
      continue;
    }
    if (request->state == RequestState::PrefillReady) {
      ready.push_back({key, WorkKind::Prefill, request->prefill_cursor,
                       request->spec.prompt_tokens - request->prefill_cursor});
    } else {
      const auto available = request->spec.stream_capacity_bytes - request->stream->bytes() -
                             request->reserved_output_bytes;
      if (request->backpressured || available < max_output_piece_bytes_) {
        request->backpressured = true;
        continue;
      }
      ready.push_back({key, WorkKind::Decode, request->prefill_cursor + request->generated_tokens, 1});
    }
  }
  return fifo_plan(ready, max_tokens, max_sequences, max_prefill_chunk);
}

std::optional<BatchTicket> RuntimeReactor::issue(const WorkPlan& plan) {
  assert_owner();
  if (shutting_down_ || !valid_work_plan(plan) || plan.items.empty() ||
      next_batch_id_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
  std::vector<RequestRecord*> records;
  records.reserve(plan.items.size());
  std::vector<TicketEntry> entries;
  entries.reserve(plan.items.size());
  for (const auto& item : plan.items) {
    auto* request = find(item.key);
    if (request == nullptr || !is_ready(*request) || terminal_intent(request->state)) return std::nullopt;
    if (request->spec.deadline && *request->spec.deadline <= clock_.now()) {
      note_terminal(*request, RequestState::FailedPendingRetire);
      return std::nullopt;
    }
    const auto expected_kind = request->state == RequestState::PrefillReady ? WorkKind::Prefill : WorkKind::Decode;
    const auto begin = expected_kind == WorkKind::Prefill ? request->prefill_cursor
                        : request->prefill_cursor + request->generated_tokens;
    const auto remaining = expected_kind == WorkKind::Prefill
                               ? request->spec.prompt_tokens - request->prefill_cursor
                               : request->spec.max_output_tokens - request->generated_tokens;
    if (item.kind != expected_kind || item.token_begin != begin || item.token_count > remaining ||
        (item.kind == WorkKind::Decode && item.token_count != 1)) return std::nullopt;
    if (item.kind == WorkKind::Decode &&
        request->spec.stream_capacity_bytes - request->stream->bytes() - request->reserved_output_bytes <
            max_output_piece_bytes_) return std::nullopt;
    const auto sequence = allocator_.find(item.key);
    if (!sequence) return std::nullopt;
    records.push_back(request);
    entries.push_back({item, *sequence, request->spec.prompt_token_ids,
                       request->spec.max_output_tokens});
  }
  for (std::size_t i = 0; i < records.size(); ++i) {
    auto* request = records[i];
    if (!allocator_.pin(request->key)) throw std::logic_error("validated sequence could not be pinned");
    ++request->inflight_tickets;
    if (plan.items[i].kind == WorkKind::Decode) request->reserved_output_bytes += max_output_piece_bytes_;
    const auto next = plan.items[i].kind == WorkKind::Prefill ? RequestState::PrefillInFlight
                                                              : RequestState::DecodeInFlight;
    if (!transition(request->state, next)) throw std::logic_error("invalid state at ticket issue");
  }
  const auto batch_id = next_batch_id_++;
  issued_tickets_.emplace(batch_id, entries);
  return BatchTicket(batch_id, std::move(entries));
}

void RuntimeReactor::handle_completion(BatchCompletedEvent event) {
  auto& ticket = event.ticket;
  const auto issued = issued_tickets_.find(ticket.id());
  const bool matches_issued = issued != issued_tickets_.end() && issued->second == ticket.entries();
  if (!ticket.retire() || !matches_issued) {
    ++stale_events_;
    return;
  }
  issued_tickets_.erase(issued);
  if ((event.result.ok && event.result.outputs.size() != ticket.entries().size()) ||
      (!event.result.ok && (event.result.failure == FailureClass::EngineRestartRequired ||
                            event.result.failure == FailureClass::ProcessFatal))) {
    fatal_error_ = true;
  }
  const auto now = clock_.now();
  for (std::size_t index = 0; index < ticket.entries().size(); ++index) {
    const auto& entry = ticket.entries()[index];
    if (!allocator_.retire(entry.work.key)) {
      if (find(entry.work.key) == nullptr) ++stale_events_;
      else fatal_error_ = true;
      continue;
    }
    auto* request = find(entry.work.key);
    if (request == nullptr) {
      ++stale_events_;
      continue;
    }
    if (request->inflight_tickets == 0) {
      fatal_error_ = true;
      note_terminal(*request, RequestState::FailedPendingRetire);
      maybe_queue_clear(*request);
      continue;
    }
    --request->inflight_tickets;
    if (entry.work.kind == WorkKind::Decode) {
      request->reserved_output_bytes = request->reserved_output_bytes >= max_output_piece_bytes_
                                          ? request->reserved_output_bytes - max_output_piece_bytes_ : 0;
    }
    if (!terminal_intent(request->state) && request->state != RequestState::Terminal &&
        request->spec.deadline && *request->spec.deadline <= now) {
      note_terminal(*request, RequestState::FailedPendingRetire);
    }
    if (fatal_error_) {
      if (!terminal_intent(request->state) && request->state != RequestState::Terminal) {
        note_terminal(*request, RequestState::FailedPendingRetire);
      }
    } else if (!terminal_intent(request->state) && request->state != RequestState::Terminal) {
      if (!event.result.ok) {
        if (event.result.failure == FailureClass::EngineRestartRequired ||
            event.result.failure == FailureClass::ProcessFatal) fatal_error_ = true;
        note_terminal(*request, RequestState::FailedPendingRetire);
      } else if (entry.work.kind == WorkKind::Prefill) {
        request->prefill_cursor += entry.work.token_count;
        request->committed_tokens += entry.work.token_count;
        committed_tokens_ += entry.work.token_count;
        const auto next = request->prefill_cursor < request->spec.prompt_tokens
                              ? RequestState::PrefillReady : RequestState::DecodeReady;
        if (!transition(request->state, next)) fatal_error_ = true;
      } else {
        const auto& output = event.result.outputs[index];
        if (!output) {
          fatal_error_ = true;
          note_terminal(*request, RequestState::FailedPendingRetire);
        } else if (output->piece.size() > max_output_piece_bytes_) {
          fatal_error_ = true;
          note_terminal(*request, RequestState::FailedPendingRetire);
        } else if (!output->piece.empty() && !request->stream->try_push(output->piece)) {
          fatal_error_ = true;
          note_terminal(*request, RequestState::FailedPendingRetire);
        } else if (output->end_of_sequence) {
          note_terminal(*request, RequestState::CompletePendingRetire);
        } else {
          ++request->generated_tokens;
          ++request->committed_tokens;
          ++committed_tokens_;
          if (request->generated_tokens >= request->spec.max_output_tokens) {
            note_terminal(*request, RequestState::CompletePendingRetire);
          } else if (!transition(request->state, RequestState::DecodeReady)) {
            fatal_error_ = true;
          }
        }
      }
    }
    maybe_queue_clear(*request);
  }
}

void RuntimeReactor::maybe_queue_clear(RequestRecord& request) {
  if (!terminal_intent(request.state) || request.inflight_tickets != 0 || request.clear_queued) return;
  if (!allocator_.begin_clear(request.key)) {
    fatal_error_ = true;
    return;
  }
  clear_requests_.push_back({request.key, request.sequence});
  request.clear_queued = true;
}

void RuntimeReactor::process_clear(SequenceClearedEvent event) {
  auto* request = find(event.key);
  if (request == nullptr || !request->clear_queued) {
    ++stale_events_;
    return;
  }
  if (!allocator_.clear_result(event.key, event.succeeded)) {
    fatal_error_ = true;
    return;
  }
  if (!event.succeeded) {
    fatal_error_ = true;
    return;
  }
  if (committed_tokens_ < request->committed_tokens) {
    fatal_error_ = true;
    return;
  }
  committed_tokens_ -= request->committed_tokens;
  if (!transition(request->state, RequestState::Terminal)) {
    fatal_error_ = true;
    return;
  }
  std::erase(fifo_, event.key);
  requests_.erase(event.key);
}

std::size_t RuntimeReactor::expire_deadlines() {
  assert_owner();
  std::size_t expired = 0;
  for (auto& [key, request] : requests_) {
    (void)key;
    if (!terminal_intent(request.state) && request.state != RequestState::Terminal &&
        request.spec.deadline && *request.spec.deadline <= clock_.now()) {
      note_terminal(request, RequestState::FailedPendingRetire);
      ++expired;
    }
  }
  return expired;
}

std::uint64_t RuntimeReactor::inflight_ticket_count() const {
  assert_owner();
  std::uint64_t total = 0;
  for (const auto& [key, request] : requests_) {
    (void)key;
    total += request.inflight_tickets;
  }
  return total;
}

bool RuntimeReactor::cancel(RequestKey key) {
  assert_owner();
  const auto before = stale_events_;
  handle_cancel(key, RequestState::CancelPendingRetire);
  return stale_events_ == before;
}

void RuntimeReactor::begin_shutdown() {
  assert_owner();
  if (shutting_down_) return;
  shutting_down_ = true;
  for (auto& [key, request] : requests_) {
    (void)key;
    if (!terminal_intent(request.state) && request.state != RequestState::Terminal) {
      note_terminal(request, RequestState::CancelPendingRetire);
    }
  }
}

void RuntimeReactor::fail_all() {
  assert_owner();
  shutting_down_ = true;
  for (auto& [key, request] : requests_) {
    (void)key;
    if (!terminal_intent(request.state) && request.state != RequestState::Terminal) {
      note_terminal(request, RequestState::FailedPendingRetire);
    }
  }
}

std::optional<RequestKey> RuntimeReactor::latest_key(std::uint64_t id) const {
  assert_owner();
  for (const auto& [key, request] : requests_) {
    (void)request;
    if (key.id == id) return key;
  }
  const auto generation = last_generation_.find(id);
  if (generation == last_generation_.end()) return std::nullopt;
  return RequestKey{id, generation->second};
}

std::optional<RequestSnapshot> RuntimeReactor::snapshot(RequestKey key) const {
  assert_owner();
  const auto* request = find(key);
  if (request == nullptr) return std::nullopt;
  return RequestSnapshot{request->key, request->state, request->prefill_cursor,
                         request->generated_tokens, request->inflight_tickets,
                         request->committed_tokens, request->backpressured};
}

std::shared_ptr<StreamBuffer> RuntimeReactor::stream_for(RequestKey key) const {
  assert_owner();
  const auto* request = find(key);
  return request == nullptr ? nullptr : request->stream;
}

std::vector<ClearRequest> RuntimeReactor::take_clear_requests() {
  assert_owner();
  auto result = std::move(clear_requests_);
  clear_requests_.clear();
  return result;
}

bool RuntimeReactor::shutdown_complete() const {
  assert_owner();
  return shutting_down_ && requests_.empty() && allocator_.live_sequences() == 0 &&
         issued_tickets_.empty();
}

RuntimeReactor::RequestRecord* RuntimeReactor::find(RequestKey key) {
  const auto it = requests_.find(key);
  return it == requests_.end() ? nullptr : &it->second;
}

const RuntimeReactor::RequestRecord* RuntimeReactor::find(RequestKey key) const {
  const auto it = requests_.find(key);
  return it == requests_.end() ? nullptr : &it->second;
}

}  // namespace inferscale
