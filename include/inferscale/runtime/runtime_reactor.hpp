#pragma once

#include "inferscale/engine/error_classifier.hpp"
#include "inferscale/execution/batch_ticket.hpp"
#include "inferscale/runtime/bounded_queue.hpp"
#include "inferscale/runtime/clock.hpp"
#include "inferscale/runtime/lifecycle.hpp"
#include "inferscale/runtime/kv_budget.hpp"
#include "inferscale/runtime/sequence_allocator.hpp"
#include "inferscale/runtime/stream_buffer.hpp"
#include "inferscale/runtime/types.hpp"
#include "inferscale/scheduler/sawds.hpp"

#include <array>
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
struct BatchCompletedEvent {
  BatchTicket ticket;
  EngineResult result;
  std::chrono::nanoseconds backend_duration{};
};
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
  RequestClass service_class{RequestClass::Interactive};
  std::uint64_t soft_reserved_tokens{};
  std::uint64_t peak_kv_tokens{};
  std::chrono::milliseconds ready_wait{};
};

enum class SchedulerMode : std::uint8_t { FIFO, SAWDS };

struct AdaptivePrefillConfig {
  bool enabled{false};
  std::uint32_t minimum_tokens{128};
  std::uint32_t maximum_tokens{2048};
  std::uint32_t initial_tokens{1024};
  std::uint32_t step_tokens{128};
  std::uint32_t high_interactive_wait_ms{750};
  std::uint32_t low_interactive_wait_ms{500};
  std::uint32_t high_decode_backlog{8};
  std::uint32_t low_decode_backlog{2};
  std::uint16_t high_executor_idle_basis_points{5000};
  std::uint32_t windows_before_change{3};
  std::uint32_t cooldown_windows{5};
  std::size_t trace_capacity{128};
};

enum class AdaptivePrefillReason : std::uint8_t {
  StaticFallback,
  HysteresisHold,
  ReduceForInteractivePressure,
  IncreaseForIdleCapacity,
  Cooldown
};

struct AdaptivePrefillTraceEntry {
  std::uint64_t batch_id{};
  std::uint32_t budget_before{};
  std::uint32_t budget_after{};
  std::uint32_t prefill_tokens{};
  std::uint32_t decode_tokens{};
  std::uint32_t decode_backlog{};
  std::uint32_t oldest_interactive_ready_wait_ms{};
  std::uint64_t interactive_ttft_ewma_ms{};
  std::uint16_t executor_idle_basis_points{};
  AdaptivePrefillReason reason{AdaptivePrefillReason::StaticFallback};
};

struct RuntimeConfig {
  std::uint64_t hard_usable_kv_tokens{};
  std::uint16_t soft_pressure_basis_points{7000};
  std::uint16_t high_pressure_basis_points{8500};
  std::uint64_t kv_safety_margin_tokens{16};
  std::uint32_t admission_generation_reserve_tokens{64};
  SchedulerMode scheduler{SchedulerMode::FIFO};
  std::uint32_t max_prefill_tokens_per_epoch{1024};
  std::uint32_t scheduler_prefill_chunk_tokens{};
  SawdsConfig sawds{};
  AdaptivePrefillConfig adaptive_prefill{};
};

struct ClassRuntimeMetrics {
  std::uint64_t admitted{};
  std::uint64_t rejected{};
  std::uint64_t rejected_invalid{};
  std::uint64_t rejected_kv_capacity{};
  std::uint64_t rejected_sequence_capacity{};
  std::uint64_t rejected_runtime_state{};
  std::uint64_t resource_deferred{};
  std::uint64_t resource_unblocked{};
  std::uint64_t completed{};
  std::uint64_t slo_met{};
  std::uint64_t service_tokens{};
  std::uint64_t maximum_ready_wait_ms{};
  std::uint32_t scheduler_wait_p99_ms{};
  std::array<std::uint64_t, 32> scheduler_wait_histogram{};
  std::uint64_t starvation_events{};
  std::uint64_t hard_limit_aborted{};
};

