# InferScale — System Architecture

## 1. Ownership boundary

InferScale links to `libllama` as an execution backend. It does not run requests through `llama-server` internally.

```text
+-------------------- InferScale owns ---------------------+
|                                                         |
| HTTP/SSE -> Runtime Reactor -> Admission -> Scheduler    |
|               |               |             |            |
|               +-> lifecycle   +-> KV budget +-> WorkPlan|
|               +-> deadline                  |            |
|               +-> cancel                    v            |
|                                      Batch Builder       |
|                                            |             |
|                                      Engine Executor     |
+--------------------------------------------+-------------+
                                             |
                                             v
                                  +---------------------+
                                  | direct libllama API |
                                  | model + memory      |
                                  +----------+----------+
                                             |
                                          ggml/CUDA
                                             |
                                            GPU
```

InferScale owns **policy and lifecycle**. `libllama` owns model/tensor execution and the actual backend memory implementation.

## 2. Top-level runtime

```text
Client / Benchmark
       |
       v
+----------------------+      bounded output queues
| Async HTTP/SSE       |<-----------------------------+
| parse/auth/limits    |                              |
+----------+-----------+                              |
           | RuntimeEvent                             |
           v                                          |
+--------------------------------------------------+  |
| Single-Owner Runtime Reactor                    |  |
|                                                  |  |
| Request table + generations                     |  |
| lifecycle/deadlines/cancel                      |  |
| AdmissionController                             |  |
| KvBudgetManager                                 |  |
| AdaptiveScheduler / FIFO policy                 |  |
| AdaptivePrefillController                       |  |
+---------------------+----------------------------+  |
                      | WorkPlan                      |
                      v                               |
              +---------------+                      |
              | BatchBuilder  |                      |
              +-------+-------+                      |
                      | EngineBatch                  |
                      v                               |
              +---------------+                      |
              | Executor      |                      |
              | BatchTicket   |                      |
              +-------+-------+                      |
                      |                               |
                      v                               |
                 libllama/GPU                        |
                      |                               |
               BatchCompleted                       |
                      +-------------------------------+

All components -> structured metrics, traces, benchmark event log
```

## 3. Threading and ownership model

### Network I/O threads
Own sockets and connection-local buffers only. They parse bounded requests and post immutable runtime events such as:

```text
NewRequest
ClientDisconnected
StreamWritable
StreamClosed
ShutdownRequested
```

They do **not** mutate scheduler queues or request lifecycle state.

### Runtime reactor thread
The only writer for:

- request state;
- admission state;
- scheduler queues;
- KV reservations/leases;
- deadline transitions;
- cancellation intent;
- terminal cleanup eligibility.

This design intentionally prefers determinism and inspectability over a highly concurrent shared-state control plane.

### Engine executor thread
Owns the backend execution context if required by the chosen integration. It receives immutable `EngineBatch` objects plus a `BatchTicket`, invokes `libllama`, and posts completion/failure events back to the reactor.

No request resource can be reclaimed until every `BatchTicket` referencing `(RequestId, Generation)` has retired.

## 4. Request identity and generation fencing

`RequestId` alone is insufficient because IDs or backend sequence slots may be reused.

Each live request has:

```text
RequestKey = { request_id, generation }
```

Every queued work item and batch ticket carries the full key.

If a stale executor completion arrives for `42:g7` after the runtime now owns `42:g8`, the event is rejected as stale and counted. This makes sequence reuse and cancellation behavior testable.

## 5. Request lifecycle

```text
RECEIVED
  |
  v
VALIDATED
  |
  v
TOKENIZED
  |
  v
WAITING_ADMISSION --------------------> REJECTED
  |                                        overload / impossible
  v
ADMITTED
  |
  v
PREFILL_READY
  |
  | chunk 0..N
  v
PREFILL_IN_PROGRESS -------------------+
  |                                    |
  +--> PREFILL_READY (more prompt)     |
  |                                    |
  v                                    |
DECODE_READY <-------------------------+
  |
  v
DECODE_IN_PROGRESS
  |
  +--> DECODE_READY (next token)
  |
  +--> COMPLETE
  |
  +--> CANCEL_REQUESTED
  |
  +--> DEADLINE_EXPIRED
  |
  +--> FAILED

Any terminal intent that still has in-flight tickets:
  -> RETIRING
  -> RELEASE_RESOURCES exactly once
  -> TERMINAL
```

