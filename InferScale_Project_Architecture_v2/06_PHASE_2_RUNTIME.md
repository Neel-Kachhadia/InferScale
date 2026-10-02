# Phase 2 — Single-Owner Runtime, FIFO Control, and Cancellation-Safe Streaming

## Goal
Build the correctness foundation of InferScale with the simplest policy possible: a bounded event-driven runtime, direct backend execution, FIFO scheduling, request generations, batch-ticket retirement, and streaming backpressure.

## Duration target
**5–8 focused days**.

## 1. Build the runtime reactor

Implement a single control-plane owner for:

```text
request table
lifecycle transitions
backend sequence-slot allocation
waiting/ready queues
deadline timers
cancellation intent
in-flight ticket counts
terminal cleanup
```

Network and executor code communicate through bounded event queues.

### Exit invariant
No HTTP/network callback may directly mutate scheduler or KV/request state.

## 2. Explicit state machine

Implement legal transitions centrally and reject illegal transitions in debug/test builds.

Required terminal-intent states include:

```text
COMPLETE_PENDING_RETIRE
CANCEL_PENDING_RETIRE
FAILED_PENDING_RETIRE
```

Cleanup is permitted only after `inflight_tickets == 0`.

## 3. Request generations

Every request slot/ID has a monotonically changing generation. All asynchronous events carry `(id,generation)`.

Test stale completion explicitly:

```text
request 42:g7 submits batch
42:g7 cancels and later retires
slot 42 reused as g8
late completion for 42:g7 arrives
=> ignored/counted; g8 unchanged
```

## 4. FIFO scheduler control

Implement the simplest deterministic policy first:

- stable FIFO admission/ready order;
- no priority optimization;
- no adaptive aging;
- prefill may initially run as the smallest correctness-preserving chunk supported by the engine integration, but adaptive chunk budgeting belongs to Phase 3.

The key output is already a `WorkPlan` of exact work items, not request IDs.

## 5. Batch builder and executor

Build:

```text
WorkPlan
  -> validate RequestKey generations
  -> allocate/lookup backend sequence IDs
  -> build backend batch
  -> issue move-only BatchTicket
  -> execute on engine thread
  -> post BatchCompleted/BatchFailed
  -> retire ticket in reactor
```

All backend sequence reuse is fenced by ticket retirement.

## 6. Minimal KV accounting

Phase 2 may begin with conservative capacity accounting sufficient to prevent obviously impossible work. Full soft reservations/pressure policy arrive in Phase 3.

Track at minimum:

```text
estimated committed tokens per live request
total committed estimate
engine sequence count
backend memory snapshot
```

Do not call this physical paging.

## 7. Cancellation

Cancellation sources:

- explicit API/client cancellation if exposed;
- HTTP disconnect;
- deadline expiration;
- shutdown.

Required semantics:

1. reactor marks terminal intent;
2. request becomes ineligible for future work;
3. queued work is removed/ignored;
4. in-flight work is allowed to reach a safe retirement point;
5. output from cancelled generations is discarded;
6. backend sequence/resources release exactly once after final ticket retires.

## 8. Async streaming and backpressure

Implement bounded SSE streaming:

- per-stream byte/token cap;
- asynchronous writes;
- no blocking model executor on a slow socket;
- writable events back to reactor;
- scheduler suppresses new decode when output buffer is full;
- slow-reader timeout triggers cancellation.

This turns slow clients into an explicit scheduling condition instead of hidden memory growth.

## 9. Fake engine

Create a deterministic `FakeEngine` before real concurrency tests.

Capabilities:

```text
configurable prefill/decode duration
manual barrier before/after execution
capacity limit
recoverable failure injection
restart-required failure injection
fatal failure injection
deterministic token generation
sequence ownership inspection
```

## 10. Deadline timers

Use the clock abstraction so tests can advance deadlines without sleeping.

Expired waiting requests should terminate without being submitted to the engine.

## 11. Shutdown coordinator

Implement bounded graceful shutdown:

```text
stop accept/admit
-> mark or drain waiting work according to config
-> stop new scheduling
-> retire in-flight batches
-> release sequences
-> close streams
-> flush bounded recorder
-> exit
```

Hard shutdown timeout is required.

## Tests

### State/lifetime

- every legal lifecycle path;
- illegal transition rejection;
- generation-stale completion;
- double cancellation;
- cancel before execution;
- cancel during fake in-flight batch;
- cancellation followed by slot reuse;
- engine failure during terminal intent;
- cleanup exactly once.

### Streaming

- fast reader;
- slow reader hits bounded buffer;
- disconnect while buffer has data;
- completion flush;
- shutdown mid-stream;
- no unbounded memory growth.

### Concurrency

Run fake-engine suite under TSan where compatible.

## Benchmark

Compare:

```text
A. llama-server external reference
B. InferScale FIFO
```

Do **not** expect InferScale to win yet. Record overhead honestly:

- HTTP/runtime overhead;
- scheduler/batch-build CPU time;
- TTFT/throughput difference;
- stream overhead.

A small regression is acceptable if it establishes a safer experimental control.

## Deliverables

- direct engine adapter;
- runtime reactor;
- request state machine;
- sequence allocator;
- FIFO policy;
- batch builder/tickets;
- async SSE/backpressure;
- fake engine;
- deterministic cancellation race tests;
- Phase-2 overhead report.

## Exit gate
Phase 2 is complete only when:

- fake-engine concurrency tests are deterministic and clean;
- cancellation during execution cannot cause early sequence reuse;
- slow readers cannot grow output buffers without bound;
- FIFO real-GPU E2E works for concurrent requests;
- graceful shutdown works during active load;
- ASan/UBSan pass and TSan-compatible tests are clean;
- InferScale FIFO becomes the internal scientific control for future phases.
