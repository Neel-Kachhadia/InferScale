# Phase 5 — Production Validation, Failure Campaign, and Benchmark Release

## Goal
Turn the implementation into auditable evidence: stress it, break it deterministically, quantify uncertainty, freeze a reproducible release, and document limits without inflated claims.

## Duration target
**5–8 focused days**.

## 1. Freeze candidate release inputs

Before final comparisons freeze:

```text
InferScale commit
llama.cpp commit
model + checksum
compiler/build flags
CUDA/driver
scheduler config
KV config
HTTP limits
sampling config
workload manifests
load-generator commit
statistical analysis code
```

Any later code change that can affect results requires new run IDs.

## 2. Stress matrix

Run combinations across:

### Load

```text
under saturation
near saturation
above saturation
burst overload
```

### Workload

```text
interactive-short
long-context
mixed short+long
long generation
high cancellation
deadline expiry
slow readers
memory pressure
```

### Policy

```text
FIFO
adaptive without chunking
adaptive + chunking
full adaptive + KV-aware admission
full adaptive + retained Phase-4 optimization
```

## 3. Failure campaign

Inject:

- disconnect before admission;
- disconnect during prefill;
- disconnect during decode;
- cancellation during fake engine barrier;
- stale batch completion after generation reuse;
- queue overflow;
- hard KV growth denial;
- recoverable engine error;
- engine restart-required error;
- process-fatal error in a subprocess test;
- metrics/event-recorder failure;
- slow client;
- `SIGTERM` during active load;
- forced shutdown timeout path.

Each scenario has an expected lifecycle/resource outcome.

## 4. Sanitizer/tooling campaign

Required evidence:

```text
ASan + UBSan: unit/integration/fake-engine suite
TSan: control-plane/fake-engine concurrency suite where compatible
Compute Sanitizer: retained custom CUDA targets, if any
```

Document suppressions with justification. Do not silently ignore sanitizer failures.

## 5. Soak tests

Run repeated request/cancellation cycles long enough to detect monotonic growth or rare lifecycle bugs.

Track:

- host RSS;
- GPU memory;
- active request count;
- backend sequence slots;
- KV committed/reserved total;
- dropped metric/event records;
- engine restarts;
- invariant violations.

A soak run with one invariant violation fails the release.

## 6. Final performance protocol

Headline comparisons require:

- randomized/rotated trial order to reduce thermal/time-order bias;
- identical workloads and seeds where paired comparison is valid;
- sufficient request population for tail claims;
- at least 5 repeated trials, preferably 7+;
- bootstrap or otherwise documented uncertainty intervals;
- preserved raw data;
- no removal of inconvenient valid trials.

Flag thermal throttling or client-generator overload.

## 7. Final benchmark comparisons

Publish:

```text
A. llama-server external reference
B. InferScale FIFO
C. InferScale adaptive
D. InferScale adaptive + retained optimization
```

Causality claims about scheduling use **B vs C**.

A vs C answers a different practical question and must be worded accordingly.

## 8. Required ablations

At least:

```text
chunked prefill off/on
aging/fairness off/on or FIFO/adaptive
KV-aware admission off/on under pressure
Phase-4 optimization off/on
```

Optional features such as prefix reuse require separate ablations.

## 9. Required charts/tables

The final release should include:

```text
throughput vs offered load
SLO goodput vs offered load
p99 TTFT vs offered load
TTFT CDF at representative saturation point
interactive slowdown with long-prefill interference
batch wait/starvation behavior
KV reserved/committed timeline during burst
scheduler epoch/batch occupancy distribution
cancellation stop/release latency
before/after profiling timeline for retained optimization
```

Each figure must link to run IDs/raw data.

## 10. README story

The top-level README should answer in under two minutes:

```text
What problem?
What does InferScale own?
Why direct libllama?
What is SA-WDS?
What is chunked prefill?
What is the KV accounting boundary?
What improved under what exact workload?
What did not improve?
How can I reproduce it?
```

## 11. Failure-model document

Publish a concise table mapping every major error to:

```text
failure class
affected scope
retry/restart policy
resource cleanup behavior
observable metric/log
```

## 12. Limitations section

Mandatory examples:

- single GPU;
- backend-owned physical KV representation;
- no claim of general superiority across every model/GPU;
- scheduling SLO thresholds tuned/validated for documented workloads;
- external server comparison is not a scheduler-only causal experiment;
- any optional custom CUDA optimization is hardware/workload dependent.

## 13. Reproduction bundle

A release artifact should contain or regenerate:

```text
resolved config
environment manifest
workload manifests
exact commands
raw per-request records
summary statistics
plots
profiling references
checksums
```

Provide a `reproduce_release.sh`/documented equivalent that fails loudly when prerequisites differ.

## 14. Final release gate

No release tag until:

- all correctness/resource invariants pass;
- no known cancellation race remains;
- overload produces bounded behavior;
- sanitizer campaign is clean/justified;
- benchmark methodology passes its own validation checks;
- headline improvements are statistically defensible;
- regressions/tradeoffs are documented;
- claims use actual measured numbers only.

Suggested tag:

```text
v1.0-resume-grade
```

An `exceptional` tag/status requires the additional conditions in `14_DEFINITION_OF_DONE.md`.