struct RuntimeMetricsSnapshot {
  std::array<ClassRuntimeMetrics, 2> by_class{};
  SchedulerMode scheduler{SchedulerMode::FIFO};
  bool adaptive_prefill_enabled{};
  std::uint32_t prefill_epoch_budget{};
  AdaptivePrefillReason adaptive_prefill_reason{AdaptivePrefillReason::StaticFallback};
  std::uint32_t eligible_requests{};
  std::uint32_t selected_requests{};
  std::uint32_t last_batch_tokens{};
  std::uint32_t last_batch_sequences{};
  std::uint32_t last_batch_prefill_tokens{};
  std::uint32_t last_batch_decode_tokens{};
  std::uint64_t last_scheduler_epoch_interval_ns{};
  std::uint64_t last_scheduler_epoch_duration_ns{};
  std::uint64_t last_backend_duration_ns{};
  std::uint64_t total_backend_duration_ns{};
  std::uint64_t total_prefill_tokens_issued{};
  std::uint64_t total_decode_tokens_issued{};
  std::uint64_t event_queue_rejections{};
  std::vector<AdaptivePrefillTraceEntry> adaptive_prefill_trace;
  std::uint64_t completed_batches{};
  std::uint64_t rejected_kv_reservations{};
  std::uint64_t hard_usable_kv_tokens{};
  std::uint64_t available_kv_tokens{};
  std::uint32_t available_sequences{};
  bool admission_ready{};
  std::uint64_t charged_kv_tokens{};
  std::uint64_t committed_kv_tokens{};
  std::uint64_t soft_reserved_kv_tokens{};
  std::uint64_t authorized_kv_tokens{};
  std::uint64_t peak_charged_kv_tokens{};
  std::uint64_t released_kv_tokens{};
  KvPressureState pressure{KvPressureState::Normal};
};

class RuntimeReactor {
 public:
  RuntimeReactor(const Clock& clock, std::uint32_t max_sequences,
                 std::size_t event_capacity = 256,
                 std::uint32_t context_limit = 32768,
                 std::uint32_t max_output_piece_bytes = 11,
                 RuntimeConfig config = {});

  bool post(RuntimeEvent event);
  bool post_completion(RuntimeEvent event);
  bool run_one();
  bool wait_one_for(std::chrono::milliseconds timeout);
  std::size_t run_ready();
  void seal_events();
  std::size_t expire_deadlines();

  WorkPlan make_fifo_plan(std::uint32_t max_tokens, std::uint32_t max_sequences,
                          std::uint32_t max_prefill_chunk,
                          std::uint32_t max_prefill_tokens_per_epoch = UINT32_MAX);
  WorkPlan make_sawds_plan(std::uint32_t max_tokens, std::uint32_t max_sequences,
                           std::uint32_t max_prefill_chunk,
                           std::uint32_t max_prefill_tokens_per_epoch = 0);
  WorkPlan make_configured_plan(std::uint32_t max_tokens, std::uint32_t max_sequences,
                                std::uint32_t max_prefill_chunk);
  bool resolve_resource_stall();
  std::optional<BatchTicket> issue(const WorkPlan& plan);
  bool cancel(RequestKey key);
  void begin_shutdown();
  void fail_all();

  std::optional<RequestKey> latest_key(std::uint64_t id) const;
  std::optional<RequestSnapshot> snapshot(RequestKey key) const;
  std::shared_ptr<StreamBuffer> stream_for(RequestKey key) const;
  std::vector<ClearRequest> take_clear_requests();
  std::uint64_t stale_event_count() const { assert_owner(); return stale_events_; }
  std::uint64_t estimated_committed_tokens() const { assert_owner(); return kv_budget_.committed(); }
  std::uint32_t live_sequences() const { assert_owner(); return allocator_.live_sequences(); }
  std::size_t queued_request_count() const { assert_owner(); return fifo_.size(); }
  std::uint64_t inflight_ticket_count() const;
  bool shutting_down() const { assert_owner(); return shutting_down_; }
  bool fatal_error() const { assert_owner(); return fatal_error_; }
  RuntimeMetricsSnapshot metrics() const;
  std::uint32_t prefill_epoch_budget() const;
  std::vector<AdaptivePrefillTraceEntry> adaptive_prefill_trace() const;
  KvPressureState pressure_state() const { assert_owner(); return kv_budget_.pressure_state(); }
  const RuntimeConfig& config() const noexcept { return config_; }
  bool shutdown_complete() const;

