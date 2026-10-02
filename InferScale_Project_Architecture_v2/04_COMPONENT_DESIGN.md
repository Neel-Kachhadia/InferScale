# InferScale — Detailed Component Design

## 1. `RequestKey` and generation safety

```cpp
struct RequestKey {
    uint64_t id;
    uint32_t generation;

    auto operator<=>(const RequestKey&) const = default;
};
```

Every asynchronous callback, queued work item, batch ticket, stream event, and cleanup request carries `RequestKey`, not a bare ID.

### Invariant
No completion/event for generation `g` may mutate a request slot currently assigned to another generation.

---

## 2. `RequestContext`

Conceptual state:

```cpp
struct RequestContext {
    RequestKey key;
    RequestState state;
    PriorityClass priority;

    TimePoint arrival;
    std::optional<TimePoint> deadline;

    std::vector<llama_token> prompt_tokens;
    uint32_t prefill_cursor = 0;
    uint32_t generated_tokens = 0;
    uint32_t max_new_tokens = 0;

    KvLease kv;
    uint32_t inflight_tickets = 0;

    bool cancel_requested = false;
    bool stream_backpressured = false;

    RequestMetrics metrics;
};
```

The runtime reactor owns mutation. Network/executor threads communicate by event.

---

## 3. `RuntimeEvent`

Use a tagged variant or equivalent:

```text
NewRequest
TokenizationFinished
ClientDisconnected
ClientWritable
DeadlineTimerFired
BatchCompleted
BatchFailed
ShutdownRequested
```

Events contain immutable payloads and request generations where relevant.

### Why this matters
A small event-driven control plane reduces lock complexity and makes deterministic replay/failure tests possible.

---

## 4. `AdmissionController`

### Inputs

```text
queue occupancy
request prompt tokens
max_new_tokens
priority class
optional deadline/SLO
total hard KV capacity
soft reservations
committed KV usage
safety reserve
engine sequence limit
```

### Output

```cpp
enum class AdmissionDecision {
    Admit,
    Defer,
    RejectOverload,
    RejectImpossible,
    RejectDeadline
};
```

### Hard feasibility
The request must satisfy context/model constraints and be capable of making minimum forward progress without violating hard resource capacity.

### Soft reservation
A configurable policy estimates:

```text
soft_reserve = prompt_remaining
             + generation_reserve(max_new_tokens, priority, history)
             + safety_margin
```

Soft reservation controls over-admission; hard growth checks remain authoritative later.

### No unsafe priority override
Priority may alter defer/reject policy under *soft* pressure but never bypass hard capacity.

---

## 5. `KvBudgetManager`

This component owns **logical resource accounting**, not physical backend pages.

```cpp
struct KvLease {
    RequestKey owner;
    uint32_t reserved_tokens;
    uint32_t committed_tokens;
    uint32_t peak_tokens;
};

class KvBudgetManager {
public:
    Result<KvLease> reserve(RequestKey, uint32_t soft_tokens);
    Result<void> authorize_growth(RequestKey, uint32_t tokens);
    void commit_growth(RequestKey, uint32_t tokens);
    void reconcile(RequestKey, BackendSequenceState);
    Result<void> release(RequestKey);
    KvBudgetStats stats() const;
};
```

### Core invariants

```text
0 <= committed <= hard_usable_capacity
sum(committed_by_request) == committed_global
lease released at most once
no growth after terminal-retiring state
stale generation cannot grow/release current lease
```

### Pressure states

```text
NORMAL
SOFT_PRESSURE
HIGH_PRESSURE
HARD_LIMIT
```

These pressure states may affect admission and scheduler eligibility but cannot redefine hard capacity.

---

## 6. `WorkItem`

```cpp
enum class WorkKind { Prefill, Decode };

struct WorkItem {
    RequestKey key;
    WorkKind kind;
    uint32_t token_begin;
    uint32_t token_count;
    uint32_t expected_kv_growth;
};
```

For prefill, `token_count <= max_prefill_chunk_tokens`.
For normal autoregressive decode, `token_count` is normally one token worth of new sequence growth per request per step.

---

## 7. Scheduler interface

```cpp
struct SchedulerSnapshot {
    std::span<const RequestView> ready;
    KvBudgetStats kv;
    EngineCapabilities engine;
    TimePoint now;
    SchedulerConfig config;
};

struct WorkPlan {
    std::vector<WorkItem> items;
    uint32_t total_tokens = 0;
    uint32_t sequence_count = 0;
    ScheduleReasonSummary reasons;
};

class SchedulerPolicy {
public:
    virtual WorkPlan plan(const SchedulerSnapshot&) = 0;
    virtual void on_work_committed(const WorkPlan&) = 0;
    virtual ~SchedulerPolicy() = default;
};
```

