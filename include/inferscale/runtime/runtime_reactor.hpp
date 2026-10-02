#pragma once

#include "inferscale/engine/error_classifier.hpp"
#include "inferscale/execution/batch_ticket.hpp"
#include "inferscale/runtime/bounded_queue.hpp"
#include "inferscale/runtime/clock.hpp"
#include "inferscale/runtime/lifecycle.hpp"
#include "inferscale/runtime/sequence_allocator.hpp"
#include "inferscale/runtime/stream_buffer.hpp"
#include "inferscale/runtime/types.hpp"

#include <deque>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <variant>

namespace inferscale {

enum class AdmissionResult : std::uint8_t { Pending, Accepted, Rejected };
struct NewRequestEvent {
  RequestSpec spec;
  std::uint32_t generation_hint{};
  std::shared_ptr<StreamBuffer> output_stream;
  std::shared_ptr<std::atomic<AdmissionResult>> admission;
};
struct CancelEvent { RequestKey key; };
struct DeadlineEvent { RequestKey key; };
struct StreamWritableEvent { RequestKey key; };
struct BatchCompletedEvent { BatchTicket ticket; EngineResult result; };
struct SequenceClearedEvent { RequestKey key; bool succeeded{}; };
struct ShutdownEvent {};
using RuntimeEvent = std::variant<NewRequestEvent, CancelEvent, DeadlineEvent, StreamWritableEvent,
                                  BatchCompletedEvent, SequenceClearedEvent, ShutdownEvent>;

struct ClearRequest { RequestKey key; SequenceId sequence{}; };

struct RequestSnapshot {
  RequestKey key;
  RequestState state{};
  std::uint32_t prefill_cursor{};
  std::uint32_t generated_tokens{};
  std::uint32_t inflight_tickets{};
  std::uint64_t estimated_committed_tokens{};
  bool stream_backpressured{false};
};

class RuntimeReactor {
 public:
  RuntimeReactor(const Clock& clock, std::uint32_t max_sequences,
                 std::size_t event_capacity = 256,
                 std::uint32_t context_limit = 32768,
                 std::uint32_t max_output_piece_bytes = 11);

  bool post(RuntimeEvent event);
  bool post_completion(RuntimeEvent event);
  bool run_one();
  bool wait_one_for(std::chrono::milliseconds timeout);
  std::size_t run_ready();
  void seal_events();
  std::size_t expire_deadlines();

  WorkPlan make_fifo_plan(std::uint32_t max_tokens, std::uint32_t max_sequences,
                          std::uint32_t max_prefill_chunk);
  std::optional<BatchTicket> issue(const WorkPlan& plan);
  bool cancel(RequestKey key);
  void begin_shutdown();
  void fail_all();

  std::optional<RequestKey> latest_key(std::uint64_t id) const;
  std::optional<RequestSnapshot> snapshot(RequestKey key) const;
  std::shared_ptr<StreamBuffer> stream_for(RequestKey key) const;
  std::vector<ClearRequest> take_clear_requests();
  std::uint64_t stale_event_count() const { assert_owner(); return stale_events_; }
  std::uint64_t estimated_committed_tokens() const { assert_owner(); return committed_tokens_; }
  std::uint32_t live_sequences() const { assert_owner(); return allocator_.live_sequences(); }
  std::size_t queued_request_count() const { assert_owner(); return fifo_.size(); }
  std::uint64_t inflight_ticket_count() const;
  bool shutting_down() const { assert_owner(); return shutting_down_; }
  bool fatal_error() const { assert_owner(); return fatal_error_; }
  bool shutdown_complete() const;

 private:
  struct RequestRecord {
    RequestSpec spec;
    RequestKey key;
    RequestState state{RequestState::Received};
    SequenceId sequence{};
    std::uint32_t prefill_cursor{};
    std::uint32_t generated_tokens{};
    std::uint32_t inflight_tickets{};
    std::uint64_t committed_tokens{};
    std::size_t reserved_output_bytes{};
    bool backpressured{false};
    bool clear_queued{false};
    std::shared_ptr<StreamBuffer> stream;
  };

  void assert_owner() const;
  void handle(RuntimeEvent event);
  void handle_new(NewRequestEvent event);
  void handle_cancel(RequestKey key, RequestState terminal_state);
  void handle_completion(BatchCompletedEvent event);
  void maybe_queue_clear(RequestRecord& request);
  void process_clear(SequenceClearedEvent event);
  RequestRecord* find(RequestKey key);
  const RequestRecord* find(RequestKey key) const;
  bool is_ready(const RequestRecord& request) const noexcept;
  void note_terminal(RequestRecord& request, RequestState state);

  const Clock& clock_;
  BoundedQueue<RuntimeEvent> events_;
  SequenceAllocator allocator_;
  const std::uint32_t context_limit_;
  const std::uint32_t max_output_piece_bytes_;
  std::unordered_map<RequestKey, RequestRecord, RequestKeyHash> requests_;
  std::unordered_map<std::uint64_t, std::uint32_t> last_generation_;
  std::unordered_map<std::uint64_t, std::vector<TicketEntry>> issued_tickets_;
  std::deque<RequestKey> fifo_;
  std::vector<ClearRequest> clear_requests_;
  mutable std::thread::id owner_{};
  std::uint64_t next_batch_id_{1};
  std::uint64_t stale_events_{};
  std::uint64_t committed_tokens_{};
  bool shutting_down_{false};
  bool fatal_error_{false};
};

}  // namespace inferscale
