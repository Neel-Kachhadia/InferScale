# InferScale — Repository Structure and Interface Contracts

## Proposed repository

```text
InferScale/
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
├── SECURITY.md
├── .clang-format
├── .clang-tidy
├── .github/
│   └── workflows/
│       ├── build-test.yml
│       ├── sanitizers.yml
│       ├── fuzz-smoke.yml
│       └── dependency-scan.yml
├── cmake/
│   ├── Dependencies.cmake
│   └── Warnings.cmake
├── include/inferscale/
│   ├── api/
│   │   ├── server.h
│   │   └── stream_controller.h
│   ├── runtime/
│   │   ├── event.h
│   │   ├── request.h
│   │   ├── request_key.h
│   │   ├── lifecycle.h
│   │   ├── runtime_reactor.h
│   │   └── shutdown.h
│   ├── admission/
│   │   └── admission_controller.h
│   ├── scheduler/
│   │   ├── work_item.h
│   │   ├── policy.h
│   │   ├── fifo.h
│   │   ├── sa_wds.h
│   │   └── adaptive_prefill_controller.h
│   ├── memory/
│   │   ├── kv_budget.h
│   │   └── kv_lease.h
│   ├── execution/
│   │   ├── batch_builder.h
│   │   ├── batch_ticket.h
│   │   └── executor.h
│   ├── engine/
│   │   ├── engine.h
│   │   ├── llama_engine.h
│   │   ├── sequence_allocator.h
│   │   └── error_classifier.h
│   ├── metrics/
│   │   ├── metrics.h
│   │   └── event_recorder.h
│   ├── config/
│   │   └── config.h
│   └── util/
│       ├── bounded_queue.h
│       ├── clock.h
│       └── result.h
├── src/
│   ├── api/
│   ├── runtime/
│   ├── admission/
│   ├── scheduler/
│   ├── memory/
│   ├── execution/
│   ├── engine/
│   ├── metrics/
│   ├── config/
│   └── main.cc
├── cuda/
│   ├── CMakeLists.txt
│   ├── kernels.cuh
│   ├── kernels.cu
│   └── reference/
├── tests/
│   ├── unit/
│   │   ├── lifecycle_test.cc
│   │   ├── admission_test.cc
│   │   ├── kv_budget_test.cc
│   │   ├── fifo_test.cc
│   │   ├── sa_wds_test.cc
│   │   ├── adaptive_prefill_controller_test.cc
│   │   ├── batch_ticket_test.cc
│   │   └── error_classifier_test.cc
│   ├── model/
│   │   └── runtime_model_test.cc
│   ├── integration/
│   │   ├── fake_engine.h
│   │   ├── cancellation_test.cc
│   │   ├── stale_generation_test.cc
│   │   ├── slow_reader_test.cc
│   │   ├── restart_test.cc
│   │   └── shutdown_test.cc
│   ├── fuzz/
│   │   ├── request_parser_fuzz.cc
│   │   └── runtime_event_fuzz.cc
│   └── gpu/
│       ├── smoke_test.cc
│       └── kernel_correctness_test.cc
├── bench/
│   ├── run.py
│   ├── analyze.py
│   ├── validate_run.py
│   ├── client.py
│   ├── stats.py
│   ├── schema/
│   ├── workloads/
│   │   ├── interactive_short.yaml
│   │   ├── long_context.yaml
│   │   ├── mixed_slo.yaml
│   │   ├── burst.yaml
│   │   ├── cancel.yaml
│   │   ├── memory_pressure.yaml
│   │   └── slow_reader.yaml
│   └── releases/
├── scripts/
│   ├── configure.sh
│   ├── build.sh
│   ├── test.sh
│   ├── sanitizers.sh
│   ├── profile.sh
│   ├── benchmark_matrix.sh
│   └── reproduce_release.sh
├── docs/
│   ├── ARCHITECTURE.md
│   ├── BASELINE.md
│   ├── SCHEDULING.md
│   ├── KV_ACCOUNTING.md
│   ├── FAILURE_MODEL.md
│   ├── BENCHMARK_METHODOLOGY.md
│   ├── BENCHMARKS.md
│   ├── LIMITATIONS.md
│   ├── ADR/
│   │   ├── ADR-001-direct-libllama.md
│   │   ├── ADR-002-single-owner-reactor.md
│   │   ├── ADR-003-logical-kv-v1.md
│   │   └── ADR-004-sa-wds.md
│   └── profiling/
└── third_party/
    └── README.md
```

