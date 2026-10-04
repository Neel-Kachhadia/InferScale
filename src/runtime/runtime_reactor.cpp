#include "inferscale/runtime/runtime_reactor.hpp"
#include "inferscale/runtime/nvtx.hpp"
#include "inferscale/scheduler/fifo.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace inferscale {
namespace {

void saturating_increment(std::uint64_t& value) noexcept {
  if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
}

void saturating_add(std::uint64_t& value, std::uint64_t amount) noexcept {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  value = amount > maximum - value ? maximum : value + amount;
}

std::uint64_t nonnegative_nanoseconds(std::chrono::nanoseconds value) noexcept {
  return value.count() <= 0 ? 0U : static_cast<std::uint64_t>(value.count());
}

std::size_t ready_wait_bucket(std::uint64_t milliseconds) noexcept {
  std::size_t bucket = 0;
  while (milliseconds != 0 && bucket < 31) {
    milliseconds >>= 1U;
    ++bucket;
  }
  return bucket;
}

}  // namespace

RuntimeReactor::RuntimeReactor(const Clock& clock, std::uint32_t max_sequences,
                               std::size_t event_capacity, std::uint32_t context_limit,
                               std::uint32_t max_output_piece_bytes,
                               RuntimeConfig config)
    : clock_(clock), events_(event_capacity), allocator_(max_sequences),
      context_limit_(context_limit), max_sequences_(max_sequences),
      max_output_piece_bytes_(max_output_piece_bytes),
      config_(std::move(config)),
      kv_budget_(make_kv_config(config_, max_sequences, context_limit_)),
      prefill_epoch_budget_(config_.adaptive_prefill.enabled
                                ? config_.adaptive_prefill.initial_tokens
                                : config_.max_prefill_tokens_per_epoch) {
  const auto& adaptive = config_.adaptive_prefill;
  if (max_output_piece_bytes_ == 0) throw std::invalid_argument("output piece bound must be positive");
  if (config_.max_prefill_tokens_per_epoch == 0 || adaptive.trace_capacity > 128 ||
      adaptive.minimum_tokens == 0 || adaptive.minimum_tokens > adaptive.maximum_tokens ||
      adaptive.initial_tokens < adaptive.minimum_tokens ||
      adaptive.initial_tokens > adaptive.maximum_tokens || adaptive.step_tokens == 0 ||
      adaptive.windows_before_change == 0 ||
      adaptive.high_executor_idle_basis_points > 10000 ||
      adaptive.low_interactive_wait_ms >= adaptive.high_interactive_wait_ms ||
      adaptive.low_decode_backlog > adaptive.high_decode_backlog ||
      (adaptive.enabled && adaptive.trace_capacity == 0)) {
    throw std::invalid_argument("invalid runtime scheduling configuration");
  }
  validate_sawds_config(config_.sawds);
}

KvBudgetConfig RuntimeReactor::make_kv_config(const RuntimeConfig& config,
                                               std::uint32_t max_sequences,
                                               std::uint32_t context_limit) {
  const auto resolved_context = static_cast<std::uint64_t>(max_sequences) * context_limit;
  if (resolved_context == 0) throw std::invalid_argument("runtime requires a nonempty KV context");
  const auto hard = config.hard_usable_kv_tokens == 0
      ? KvBudgetManager::conservative_capacity(resolved_context)
      : config.hard_usable_kv_tokens;
  if (hard == 0 || hard > resolved_context) {
    throw std::invalid_argument("hard KV capacity must fit the resolved context");
  }
  return {hard, config.soft_pressure_basis_points,
          config.high_pressure_basis_points, config.kv_safety_margin_tokens};
}

