# InferScale — Testing, Model-Based Validation, and Failure Injection

## Testing principle
The highest-risk bugs in InferScale are not "wrong text" bugs. They are **lifetime, ordering, accounting, overload, and race bugs**. The test architecture is therefore built around deterministic state-machine control with a fake engine, then a smaller real-GPU validation layer.

## 1. Test pyramid

```text
                     real GPU E2E
                  /                 \
            API integration     soak/recovery
             /                         \
       fake-engine runtime integration
       /                                 \
 scheduler/admission/KV/lifecycle unit + model-based tests
                 |
          parser/fuzzer targets
```

Most correctness must be reproducible without loading a model.

## 2. Fake engine contract

`FakeEngine` must support:

```text
fixed or scripted execution latency
barrier before execute
barrier after partial work
sequence capacity limit
batch token/sequence limit
recoverable error injection
restart-required error injection
fatal error injection
deterministic output tokens
backend sequence position inspection
manual completion ordering
```

The fake engine should be deterministic for a given script/seed.

## 3. Request lifecycle model tests

### Legal paths

Verify every supported path from receipt to terminal cleanup.

Examples:

```text
normal completion
reject before admission
defer then admit
deadline before admission
cancel while waiting
cancel during prefill
cancel during decode
slow-reader timeout
recoverable engine error then retry
engine restart-required path
shutdown while waiting/in-flight
```

### Illegal transitions
Attempt transitions such as:

```text
TERMINAL -> DECODE_READY
RELEASED -> GROW_KV
CANCEL_PENDING_RETIRE -> new WorkItem
```

and require deterministic rejection/assertion in test builds.

## 4. Generation-fencing tests

Required race:

```text
42:g7 submit batch
pause fake engine
cancel g7
retire/reuse after safe point
42:g8 created
inject late g7 completion/event
assert g8 unchanged
assert stale event counter increments
```

Variants include late stream-writable events and duplicate executor completions.

## 5. Batch-ticket tests

Assert:

```text
issue increments inflight exactly once
retire decrements exactly once
move/destroy cannot double-retire
terminal resource release waits for zero inflight
failed batches still retire ticket ownership
partial backend work triggers reconciliation path
```

A request whose terminal intent is set may remain allocated while in flight, but must never receive new scheduled work.

## 6. Scheduler unit tests

### Determinism
Same snapshot/config/manual clock -> byte-for-byte equivalent plan ordering.

### Bounds

```text
plan.total_tokens <= epoch_max_tokens
plan.sequence_count <= max_sequences
prefill item <= max_prefill_chunk_tokens
prefill total <= max_prefill_tokens_per_epoch
```

### Decode protection
With ready decode work, verify configured minimum service share unless hard eligibility limits make it impossible.

### Fairness/aging
Under sustained interactive arrivals, batch work still crosses the service threshold before starvation limit.

### Deadlines
Near-deadline eligible work outranks otherwise equivalent same-class work according to the documented slack buckets.

### Memory eligibility
A request that cannot pass hard growth authorization is not scheduled regardless of class weight/priority.

### Adaptive prefill controller

Use `ManualClock` and scripted signal windows to verify:

- budget never leaves configured `[min,max]`;
- high pressure requires the configured persistence window before decreasing;
- low pressure/headroom requires persistence before increasing;
- hysteresis prevents flip-flop around one threshold;
- cooldown suppresses rapid repeated changes;
- static mode never changes the budget;
- controller cannot alter hard KV limits or fairness weights.

## 7. KV budget tests

### Accounting

- reserve;
- grow authorize/commit;
- reclaim unused soft reserve;
- release;
- exact-capacity boundary;
- pressure-state transitions;
- backend reconciliation.

### Invariants after every operation

```text
committed_global == sum(committed_live)
reserved_global == sum(reserved_live)
committed_global <= hard_usable_capacity
released request has no lease
no negative/overflow counters
stale generation cannot mutate lease
```

### Misleading reservation scenarios

- tiny `max_new_tokens` then policy adjustment;
- huge `max_new_tokens` but early EOS;
- many requests with conservative soft reservations;
- generation reaching soft reservation and requesting more growth.

## 8. Model-based randomized state testing

Create a simple reference model independent of production classes.

Generate sequences of operations from a deterministic seed:

```text
new request
admit/defer/reject
schedule work
complete/partial/fail work
cancel
expire deadline
stream block/unblock
engine restart
release
reuse slot
```

After each operation compare production observable state against the reference model.

Run thousands of seeds in normal CI and a much larger campaign before release.

Persist the failing seed and minimized event sequence.

## 9. Fuzz targets