## Interface contracts

### Request identity

```cpp
struct RequestKey {
    uint64_t id;
    uint32_t generation;
    auto operator<=>(const RequestKey&) const = default;
};
```

### Work item

```cpp
enum class WorkKind : uint8_t { Prefill, Decode };

struct WorkItem {
    RequestKey key;
    WorkKind kind;
    uint32_t token_begin;
    uint32_t token_count;
    uint32_t expected_kv_growth;
};
```

### Engine

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

### Scheduler

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
    uint32_t total_tokens;
    uint32_t sequence_count;
};

class SchedulerPolicy {
public:
    virtual WorkPlan plan(const SchedulerSnapshot&) = 0;
    virtual void on_work_committed(const WorkPlan&) = 0;
    virtual ~SchedulerPolicy() = default;
};
```

### KV budget

```cpp
struct KvLease {
    RequestKey owner;
    uint32_t reserved_tokens;
    uint32_t committed_tokens;
    uint32_t peak_tokens;
};

class KvBudgetManager {
public:
    virtual Result<KvLease> reserve(RequestKey, uint32_t soft_tokens) = 0;
    virtual Result<void> authorize_growth(RequestKey, uint32_t tokens) = 0;
    virtual void commit_growth(RequestKey, uint32_t tokens) = 0;
    virtual Result<void> release(RequestKey) = 0;
    virtual KvBudgetStats stats() const = 0;
    virtual ~KvBudgetManager() = default;
};
```

### Batch ticket

```cpp
struct TicketEntry {
    RequestKey key;
    WorkKind kind;
    uint32_t expected_kv_growth;
};

class BatchTicket {
public:
    BatchTicket(BatchTicket&&) noexcept;
    BatchTicket& operator=(BatchTicket&&) noexcept;
    BatchTicket(const BatchTicket&) = delete;
    BatchTicket& operator=(const BatchTicket&) = delete;

    uint64_t id() const;
    std::span<const TicketEntry> entries() const;
};
```

Move-only ownership helps make double retirement harder to express accidentally.

### Runtime events

```cpp
using RuntimeEvent = std::variant<
    NewRequestEvent,
    ClientDisconnectedEvent,
    StreamWritableEvent,
    DeadlineExpiredEvent,
    BatchCompletedEvent,
    BatchFailedEvent,
    ShutdownEvent>;
```

The runtime reactor is the only request-state writer.

## Configuration

One resolved configuration object is constructed at startup and dumped into every benchmark release.

Representative file:

```yaml
server:
  host: 127.0.0.1
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
  class_weights:
    interactive: 4
    batch: 1

kv:
  hard_usable_tokens: auto
  safety_reserve_percent: 8
  min_generation_reserve: 64

runtime:
  engine_retry_limit: 1
  shutdown_grace_ms: 10000

engine:
  model: /models/model.gguf
  context_tokens: 16384
```

Unknown config keys should be rejected or loudly warned according to a documented policy.

## Architecture decision records

The repository should include at least these ADRs:

```text
ADR-001: direct libllama rather than internal llama-server proxy
ADR-002: single-owner runtime reactor
ADR-003: logical KV budgeting for v1; physical paging deferred
ADR-004: SA-WDS scheduler and fairness model
```

ADRs make interview tradeoffs visible and prevent later code drift from rewriting project history.

## Commit/milestone strategy

Suggested milestones:

```text
milestone-1-baseline
milestone-2-safe-runtime
milestone-3-adaptive-scheduling
milestone-4-profiled-optimization
v1.0-resume-grade
```

Representative commits should describe verified behavior, not vague progress:

```text
bench: freeze coordinated-omission-safe baseline harness
feat: add generation-fenced request reactor
feat: retire batch tickets before sequence reuse
feat: add hard/soft KV budget enforcement
feat: add chunked prefill and SA-WDS policy
feat: add bounded adaptive prefill-budget feedback
perf: remove measured batch-build bottleneck
bench: publish reproducible v1 release evidence
```