 private:
  struct RequestRecord {
    RequestSpec spec;
    RequestKey key;
    RequestState state{RequestState::Received};
    SequenceId sequence{};
    KvLease kv_lease;
    RequestClass service_class{RequestClass::Interactive};
    ClockTime arrived_at{};
    ClockTime ready_since{};
    std::uint64_t arrival_order{};
    std::optional<ClockTime> first_scheduled_at;
    std::optional<ClockTime> first_output_at;
    bool starvation_recorded{false};
    std::uint32_t prefill_cursor{};
    std::uint32_t generated_tokens{};
    std::uint32_t inflight_tickets{};
    std::size_t reserved_output_bytes{};
    bool backpressured{false};
    bool resource_blocked{false};
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
  void reclaim_excess_soft_reservations();
  void observe_adaptive_prefill(const BatchCompletedEvent& event,
                                std::uint64_t batch_id,
                                std::optional<std::uint64_t> interactive_ttft_ms,
                                std::uint32_t prefill_tokens,
                                std::uint32_t decode_tokens,
                                ClockTime completed_at);
  std::vector<SchedulerCandidate> collect_scheduler_candidates(
      std::uint32_t max_prefill_chunk,
      std::uint32_t max_prefill_tokens_per_epoch);
  void limit_plan_to_hard_growth(WorkPlan& plan,
                                 const std::vector<SchedulerCandidate>& candidates) const;
  std::uint32_t service_class_index(RequestClass service_class) const;
  static KvBudgetConfig make_kv_config(const RuntimeConfig& config,
                                       std::uint32_t max_sequences,
                                       std::uint32_t context_limit);

  struct PendingSchedulerCommit {
    WorkPlan plan;
    std::array<std::uint64_t, 2> credits_after{};
  };

  const Clock& clock_;
  BoundedQueue<RuntimeEvent> events_;
  SequenceAllocator allocator_;
  const std::uint32_t context_limit_;
  const std::uint32_t max_sequences_;
  const std::uint32_t max_output_piece_bytes_;
  const RuntimeConfig config_;
  KvBudgetManager kv_budget_;
  std::unordered_map<RequestKey, RequestRecord, RequestKeyHash> requests_;
  std::unordered_map<std::uint64_t, std::uint32_t> last_generation_;
  std::unordered_map<std::uint64_t, std::vector<TicketEntry>> issued_tickets_;
  std::deque<RequestKey> fifo_;
  std::vector<ClearRequest> clear_requests_;
  std::array<std::uint64_t, 2> scheduler_credits_{};
  std::optional<PendingSchedulerCommit> pending_scheduler_commit_;
  std::array<ClassRuntimeMetrics, 2> class_metrics_{};
  AdaptivePrefillReason adaptive_prefill_reason_{AdaptivePrefillReason::StaticFallback};
  std::uint32_t eligible_requests_{};
  std::uint32_t selected_requests_{};
  std::uint32_t last_batch_tokens_{};
  std::uint32_t last_batch_sequences_{};
  std::uint32_t last_batch_prefill_tokens_{};
  std::uint32_t last_batch_decode_tokens_{};
  std::uint64_t last_scheduler_epoch_interval_ns_{};
  std::uint64_t last_scheduler_epoch_duration_ns_{};
  std::uint64_t last_backend_duration_ns_{};
  std::uint64_t total_backend_duration_ns_{};
  std::uint64_t total_prefill_tokens_issued_{};
  std::uint64_t total_decode_tokens_issued_{};
  std::optional<ClockTime> previous_issue_time_;
  std::uint32_t prefill_epoch_budget_{};
  std::uint64_t adaptive_ewma_ttft_ms_{};
  bool has_adaptive_ewma_{false};
  std::uint32_t high_pressure_windows_{};
  std::uint32_t low_pressure_windows_{};
  std::uint32_t adaptive_cooldown_remaining_{};
  std::optional<ClockTime> previous_batch_completion_;
  std::deque<AdaptivePrefillTraceEntry> adaptive_trace_;
  mutable std::thread::id owner_{};
  std::uint64_t next_arrival_order_{1};
  std::uint64_t next_batch_id_{1};
  std::uint64_t stale_events_{};
  std::uint64_t completed_batches_{};
  bool shutting_down_{false};
  bool fatal_error_{false};
};

}  // namespace inferscale