Where practical, add libFuzzer/AFL-compatible targets for:

- JSON/request parsing with strict size caps;
- state-machine event sequence decoder;
- benchmark manifest parser;
- workload manifest parser.

Fuzzing must not require a GPU.

## 10. Admission tests

Required cases:

- impossible context length;
- request fits exactly at hard boundary;
- safety reserve causes defer;
- queue full;
- soft pressure causes defer;
- deadline already expired;
- high priority under soft pressure;
- high priority still denied by hard capacity;
- capacity frees and deferred request becomes admissible.

## 11. HTTP/SSE tests

- valid streaming request;
- malformed JSON;
- body above limit;
- unsupported field behavior documented;
- socket close before admission;
- socket close mid-prefill/decode;
- slow reader fills bounded buffer;
- stream resumes after writable event;
- slow-reader timeout cancels;
- health/readiness during saturation;
- shutdown mid-stream.

Measure process memory during slow-reader tests to prove boundedness.

## 12. Engine failure taxonomy tests

For each injected backend result assert the mapped action:

| Injected class | Expected scope | Expected action |
|---|---|---|
| RequestRecoverable | one request | fail request, release safely |
| EngineRecoverable | batch/request set | bounded retry/requeue or fail |
| EngineRestartRequired | engine | quiesce, restart, reconcile/fail affected work |
| ProcessFatal | process | emit evidence and terminate subprocess |

Unknown backend errors must map conservatively and be covered by a test.

## 13. Engine restart tests

In a subprocess or controlled test harness:

```text
inject restart-required failure
stop new submission
retire/fail affected tickets safely
recreate engine/context
clear stale sequence mappings
resume admission
verify new requests succeed
```

If restart fails, transition to process-fatal behavior.

## 14. Real-GPU E2E suite

Keep smaller than fake-engine tests but cover the integration seams:

- known prompt produces valid streamed output;
- 2/4/8+ concurrent requests as hardware permits;
- mixed context lengths;
- chunked long prefill + active decodes;
- cancellation during real execution;
- repeated sequence reuse;
- memory-pressure admission behavior;
- graceful shutdown;
- no monotonic GPU-memory growth.

## 15. CUDA-specific validation

Only if custom CUDA remains in v1:

- deterministic reference comparison;
- edge sizes;
- randomized input sizes;
- non-multiple block dimensions;
- launch error checks;
- Compute Sanitizer memcheck/racecheck where applicable;
- stress loop for asynchronous lifetime issues.

## 16. Sanitizer policy

Every milestone:

```text
Debug unit/integration
ASan + UBSan
```

Before release:

```text
TSan-compatible fake-engine/control-plane suite
Compute Sanitizer if custom CUDA exists
```

Suppressions require a checked-in explanation.

## 17. Soak tests

At least one long-running deterministic or reproducibly randomized workload repeatedly performs:

```text
admit -> prefill -> decode -> complete
admit -> cancel
admit -> slow-reader cancel
admit -> recoverable engine error
```

Track:

```text
RSS
GPU memory
live request count
live sequence count
inflight ticket count
KV committed/reserved
output buffer bytes
error counters
```

No monotonically growing resource without a documented cache is acceptable.

## 18. Performance-regression tests

GPU speed is not stable enough for ordinary CI exact thresholds.

CI may enforce:

- scheduler plan construction budget on synthetic snapshots;
- no unexpected allocations after warm-up in a microbenchmark where instrumentation exists;
- bounded event-queue handling time;
- benchmark parser/statistics golden tests;
- no >configured regression in fake-engine control-plane throughput on a controlled runner.

Release GPU performance is evaluated separately on the pinned benchmark machine.

## 19. Failure-injection table

| Failure | Injection | Required behavior |
|---|---|---|
| Client disconnect | close socket | terminal intent; no new work; delayed safe release |
| Slow reader | throttle reads | bounded buffer; scheduling suppression; timeout/cancel |
| Queue overload | burst | bounded reject/defer; server remains responsive |
| Hard KV limit | fake/capacity workload | no unsafe growth; wait/reject, no avoidable OOM |
| Recoverable engine error | fake return code | bounded retry/fail; ticket retires |
| Restart-required error | fake engine | quiesce/restart path |
| Fatal engine/CUDA error | subprocess injection | no continued serving from unknown state |
| Metrics sink failure | injected write failure | inference continues; loss counter increments |
| Shutdown | SIGTERM | bounded quiesce/drain/release/exit |
| Late completion | scripted out-of-order event | stale generation ignored/counted |

## 20. Release correctness rule
One unexplained lifetime/accounting invariant failure invalidates the release candidate even if all benchmark numbers look excellent.