`FIFO` and adaptive policy use the same interface.

---

## 8. Adaptive scheduler: SA-WDS

Use **SLO-Aware Weighted Deficit Scheduling** as the named production policy.

The design avoids a fragile single formula by separating eligibility, urgency, and fairness.

### Step A — eligibility
A request is eligible only if:

- lifecycle state permits work;
- it is not cancelled/expired/backpressured;
- engine sequence/token limits permit inclusion;
- `KvBudgetManager::authorize_growth()` would succeed.

### Step B — class credit
Each traffic class receives deficit/service credits each epoch:

```text
interactive += interactive_weight
batch       += batch_weight
```

Scheduled work spends credit proportional to token/work cost. Credit can accumulate to a configured cap so a temporarily idle class can regain service without unbounded bursts.

### Step C — urgency
Within a class, stable ordering considers:

1. starvation threshold breach;
2. deadline slack bucket, if a deadline exists;
3. aging bucket;
4. arrival order / request ID for determinism.

Aging is discrete and bounded, e.g.:

```text
age_bucket = min(max_age_bucket,
                 wait_ms / aging_quantum_ms)
```

### Step D — decode protection
If decode work exists, reserve at least `min_decode_token_share` of the epoch token budget unless engine constraints make that impossible.

### Step E — chunked prefill
Use remaining budget for prefill chunks, each bounded by `max_prefill_chunk_tokens`.

### Fairness target
Under continuous eligibility and a functioning executor, no admitted non-cancelled request should remain unscheduled beyond the configured starvation bound without emitting a test-failing starvation event.

This is an operational bound, not a formal real-time guarantee.

---

## 9. `AdaptivePrefillController`

A small feedback controller gives InferScale a concrete adaptive mechanism without making correctness depend on an unstable learned policy.

```cpp
struct ControllerSignals {
    double interactive_wait_ewma_ms;
    double interactive_ttft_ewma_ms;
    double executor_idle_fraction;
    uint32_t decode_ready;
    double oldest_batch_wait_ms;
};

class AdaptivePrefillController {
public:
    uint32_t update(const ControllerSignals&, TimePoint now);
    uint32_t current_budget() const;
};
```

### Safety boundary
The controller may change only the prefill token budget inside configured bounds. It cannot change hard KV capacity, request context limits, or bypass admission.

### Stability controls

```text
EWMA smoothing
high/low hysteresis thresholds
minimum control-window duration
step-size limit
cooldown after change
static fallback mode
```

Every budget change emits a trace event with old/new value and trigger signals. Tests use `ManualClock` and scripted signals to prove bounds, hysteresis, and recovery.

---

## 10. `BatchBuilder`

Transforms `WorkPlan` into backend-specific sequence-aware batch input.

Responsibilities:

- validate each request generation;
- map request key to backend sequence ID;
- add exact prefill/decode token positions;
- ensure batch token/sequence limits;
- attach output/logit requirements;
- build an immutable `BatchTicket` ownership list;
- perform no scheduling decisions of its own.

### BatchBuilder invariant
The builder may reject an invalid/stale plan, but must never silently change scheduling policy.

---

## 11. `BatchTicket`

```cpp
struct TicketEntry {
    RequestKey key;
    WorkKind kind;
    uint32_t expected_kv_growth;
};

class BatchTicket {
public:
    uint64_t batch_id() const;
    std::span<const TicketEntry> entries() const;
    // move-only; retirement handled exactly once
};
```

When issued, the reactor increments each request's in-flight count. On completion/failure, retirement decrements it exactly once.

Terminal resources are reclaimable only when:

```text
terminal_intent == true && inflight_tickets == 0
```

---

## 12. `InferenceEngine`

```cpp
struct EngineCapabilities {
    uint32_t max_sequences;
    uint32_t max_batch_tokens;
    uint32_t context_limit;
    bool cuda_enabled;
};

class InferenceEngine {
public:
    virtual EngineCapabilities capabilities() const = 0;
    virtual EngineResult execute(const EngineBatch&) = 0;
    virtual BackendMemoryStats memory_stats() const = 0;
    virtual BackendSequenceState sequence_state(SequenceId) const = 0;
    virtual Result<void> clear_sequence(SequenceId) = 0;
    virtual ~InferenceEngine() = default;
};
```