std::uint32_t RuntimeReactor::service_class_index(RequestClass service_class) const {
  switch (service_class) {
    case RequestClass::Interactive: return 0;
    case RequestClass::Batch: return 1;
  }
  throw std::invalid_argument("invalid request service class");
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
      if (auto* request = find(value.key)) {
        if (request->backpressured) request->ready_since = clock_.now();
        request->backpressured = false;
      }
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
  INFERSCALE_NVTX_RANGE("RuntimeReactor/Admission");
  auto& spec = event.spec;
  const auto reject = [this, &event](std::uint64_t ClassRuntimeMetrics::*reason) {
    if (event.spec.service_class == RequestClass::Interactive ||
        event.spec.service_class == RequestClass::Batch) {
      auto& metrics = class_metrics_[service_class_index(event.spec.service_class)];
      saturating_increment(metrics.rejected);
      if (reason != nullptr) saturating_increment(metrics.*reason);
    }
    if (event.admission) event.admission->store(AdmissionResult::Rejected, std::memory_order_release);
  };
  const auto generation = last_generation_.find(spec.id);
  const auto next = generation == last_generation_.end()
                        ? (event.generation_hint == 0 ? 1U : event.generation_hint)
                        : (event.generation_hint == 0 || generation->second ==
                               std::numeric_limits<std::uint32_t>::max()
                               ? 0U : generation->second + 1U);
  const bool valid_class = spec.service_class == RequestClass::Interactive ||
                           spec.service_class == RequestClass::Batch;
  if (shutting_down_ || fatal_error_) {
    reject(&ClassRuntimeMetrics::rejected_runtime_state);
    return;
  }
  if (spec.id == 0 || spec.prompt_tokens == 0 || spec.max_output_tokens == 0 ||
      spec.stream_capacity_bytes < max_output_piece_bytes_ || next == 0 || !valid_class ||
      next_arrival_order_ == std::numeric_limits<std::uint64_t>::max() ||
      (spec.prompt_token_ids && spec.prompt_token_ids->size() != spec.prompt_tokens) ||
      static_cast<std::uint64_t>(spec.prompt_tokens) + spec.max_output_tokens > context_limit_ ||
      (spec.deadline && *spec.deadline <= clock_.now()) ||
      (event.generation_hint != 0 && event.generation_hint != next) ||
      (event.output_stream && event.output_stream->capacity() != spec.stream_capacity_bytes)) {
    reject(&ClassRuntimeMetrics::rejected_invalid);
    return;
  }
  const auto reservation = kv_budget_.reservation_for(spec.prompt_tokens,
      std::min(spec.max_output_tokens, config_.admission_generation_reserve_tokens));
  if (kv_budget_.pressure_state() >= KvPressureState::SoftPressure ||
      !kv_budget_.can_reserve_below_soft_watermark(reservation)) {
    reclaim_excess_soft_reservations();
  }
  if (!kv_budget_.can_reserve_below_soft_watermark(reservation)) {
    reject(&ClassRuntimeMetrics::rejected_kv_capacity);
    return;
  }
  const RequestKey key{spec.id, static_cast<std::uint32_t>(next)};
  const auto sequence = allocator_.allocate(key);
  if (!sequence) {
    reject(&ClassRuntimeMetrics::rejected_sequence_capacity);
    return;
  }
  RequestRecord request;
  request.spec = std::move(spec);
  request.key = key;
  request.sequence = *sequence;
  request.service_class = request.spec.service_class;
  request.arrived_at = clock_.now();
  request.ready_since = request.arrived_at;
  request.arrival_order = next_arrival_order_++;
  if (!kv_budget_.reserve(request.kv_lease, key, reservation)) {
    if (!allocator_.begin_clear(key) || !allocator_.clear_result(key, true)) fatal_error_ = true;
    reject(&ClassRuntimeMetrics::rejected_kv_capacity);
    return;
  }
  request.stream = event.output_stream ? std::move(event.output_stream)
                                       : std::make_shared<StreamBuffer>(request.spec.stream_capacity_bytes);
  if (!transition(request.state, RequestState::PrefillReady)) {
    reject(&ClassRuntimeMetrics::rejected_runtime_state);
    return;
  }
  requests_.emplace(key, std::move(request));
  saturating_increment(class_metrics_[service_class_index(requests_.at(key).service_class)].admitted);
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
  INFERSCALE_NVTX_RANGE("RuntimeReactor/CancellationCleanup");
  if (!transition(request.state, state)) return;
  if (request.kv_lease.soft_reserved() != 0 &&
      !kv_budget_.reclaim_soft_reservation(request.kv_lease,
                                           request.kv_lease.soft_reserved())) {
    fatal_error_ = true;
  }
  request.reserved_output_bytes = 0;
  if (state == RequestState::CompletePendingRetire) {
    auto& metrics = class_metrics_[service_class_index(request.service_class)];
    ++metrics.completed;
    bool met = true;
    const auto now = clock_.now();
    if (request.spec.ttft_slo) {
      met = request.first_output_at.has_value() &&
            *request.first_output_at - request.arrived_at <= *request.spec.ttft_slo;
    }
    if (met && request.spec.e2e_slo) met = now - request.arrived_at <= *request.spec.e2e_slo;
    if (met && (request.spec.ttft_slo || request.spec.e2e_slo)) ++metrics.slo_met;
  }
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
                                        std::uint32_t max_prefill_chunk,
                                        std::uint32_t max_prefill_tokens_per_epoch) {
  INFERSCALE_NVTX_RANGE("RuntimeReactor/SchedulerEpoch");
  const auto epoch_started = std::chrono::steady_clock::now();
  WorkPlan plan;
  {
    INFERSCALE_NVTX_RANGE("RuntimeReactor/WorkPlanConstruction");
    assert_owner();
    pending_scheduler_commit_.reset();
    const auto candidates = collect_scheduler_candidates(max_prefill_chunk,
                                                          max_prefill_tokens_per_epoch);
    std::vector<ReadyRequest> ready;
    ready.reserve(candidates.size());
    for (const auto& candidate : candidates) {
      ready.push_back({candidate.key, candidate.kind, candidate.token_begin,
                       candidate.tokens_remaining});
    }
    {
      INFERSCALE_NVTX_RANGE("RuntimeReactor/Scheduler/FIFO");
      plan = fifo_plan(ready, max_tokens, max_sequences, max_prefill_chunk,
                       max_prefill_tokens_per_epoch);
    }
    limit_plan_to_hard_growth(plan, candidates);
    eligible_requests_ = static_cast<std::uint32_t>(std::min<std::size_t>(
        candidates.size(), std::numeric_limits<std::uint32_t>::max()));
    selected_requests_ = plan.sequence_count;
  }
  last_scheduler_epoch_duration_ns_ = nonnegative_nanoseconds(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - epoch_started));
  return plan;
}