Cancellation is therefore a **two-stage concept**: stop future scheduling immediately at a safe reactor point, then reclaim backend/KV state only after the last referencing execution ticket retires.

## 6. Scheduling unit: `WorkItem`, not request ID

A scheduler plan identifies exact work:

```cpp
struct WorkItem {
    RequestKey key;
    WorkKind kind;              // Prefill or Decode
    uint32_t token_begin;       // prompt offset for prefill
    uint32_t token_count;       // bounded chunk; decode normally 1
    uint32_t expected_kv_growth;
    uint32_t priority_class;
};
```

This makes prefill budgeting enforceable.

Example:

```text
prompt length = 8192
prefill_chunk_tokens = 512

Epoch 1: A[0..511]
Epoch 2: A[512..1023]
...
```

Other decode requests can be inserted between chunks.

## 7. Adaptive scheduling model

The production policy is **SLO-aware weighted deficit scheduling with aging**, implemented as a deterministic policy rather than one opaque floating-point score.

Each scheduling epoch:

1. expire requests whose hard deadline has passed;
2. add service credits to traffic classes according to configured weights;
3. mark requests that exceed the starvation-warning age as urgent;
4. reserve a configurable minimum decode service share when decode work exists;
5. schedule eligible decode work using class credit, age, deadline slack, and stable arrival order;
6. spend remaining token budget on bounded prefill chunks;
7. never schedule work whose hard KV growth check cannot succeed;
8. fill only to engine sequence/token limits.

Memory pressure is an **eligibility constraint**, not a magic priority bonus.

### Suggested traffic classes

```text
interactive: higher service weight, stricter TTFT SLO
batch:       lower service weight, explicit anti-starvation bound
```

Class weights influence service over time; aging prevents indefinite starvation.

## 8. Adaptive prefill-budget controller

The scheduler remains correct with fixed limits, but the production policy may feed a **bounded controller** that adjusts only `max_prefill_tokens_per_epoch` inside configured `[min,max]` limits. Safety limits, fairness weights, and hard KV capacity are never dynamically relaxed.

Suggested control signals:

```text
EWMA interactive scheduler wait / TTFT pressure
executor/GPU idle-headroom estimate
ready decode backlog
oldest batch-class wait
```

Example hysteretic policy:

```text
if SLO pressure > high_threshold for N control windows:
    prefill_budget = max(min_budget, prefill_budget - step)
else if SLO pressure < low_threshold
        and idle_headroom > threshold
        and batch starvation is healthy
        for N windows:
    prefill_budget = min(max_budget, prefill_budget + step)
else:
    hold
```

Changes occur at coarse control windows, not every token. A cooldown/hysteresis band prevents oscillation. Controller state and each budget change are traced. Static-budget mode is always available as a control and safe fallback.

## 9. Prefill/decode budgets

A scheduling epoch has independent constraints:

```text
max_batch_tokens
max_batch_sequences
max_prefill_tokens_per_epoch
min_decode_share_when_ready
max_prefill_chunk_tokens
```

This lets InferScale independently tune throughput and interference.

The default correctness principle is:

> No single request can inject an arbitrarily large prefill into one epoch.

## 10. KV resource architecture

Resume-grade v1 uses a **KV Budget Manager**, not a fake physical page allocator.

```text
Backend actual sequence memory (owned by libllama)
                 ^
                 |
       measured/reconciled state
                 |
+----------------------------------+
| InferScale KvBudgetManager       |
|                                  |
| hard_capacity_tokens/bytes       |
| safety_reserve                   |
| per-request soft reservations    |
| per-request committed growth     |
| high-water / pressure state      |
+----------------------------------+
```

### Hard capacity
Never intentionally exceed the configured safe backend capacity.

Before every scheduled growth:

```text
committed + work_item.expected_kv_growth <= hard_usable_capacity
```

If not, work cannot run regardless of priority.

### Soft reservation
Admission uses:

```text
prompt_tokens
+ generation_reserve_policy(max_new_tokens, class, observed history)
+ safety margin
```

Soft reservation may be conservative and can be adjusted/reclaimed as actual generation progresses.