### `LlamaEngine`
Direct adapter over the pinned public llama.cpp API. It should be thin enough that scheduling policy remains outside the backend adapter.

### `FakeEngine`
Deterministic CPU test double that can:

- pause at barriers;
- inject recoverable/fatal errors;
- simulate variable prefill/decode duration;
- simulate hard capacity exhaustion;
- return deterministic tokens;
- expose exact in-flight references for assertions.

---

## 13. Backend sequence allocator

A dedicated sequence-slot allocator maps live `RequestKey` values to backend sequence IDs.

Required safety:

```text
sequence_id cannot be reused
until all BatchTickets for the prior generation retire
and backend clear/release succeeds
```

Generation fencing must exist even if the backend sequence ID itself is unique enough in normal runs.

---

## 14. `StreamController`

Per connection/request:

- bounded pending-byte/token buffer;
- writable notification to reactor;
- slow-reader timeout;
- terminal flush semantics;
- disconnect -> cancellation event.

The scheduler must be able to suppress new decode work for a stream whose bounded buffer is full.

---

## 15. `ErrorClassifier`

Maps backend/runtime errors to:

```cpp
enum class FailureClass {
    RequestRecoverable,
    EngineRecoverable,
    EngineRestartRequired,
    ProcessFatal
};
```

The mapping must be documented and unit tested. Unknown errors default conservatively rather than being treated as harmless.

---

## 16. `MetricsRecorder`

### Online metrics
Low-cardinality aggregates only.

Examples:

```text
requests_total{result=...}
queue_depth{class=...}
kv_committed_tokens
kv_reserved_tokens
batch_tokens
batch_sequences
scheduler_epoch_us
engine_errors_total{class=...}
starvation_events_total{class=...}
stream_backpressure_total
```

### Benchmark event log
Per-request details written asynchronously to a bounded recorder:

```text
request key
workload sample ID
all lifecycle timestamps
prefill chunk count
decode count
scheduler wait
first token
completion
KV peak
cancel metrics
failure class
```

If benchmark logging cannot keep up, count dropped events and invalidate any run that exceeds the configured loss threshold.

---

## 17. `Clock` abstraction

Scheduler/lifecycle tests should not sleep in wall-clock time.

```cpp
class Clock {
public:
    virtual TimePoint now() const = 0;
};
```

Use `ManualClock` in deterministic tests to advance deadlines/aging instantly.

---

## 18. `RuntimeReactor`

Conceptual loop:

```text
while running:
    drain bounded control events
    process deadlines
    apply cancellations
    retire completed batch tickets
    reclaim terminal resources
    attempt admissions
    if executor idle:
        snapshot ready state
        ask scheduler for WorkPlan
        authorize KV growth
        build batch
        issue BatchTicket
        submit to executor
    flush ready stream output
    publish bounded metrics
```

The exact implementation may be event-driven rather than polling, but the state-machine ordering must be explicit.

---

## 19. Shutdown coordinator

Shutdown has a bounded deadline and phases:

```text
QUIESCE -> DRAIN/CANCEL -> RETIRE -> RELEASE -> STOP
```

Tests inject shutdown during:

- waiting admission;
- prefill;
- decode;
- stream backpressure;
- engine restart;
- benchmark logging.

---

## 20. Configuration surface

Representative config:

```yaml
server:
  port: 8080
  max_waiting: 256
  max_request_bytes: 1048576
  max_stream_buffer_bytes: 262144
  slow_reader_timeout_ms: 15000

scheduler:
  policy: sa_wds
  epoch_max_tokens: 2048
  max_sequences: 16
  max_prefill_chunk_tokens: 512
  max_prefill_tokens_per_epoch: 1024
  min_prefill_tokens_per_epoch: 256
  max_prefill_tokens_controller: 1536
  controller_window_ms: 500
  controller_step_tokens: 128
  controller_high_pressure: 1.10
  controller_low_pressure: 0.80
  controller_cooldown_ms: 1000
  min_decode_token_share: 0.35
  aging_quantum_ms: 100
  starvation_limit_ms: 5000
  interactive_weight: 4
  batch_weight: 1

kv:
  hard_usable_tokens: auto_calibrated
  safety_reserve_percent: 8
  min_generation_reserve: 64

engine:
  model: /models/model.gguf
  backend_commit: pinned
  context_tokens: 16384

runtime:
  engine_retry_limit: 1
  shutdown_grace_ms: 10000
```

Every benchmark release stores the resolved configuration, not just defaults.