WorkPlan RuntimeReactor::make_sawds_plan(std::uint32_t max_tokens,
                                         std::uint32_t max_sequences,
                                         std::uint32_t max_prefill_chunk,
                                         std::uint32_t max_prefill_tokens_per_epoch) {
  INFERSCALE_NVTX_RANGE("RuntimeReactor/SchedulerEpoch");
  const auto epoch_started = std::chrono::steady_clock::now();
  WorkPlan work;
  {
    INFERSCALE_NVTX_RANGE("RuntimeReactor/WorkPlanConstruction");
    assert_owner();
    pending_scheduler_commit_.reset();
    if (max_prefill_tokens_per_epoch == 0) {
      max_prefill_tokens_per_epoch = config_.max_prefill_tokens_per_epoch;
    }
    const auto candidates = collect_scheduler_candidates(max_prefill_chunk,
                                                          max_prefill_tokens_per_epoch);
    SawdsPlan decision;
    {
      INFERSCALE_NVTX_RANGE("RuntimeReactor/Scheduler/SA-WDS");
      decision = sawds_plan(candidates, scheduler_credits_, clock_.now(), max_tokens,
                            max_sequences, max_prefill_chunk,
                            max_prefill_tokens_per_epoch, config_.sawds);
    }
    const auto credit_base = decision.credits_before_service;
    limit_plan_to_hard_growth(decision.work, candidates);
    eligible_requests_ = static_cast<std::uint32_t>(std::min<std::size_t>(
        candidates.size(), std::numeric_limits<std::uint32_t>::max()));
    // Resource trimming may remove work after policy planning. Charge exactly
    // the plan that survived hard-capacity authorization, using the same
    // per-round credit base returned by SA-WDS.
    decision.credits_after = credit_base;
    for (const auto& item : decision.work.items) {
      const auto candidate = std::find_if(candidates.begin(), candidates.end(),
          [&item](const SchedulerCandidate& value) { return value.key == item.key; });
      if (candidate == candidates.end()) {
        fatal_error_ = true;
        continue;
      }
      const auto class_index = service_class_index(candidate->service_class);
      decision.credits_after[class_index] =
          decision.credits_after[class_index] >= item.token_count
              ? decision.credits_after[class_index] - item.token_count : 0;
    }
    if (!decision.work.items.empty()) {
      pending_scheduler_commit_ = PendingSchedulerCommit{decision.work, decision.credits_after};
    }
    selected_requests_ = decision.work.sequence_count;
    work = std::move(decision.work);
  }
  last_scheduler_epoch_duration_ns_ = nonnegative_nanoseconds(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - epoch_started));
  return work;
}

WorkPlan RuntimeReactor::make_configured_plan(std::uint32_t max_tokens,
                                               std::uint32_t max_sequences,
                                               std::uint32_t max_prefill_chunk) {
  assert_owner();
  const auto epoch_budget = config_.adaptive_prefill.enabled
                                ? prefill_epoch_budget_
                                : config_.max_prefill_tokens_per_epoch;
  if (config_.scheduler == SchedulerMode::FIFO) {
    return make_fifo_plan(max_tokens, max_sequences, max_prefill_chunk,
                          epoch_budget);
  }
  return make_sawds_plan(max_tokens, max_sequences, max_prefill_chunk,
                         epoch_budget);
}

bool RuntimeReactor::resolve_resource_stall() {
  INFERSCALE_NVTX_RANGE("RuntimeReactor/KV/ResourceStall");
  assert_owner();
  if (inflight_ticket_count() != 0 || fatal_error_ || shutting_down_) return false;
  for (const auto& [key, request] : requests_) {
    (void)key;
    if (request.clear_queued) return false;
  }

  RequestRecord* victim = nullptr;
  bool resource_blocked_ready_work = false;
  for (const auto key : fifo_) {
    auto* request = find(key);
    if (request == nullptr || !is_ready(*request) || request->backpressured ||
        request->inflight_tickets != 0) continue;
    const auto kind = request->state == RequestState::PrefillReady
                          ? WorkKind::Prefill : WorkKind::Decode;
    const auto remaining = kind == WorkKind::Prefill
        ? request->spec.prompt_tokens - request->prefill_cursor : 1U;
    if (remaining == 0) continue;
    const std::uint32_t desired = 1;
    if (kv_budget_.max_authorizable_growth(request->kv_lease, desired) != 0) {
      // A work item can proceed on a later deterministic credit epoch; resource
      // retirement is only a final escape when hard KV charge blocks every item.
      return false;
    }
    resource_blocked_ready_work = true;
    if (victim == nullptr ||
        request->kv_lease.committed() > victim->kv_lease.committed() ||
        (request->kv_lease.committed() == victim->kv_lease.committed() &&
         request->arrival_order < victim->arrival_order)) {
      victim = request;
    }
  }
  if (!resource_blocked_ready_work || victim == nullptr) return false;
  auto& metric = class_metrics_[service_class_index(victim->service_class)].hard_limit_aborted;
  if (metric != std::numeric_limits<std::uint64_t>::max()) ++metric;
  note_terminal(*victim, RequestState::FailedPendingRetire);
  return true;
}