### Reconciliation
Backend sequence position/memory information is sampled at safe points where supported. Mismatch between logical accounting and backend-observed state is an invariant violation or diagnostic event, never silently ignored.

## 11. Admission control

A request is classified as:

```text
ADMIT
DEFER
REJECT_OVERLOAD
REJECT_IMPOSSIBLE
REJECT_DEADLINE
```

Admission checks:

- request validity and context bound;
- queue bound;
- hard feasibility of at least prompt + minimum decode progress;
- soft KV reservation budget;
- class-specific queue/SLO pressure;
- configured overload policy.

A high-priority request may bypass **soft queue policy** if configured, but can never bypass context limits, hard KV capacity, or invariant checks.

## 12. Backpressure

### Ingress
When waiting capacity is exhausted, the server rejects quickly with a documented overload status rather than allowing unbounded memory growth.

### Streaming egress
Each connection has a bounded output buffer. If the client is slow:

- generated output is not allowed to grow without bound;
- the request can enter `STREAM_BACKPRESSURED` state;
- scheduler eligibility can be temporarily suppressed;
- a configured timeout can cancel chronically stalled clients.

This keeps network behavior from turning into hidden memory pressure.

## 13. Cancellation and deadlines

Cancellation/deadline handling uses runtime events and generation fencing:

```text
Disconnect/Cancel
     |
     v
reactor sets CANCEL_REQUESTED
     |
     +--> remove/disable future work
     +--> discard future unsent tokens
     +--> wait for outstanding BatchTicket(s)
                          |
                          v
                    ticket retired
                          |
                          v
               release sequence + KV lease
                          |
                          v
                      CANCELLED
```

Metrics record:

- cancel-to-unscheduled latency;
- cancel-to-release latency;
- post-cancel tokens executed;
- stale completion events;
- duplicate cleanup attempts.

## 14. Failure taxonomy

Every engine/API error maps to one of four classes:

### RequestRecoverable
Malformed request, invalid sampling parameters, context-limit violation. Fail one request.

### EngineRecoverable
A batch could not find a safe backend slot or a retryable engine condition occurred. Requeue or fail affected requests according to a bounded retry policy.

### EngineRestartRequired
Backend state can no longer be trusted. Stop admission, fail/drain current work, recreate engine/context, then resume only if restart succeeds.

### ProcessFatal
Invariant corruption, unrecoverable CUDA/context failure, or engine restart failure. Emit crash evidence and terminate rather than serving from unknown state.

## 15. Graceful shutdown

`SIGTERM` / shutdown request triggers:

```text
stop new admission
-> stop accepting new sockets
-> mark waiting work cancelled or drain according to config
-> wait bounded time for in-flight execution
-> close streams
-> release backend resources
-> flush metrics/event logs
-> clean exit
```

A hard shutdown deadline prevents hanging indefinitely.

## 16. Observability

Each request receives trace/event records for:

```text
arrival
validation/tokenization
admission decision + reason
queue entry/exit
scheduler epochs considered
prefill chunk start/end
first decode eligibility
batch ticket issue/retire
first token
stream stalls
cancel/deadline
terminal state
KV reserved/committed peak
```

Global metrics include:

- queue depth by class;
- offered/admitted/rejected rate;
- batch sequence/token occupancy;
- prefill/decode token share;
- scheduler build latency;
- executor idle gaps;
- engine error classes;
- KV pressure and high-water;
- goodput and SLO miss counters.

## 17. Benchmark architecture

Use two different baselines for different questions:

```text
A. pinned llama-server
   -> external mature serving reference

B. InferScale runtime + FIFO
   -> same-runtime scientific control

C. InferScale adaptive with static-safe prefill budget
   -> scheduler/chunking treatment

D. C + bounded adaptive prefill-budget feedback
   -> controller treatment

E. best validated adaptive variant + one optimization
   -> isolated optimization ablation
```

Do not infer scheduler causality from A vs C alone.

## 18. Extension boundary

Only after v1 passes all gates may the project add:

- physical paged KV / PagedAttention;
- prefix block sharing + copy-on-write;
- speculative decoding;
- multi-GPU routing;
- distributed workers.

Those features must not destabilize the v1 evidence story.