std::vector<SchedulerCandidate> RuntimeReactor::collect_scheduler_candidates(
    std::uint32_t max_prefill_chunk,
    std::uint32_t max_prefill_tokens_per_epoch) {
  std::vector<SchedulerCandidate> ready;
  ready.reserve(fifo_.size());
  for (const auto key : fifo_) {
    auto* request = find(key);
    if (request == nullptr || !is_ready(*request)) continue;
    if (request->spec.deadline && *request->spec.deadline <= clock_.now()) {
      note_terminal(*request, RequestState::FailedPendingRetire);
      continue;
    }
    WorkKind kind{};
    std::uint32_t token_begin{};
    std::uint32_t remaining{};
    std::uint32_t growth_bound{};
    if (request->state == RequestState::PrefillReady) {
      kind = WorkKind::Prefill;
      token_begin = request->prefill_cursor;
      remaining = request->spec.prompt_tokens - request->prefill_cursor;
      growth_bound = std::min({remaining, max_prefill_chunk,
                               max_prefill_tokens_per_epoch});
    } else {
      const auto available = request->spec.stream_capacity_bytes - request->stream->bytes() -
                             request->reserved_output_bytes;
      if (request->backpressured || available < max_output_piece_bytes_) {
        if (!request->backpressured) request->ready_since = clock_.now();
        request->backpressured = true;
        continue;
      }
      kind = WorkKind::Decode;
      token_begin = request->prefill_cursor + request->generated_tokens;
      remaining = 1;
      growth_bound = 1;
    }
    const auto authorizable = kv_budget_.max_authorizable_growth(request->kv_lease,
                                                                 growth_bound);
    if (authorizable == 0) {
      if (!request->resource_blocked) {
        request->resource_blocked = true;
        saturating_increment(class_metrics_[service_class_index(request->service_class)]
                                 .resource_deferred);
      }
      continue;
    }
    if (request->resource_blocked) {
      request->resource_blocked = false;
      saturating_increment(class_metrics_[service_class_index(request->service_class)]
                               .resource_unblocked);
    }
    remaining = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, authorizable));
    ready.push_back({key, request->service_class, kind, token_begin, remaining,
                     request->ready_since, request->spec.deadline,
                     request->arrival_order, request->kv_lease.soft_reserved()});
  }
  return ready;
}

void RuntimeReactor::limit_plan_to_hard_growth(
    WorkPlan& plan, const std::vector<SchedulerCandidate>& candidates) const {
  const auto charged = kv_budget_.charged();
  auto hard_headroom = charged <= kv_budget_.hard_capacity()
      ? kv_budget_.hard_capacity() - charged : 0U;
  std::vector<WorkItem> retained;
  retained.reserve(plan.items.size());
  std::uint64_t total = 0;
  for (auto item : plan.items) {
    const auto candidate = std::find_if(candidates.begin(), candidates.end(),
        [&item](const SchedulerCandidate& value) { return value.key == item.key; });
    if (candidate == candidates.end()) continue;
    const auto from_soft = std::min<std::uint64_t>(item.expected_kv_growth,
                                                   candidate->soft_reserved_tokens);
    const auto outgrowth = static_cast<std::uint64_t>(item.expected_kv_growth) - from_soft;
    const auto granted_outgrowth = std::min(outgrowth, hard_headroom);
    hard_headroom -= granted_outgrowth;
    const auto granted = from_soft + granted_outgrowth;
    if (granted == 0 || (item.kind == WorkKind::Decode && granted != 1)) continue;
    if (granted < item.token_count) {
      item.token_count = static_cast<std::uint32_t>(granted);
      item.expected_kv_growth = item.token_count;
    }
    total += item.token_count;
    retained.push_back(item);
  }
  plan.items = std::move(retained);
  plan.total_tokens = static_cast<std::uint32_t>(total);
  plan.sequence_count = static_cast<std::uint32_t>(plan.items.size());
}

void RuntimeReactor::reclaim_excess_soft_reservations() {
  for (auto& [key, request] : requests_) {
    (void)key;
    if (request.kv_lease.released() || request.kv_lease.soft_reserved() == 0) continue;
    std::uint64_t future_growth = 0;
    if (!terminal_intent(request.state) && request.state != RequestState::Terminal) {
      const auto prompt_remaining = request.spec.prompt_tokens - request.prefill_cursor;
      const auto output_remaining = std::min(
          request.spec.max_output_tokens - request.generated_tokens,
          config_.admission_generation_reserve_tokens);
      future_growth = static_cast<std::uint64_t>(prompt_remaining) + output_remaining;
      if (future_growth > request.kv_lease.authorized()) {
        future_growth -= request.kv_lease.authorized();
      } else {
        future_growth = 0;
      }
    }
    const auto reclaimable = request.kv_lease.soft_reserved() > future_growth
        ? request.kv_lease.soft_reserved() - future_growth : 0U;
    if (reclaimable != 0 &&
        !kv_budget_.reclaim_soft_reservation(request.kv_lease, reclaimable)) {
      fatal_error_ = true;
    }
  }
}

void RuntimeReactor::observe_adaptive_prefill(
    const BatchCompletedEvent& event, std::uint64_t batch_id,
    std::optional<std::uint64_t> interactive_ttft_ms,
    std::uint32_t prefill_tokens, std::uint32_t decode_tokens,
    ClockTime completed_at) {
  INFERSCALE_NVTX_RANGE("RuntimeReactor/AdaptivePrefillUpdate");
  last_backend_duration_ns_ = nonnegative_nanoseconds(event.backend_duration);
  saturating_add(total_backend_duration_ns_, last_backend_duration_ns_);
  const auto& policy = config_.adaptive_prefill;
  const auto before = prefill_epoch_budget_;
  std::uint32_t decode_backlog = 0;
  std::uint32_t oldest_interactive_ready_wait_ms = 0;
  bool interactive_ready_present = false;
  for (const auto& [key, request] : requests_) {
    (void)key;
    if (request.state == RequestState::DecodeReady &&
        request.service_class == RequestClass::Interactive && !request.backpressured &&
        decode_backlog != std::numeric_limits<std::uint32_t>::max()) {
      ++decode_backlog;
    }
    if (is_ready(request) && request.service_class == RequestClass::Interactive &&
        !request.backpressured) {
      interactive_ready_present = true;
      if (completed_at > request.ready_since) {
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
            completed_at - request.ready_since).count();
        oldest_interactive_ready_wait_ms = std::max(oldest_interactive_ready_wait_ms,
            static_cast<std::uint32_t>(std::min<std::int64_t>(
                std::max<std::int64_t>(0, wait), std::numeric_limits<std::uint32_t>::max())));
      }
    }
  }

  std::uint16_t idle_basis_points = 0;
  bool idle_observation_valid = false;
  if (previous_batch_completion_ && completed_at > *previous_batch_completion_ &&
      event.backend_duration.count() >= 0) {
    const auto interval = completed_at - *previous_batch_completion_;
    const auto busy = event.backend_duration;
    const auto idle = interval > busy ? interval - busy : std::chrono::nanoseconds::zero();
    const auto fraction = static_cast<long double>(idle.count()) * 10000.0L /
                          static_cast<long double>(interval.count());
    idle_basis_points = static_cast<std::uint16_t>(std::clamp<long double>(
        fraction, 0.0L, 10000.0L));
    idle_observation_valid = true;
  }
  previous_batch_completion_ = completed_at;

  if (!policy.enabled) {
    prefill_epoch_budget_ = config_.max_prefill_tokens_per_epoch;
  } else if (!interactive_ttft_ms && decode_backlog == 0 &&
             !interactive_ready_present) {
    // With no interactive TTFT, ready queue, or decode backlog observation,
    // return to the fixed budget rather than adapting from absent signals.
    prefill_epoch_budget_ = std::clamp(config_.max_prefill_tokens_per_epoch,
                                      policy.minimum_tokens, policy.maximum_tokens);
    high_pressure_windows_ = 0;
    low_pressure_windows_ = 0;
    adaptive_cooldown_remaining_ = 0;
    adaptive_ewma_ttft_ms_ = 0;
    has_adaptive_ewma_ = false;
    adaptive_prefill_reason_ = AdaptivePrefillReason::StaticFallback;
    if (policy.trace_capacity != 0) {
      if (adaptive_trace_.size() == policy.trace_capacity) adaptive_trace_.pop_front();
      adaptive_trace_.push_back({batch_id, before, prefill_epoch_budget_, prefill_tokens,
                                 decode_tokens, decode_backlog,
                                 oldest_interactive_ready_wait_ms, 0,
                                 idle_basis_points, AdaptivePrefillReason::StaticFallback});
    }
  } else {
    if (interactive_ttft_ms) {
      if (!has_adaptive_ewma_) {
        adaptive_ewma_ttft_ms_ = *interactive_ttft_ms;
        has_adaptive_ewma_ = true;
      } else {
        const auto old_q = adaptive_ewma_ttft_ms_ / 4;
        const auto new_q = *interactive_ttft_ms / 4;
        const auto remainder = ((adaptive_ewma_ttft_ms_ % 4) * 3 +
                                (*interactive_ttft_ms % 4)) / 4;
        adaptive_ewma_ttft_ms_ = old_q * 3 + new_q + remainder;
      }
    } else if (has_adaptive_ewma_) {
      adaptive_ewma_ttft_ms_ -= adaptive_ewma_ttft_ms_ / 4;
    }

    const bool high_pressure =
        (interactive_ttft_ms.has_value() && has_adaptive_ewma_ &&
         adaptive_ewma_ttft_ms_ >= policy.high_interactive_wait_ms) ||
        decode_backlog >= policy.high_decode_backlog ||
        oldest_interactive_ready_wait_ms >= policy.high_interactive_wait_ms;
    const bool low_pressure =
        (!has_adaptive_ewma_ ||
         adaptive_ewma_ttft_ms_ <= policy.low_interactive_wait_ms) &&
        decode_backlog <= policy.low_decode_backlog &&
        oldest_interactive_ready_wait_ms <= policy.low_interactive_wait_ms &&
        idle_observation_valid &&
        idle_basis_points >= policy.high_executor_idle_basis_points;

    const bool in_cooldown = adaptive_cooldown_remaining_ != 0;
    if (in_cooldown) {
      --adaptive_cooldown_remaining_;
    }
    AdaptivePrefillReason reason = AdaptivePrefillReason::HysteresisHold;
    if (high_pressure) {
      low_pressure_windows_ = 0;
      if (high_pressure_windows_ < policy.windows_before_change) ++high_pressure_windows_;
    } else if (low_pressure) {
      high_pressure_windows_ = 0;
      if (low_pressure_windows_ < policy.windows_before_change) ++low_pressure_windows_;
    } else {
      high_pressure_windows_ = 0;
      low_pressure_windows_ = 0;
    }

    if (in_cooldown) {
      reason = AdaptivePrefillReason::Cooldown;
    } else if (high_pressure_windows_ >= policy.windows_before_change &&
               prefill_epoch_budget_ > policy.minimum_tokens) {
      const auto decrease = std::min(policy.step_tokens,
                                     prefill_epoch_budget_ - policy.minimum_tokens);
      prefill_epoch_budget_ -= decrease;
      high_pressure_windows_ = 0;
      adaptive_cooldown_remaining_ = policy.cooldown_windows;
      reason = AdaptivePrefillReason::ReduceForInteractivePressure;
    } else if (low_pressure_windows_ >= policy.windows_before_change &&
               prefill_epoch_budget_ < policy.maximum_tokens) {
      const auto increase = std::min(policy.step_tokens,
                                     policy.maximum_tokens - prefill_epoch_budget_);
      prefill_epoch_budget_ += increase;
      low_pressure_windows_ = 0;
      adaptive_cooldown_remaining_ = policy.cooldown_windows;
      reason = AdaptivePrefillReason::IncreaseForIdleCapacity;
    }
    adaptive_prefill_reason_ = reason;

    if (policy.trace_capacity != 0) {
      if (adaptive_trace_.size() == policy.trace_capacity) adaptive_trace_.pop_front();
      adaptive_trace_.push_back({batch_id, before, prefill_epoch_budget_, prefill_tokens,
                                 decode_tokens, decode_backlog, oldest_interactive_ready_wait_ms,
                                 adaptive_ewma_ttft_ms_,
                                 idle_basis_points, reason});
    }
  }
  if (!policy.enabled && policy.trace_capacity != 0) {
    if (adaptive_trace_.size() == policy.trace_capacity) adaptive_trace_.pop_front();
    adaptive_trace_.push_back({batch_id, before, prefill_epoch_budget_, prefill_tokens,
                               decode_tokens, decode_backlog, oldest_interactive_ready_wait_ms,
                               adaptive_ewma_ttft_ms_,
                               idle_basis_points, AdaptivePrefillReason::StaticFallback});
  }
  if (!policy.enabled) adaptive_prefill_reason_ = AdaptivePrefillReason::StaticFallback;
}

std::optional<BatchTicket> RuntimeReactor::issue(const WorkPlan& plan) {
  INFERSCALE_NVTX_RANGE("RuntimeReactor/IssueAuthorizePin");
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
        item.expected_kv_growth != item.token_count ||
        (item.kind == WorkKind::Decode && item.token_count != 1)) return std::nullopt;
    if (item.kind == WorkKind::Decode &&
        request->spec.stream_capacity_bytes - request->stream->bytes() - request->reserved_output_bytes <
            max_output_piece_bytes_) return std::nullopt;
    const auto sequence = allocator_.find(item.key);
    if (!sequence || !kv_budget_.can_authorize(request->kv_lease,
                                                item.expected_kv_growth)) return std::nullopt;
    records.push_back(request);
    entries.push_back({item, *sequence, request->spec.prompt_token_ids,
                       request->spec.max_output_tokens});
  }
  std::size_t authorized_count = 0;
  {
    INFERSCALE_NVTX_RANGE("RuntimeReactor/KVBudgetAuthorizationDecision");
    for (; authorized_count < records.size(); ++authorized_count) {
      if (!kv_budget_.authorize_growth(records[authorized_count]->kv_lease,
                                       plan.items[authorized_count].expected_kv_growth)) {
        for (std::size_t rollback = 0; rollback < authorized_count; ++rollback) {
          if (!kv_budget_.rollback_growth(records[rollback]->kv_lease,
                                          plan.items[rollback].expected_kv_growth)) fatal_error_ = true;
        }
        return std::nullopt;
      }
    }
  }
  std::size_t pinned_count = 0;
  for (; pinned_count < records.size(); ++pinned_count) {
    if (!allocator_.pin(records[pinned_count]->key)) {
      for (std::size_t rollback = 0; rollback < pinned_count; ++rollback) {
        if (!allocator_.retire(records[rollback]->key)) fatal_error_ = true;
      }
      for (std::size_t rollback = 0; rollback < authorized_count; ++rollback) {
        if (!kv_budget_.rollback_growth(records[rollback]->kv_lease,
                                        plan.items[rollback].expected_kv_growth)) fatal_error_ = true;
      }
      return std::nullopt;
    }
  }
  const auto issued_at = clock_.now();
  for (std::size_t i = 0; i < records.size(); ++i) {
    auto* request = records[i];
    ++request->inflight_tickets;
    if (plan.items[i].kind == WorkKind::Decode) request->reserved_output_bytes += max_output_piece_bytes_;
    const auto next = plan.items[i].kind == WorkKind::Prefill ? RequestState::PrefillInFlight
                                                              : RequestState::DecodeInFlight;
    if (!transition(request->state, next)) throw std::logic_error("invalid state at ticket issue");
    if (!request->first_scheduled_at) request->first_scheduled_at = issued_at;
    const auto wait = issued_at > request->ready_since
        ? std::chrono::duration_cast<std::chrono::milliseconds>(issued_at - request->ready_since)
        : std::chrono::milliseconds::zero();
    auto& metrics = class_metrics_[service_class_index(request->service_class)];
    metrics.maximum_ready_wait_ms = std::max(metrics.maximum_ready_wait_ms,
        static_cast<std::uint64_t>(std::max<std::int64_t>(0, wait.count())));
    saturating_increment(metrics.scheduler_wait_histogram[
        ready_wait_bucket(static_cast<std::uint64_t>(std::max<std::int64_t>(0, wait.count())))]);
    if (!request->starvation_recorded && wait >= config_.sawds.starvation_after) {
      ++metrics.starvation_events;
      request->starvation_recorded = true;
    }
  }
  const auto batch_id = next_batch_id_++;
  issued_tickets_.emplace(batch_id, entries);
  for (std::size_t i = 0; i < records.size(); ++i) {
    saturating_add(class_metrics_[service_class_index(records[i]->service_class)].service_tokens,
                  plan.items[i].token_count);
  }
  if (pending_scheduler_commit_ &&
      pending_scheduler_commit_->plan.items == plan.items &&
      pending_scheduler_commit_->plan.total_tokens == plan.total_tokens &&
      pending_scheduler_commit_->plan.sequence_count == plan.sequence_count) {
    scheduler_credits_ = pending_scheduler_commit_->credits_after;
  }
  pending_scheduler_commit_.reset();
  const auto issued_interval = previous_issue_time_ && issued_at > *previous_issue_time_
      ? std::chrono::duration_cast<std::chrono::nanoseconds>(issued_at - *previous_issue_time_)
      : std::chrono::nanoseconds::zero();
  last_scheduler_epoch_interval_ns_ = nonnegative_nanoseconds(issued_interval);
  previous_issue_time_ = issued_at;
  last_batch_tokens_ = plan.total_tokens;
  last_batch_sequences_ = plan.sequence_count;
  last_batch_prefill_tokens_ = 0;
  last_batch_decode_tokens_ = 0;
  for (const auto& item : plan.items) {
    if (item.kind == WorkKind::Prefill) {
      last_batch_prefill_tokens_ += item.token_count;
      saturating_add(total_prefill_tokens_issued_, item.token_count);
    } else {
      last_batch_decode_tokens_ += item.token_count;
      saturating_add(total_decode_tokens_issued_, item.token_count);
    }
  }
  selected_requests_ = plan.sequence_count;
  return BatchTicket(batch_id, std::move(entries));
}

void RuntimeReactor::handle_completion(BatchCompletedEvent event) {
  INFERSCALE_NVTX_RANGE("RuntimeReactor/CompletionSettle");
  auto& ticket = event.ticket;
  const auto issued = issued_tickets_.find(ticket.id());
  const bool matches_issued = issued != issued_tickets_.end() && issued->second == ticket.entries();
  if (!ticket.retire() || !matches_issued) {
    ++stale_events_;
    return;
  }
  std::uint32_t batch_prefill_tokens = 0;
  std::uint32_t batch_decode_tokens = 0;
  for (const auto& entry : ticket.entries()) {
    if (entry.work.kind == WorkKind::Prefill) batch_prefill_tokens += entry.work.token_count;
    else batch_decode_tokens += entry.work.token_count;
  }
  issued_tickets_.erase(issued);
  ++completed_batches_;
  const bool committed_growth_shape =
      event.result.growth_resolution == GrowthResolution::Committed &&
      event.result.committed_kv_growth.size() == ticket.entries().size();
  if ((event.result.ok && (event.result.outputs.size() != ticket.entries().size() ||
                           !committed_growth_shape)) ||
      (event.result.growth_resolution == GrowthResolution::Committed &&
       event.result.committed_kv_growth.size() != ticket.entries().size()) ||
      (!event.result.ok && (event.result.failure == FailureClass::EngineRestartRequired ||
                            event.result.failure == FailureClass::ProcessFatal))) {
    fatal_error_ = true;
  }

  // Settle sequence growth before lifecycle/output handling. A cancellation or
  // deadline can suppress a response after the backend has already committed.
  if (committed_growth_shape) {
    for (std::size_t index = 0; index < ticket.entries().size(); ++index) {
      const auto& entry = ticket.entries()[index];
      auto* request = find(entry.work.key);
      const auto actual = event.result.committed_kv_growth[index];
      if (request == nullptr ||
          !kv_budget_.commit_growth(request->kv_lease, entry.work.expected_kv_growth, actual)) {
        fatal_error_ = true;
        continue;
      }
    }
  } else if (event.result.growth_resolution == GrowthResolution::NotExecuted) {
    for (const auto& entry : ticket.entries()) {
      auto* request = find(entry.work.key);
      if (request == nullptr ||
          !kv_budget_.rollback_growth(request->kv_lease,
                                      entry.work.expected_kv_growth)) {
        fatal_error_ = true;
      }
    }
  }
  if (event.result.ok && !committed_growth_shape) fatal_error_ = true;

  const auto now = clock_.now();
  std::optional<std::uint64_t> interactive_ttft_sample;
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
        if (event.result.committed_kv_growth[index] != entry.work.token_count) {
          fatal_error_ = true;
          note_terminal(*request, RequestState::FailedPendingRetire);
          maybe_queue_clear(*request);
          continue;
        }
        request->prefill_cursor += entry.work.token_count;
        const auto next = request->prefill_cursor < request->spec.prompt_tokens
                              ? RequestState::PrefillReady : RequestState::DecodeReady;
        if (!transition(request->state, next)) fatal_error_ = true;
        request->ready_since = now;
      } else {
        const auto& output = event.result.outputs[index];
        if (!output) {
          fatal_error_ = true;
          note_terminal(*request, RequestState::FailedPendingRetire);
        } else if (output->piece.size() > max_output_piece_bytes_) {
          fatal_error_ = true;
          note_terminal(*request, RequestState::FailedPendingRetire);
        } else {
          bool enqueued = true;
          if (!output->piece.empty()) {
            INFERSCALE_NVTX_RANGE("RuntimeReactor/StreamEnqueue");
            enqueued = request->stream->try_push(output->piece);
          }
          if (!enqueued) {
            fatal_error_ = true;
            note_terminal(*request, RequestState::FailedPendingRetire);
          } else {
            if (!output->piece.empty() && !request->first_output_at) {
            request->first_output_at = now;
            if (request->service_class == RequestClass::Interactive) {
              const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - request->arrived_at).count();
              interactive_ttft_sample = std::max<std::uint64_t>(
                  interactive_ttft_sample.value_or(0),
                  static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)));
            }
            }
            if (output->end_of_sequence) {
              note_terminal(*request, RequestState::CompletePendingRetire);
            } else {
              ++request->generated_tokens;
              if (request->generated_tokens >= request->spec.max_output_tokens) {
                note_terminal(*request, RequestState::CompletePendingRetire);
              } else if (!transition(request->state, RequestState::DecodeReady)) {
                fatal_error_ = true;
              } else {
                request->ready_since = now;
              }
            }
          }
        }
      }
    }
    maybe_queue_clear(*request);
  }
  observe_adaptive_prefill(event, ticket.id(), interactive_ttft_sample,
                           batch_prefill_tokens, batch_decode_tokens, now);
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
  INFERSCALE_NVTX_RANGE("RuntimeReactor/KV/ClearAckRelease");
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
  if (!kv_budget_.release(request->kv_lease)) {
    fatal_error_ = true;
    return;
  }
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
  const auto wait = clock_.now() > request->ready_since
      ? std::chrono::duration_cast<std::chrono::milliseconds>(clock_.now() - request->ready_since)
      : std::chrono::milliseconds::zero();
  return RequestSnapshot{request->key, request->state, request->prefill_cursor,
                         request->generated_tokens, request->inflight_tickets,
                         request->kv_lease.committed(), request->backpressured,
                         request->service_class, request->kv_lease.soft_reserved(),
                         request->kv_lease.peak(), wait};
}

RuntimeMetricsSnapshot RuntimeReactor::metrics() const {
  assert_owner();
  RuntimeMetricsSnapshot result;
  result.by_class = class_metrics_;
  for (auto& metrics : result.by_class) {
    std::uint64_t total_samples = 0;
    for (const auto bucket_count : metrics.scheduler_wait_histogram) {
      saturating_add(total_samples, bucket_count);
    }
    if (total_samples != 0) {
      const auto percentile_rank = total_samples - total_samples / 100U;
      std::uint64_t cumulative = 0;
      for (std::size_t bucket = 0; bucket < metrics.scheduler_wait_histogram.size(); ++bucket) {
        saturating_add(cumulative, metrics.scheduler_wait_histogram[bucket]);
        if (cumulative >= percentile_rank) {
          const auto upper_bound = bucket == 0 ? 0U
              : bucket >= 32 ? std::numeric_limits<std::uint32_t>::max()
                             : (std::uint64_t{1} << bucket) - 1U;
          metrics.scheduler_wait_p99_ms = static_cast<std::uint32_t>(upper_bound);
          break;
        }
      }
    }
  }
  result.scheduler = config_.scheduler;
  result.adaptive_prefill_enabled = config_.adaptive_prefill.enabled;
  result.prefill_epoch_budget = config_.adaptive_prefill.enabled
      ? prefill_epoch_budget_ : config_.max_prefill_tokens_per_epoch;
  result.adaptive_prefill_reason = adaptive_prefill_reason_;
  result.eligible_requests = eligible_requests_;
  result.selected_requests = selected_requests_;
  result.last_batch_tokens = last_batch_tokens_;
  result.last_batch_sequences = last_batch_sequences_;
  result.last_batch_prefill_tokens = last_batch_prefill_tokens_;
  result.last_batch_decode_tokens = last_batch_decode_tokens_;
  result.last_scheduler_epoch_interval_ns = last_scheduler_epoch_interval_ns_;
  result.last_scheduler_epoch_duration_ns = last_scheduler_epoch_duration_ns_;
  result.last_backend_duration_ns = last_backend_duration_ns_;
  result.total_backend_duration_ns = total_backend_duration_ns_;
  result.total_prefill_tokens_issued = total_prefill_tokens_issued_;
  result.total_decode_tokens_issued = total_decode_tokens_issued_;
  result.adaptive_prefill_trace.assign(adaptive_trace_.begin(), adaptive_trace_.end());
  result.completed_batches = completed_batches_;
  result.rejected_kv_reservations = kv_budget_.rejected_reservations();
  result.live_requests = requests_.size();
  result.live_sequences = allocator_.live_sequences();
  result.queued_requests = fifo_.size();
  result.inflight_tickets = inflight_ticket_count();
  result.event_queue_depth = events_.size();
  for (const auto& [key, request] : requests_) {
    (void)key;
    result.stream_buffered_bytes += request.stream->bytes();
    result.stream_reserved_bytes += request.reserved_output_bytes;
  }
  result.hard_usable_kv_tokens = kv_budget_.hard_capacity();
  result.charged_kv_tokens = kv_budget_.charged();
  result.available_kv_tokens = result.charged_kv_tokens <= result.hard_usable_kv_tokens
      ? result.hard_usable_kv_tokens - result.charged_kv_tokens : 0U;
  result.available_sequences = max_sequences_ >= allocator_.live_sequences()
      ? max_sequences_ - allocator_.live_sequences() : 0U;
  result.committed_kv_tokens = kv_budget_.committed();
  result.soft_reserved_kv_tokens = kv_budget_.soft_reserved();
  result.authorized_kv_tokens = kv_budget_.authorized();
  result.peak_charged_kv_tokens = kv_budget_.peak_charged();
  result.released_kv_tokens = kv_budget_.released_tokens();
  result.pressure = kv_budget_.pressure_state();
  result.admission_ready = !shutting_down_ && !fatal_error_ &&
      result.available_sequences != 0 && result.pressure == KvPressureState::Normal;
  return result;
}

std::uint32_t RuntimeReactor::prefill_epoch_budget() const {
  assert_owner();
  return config_.adaptive_prefill.enabled ? prefill_epoch_budget_
                                          : config_.max_prefill_tokens_per_epoch;
}

std::vector<AdaptivePrefillTraceEntry> RuntimeReactor::adaptive_prefill_trace() const {
  assert_owner();
  return {adaptive_trace_.begin(), adaptive_trace_.end()};
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
