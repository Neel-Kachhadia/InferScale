# InferScale — Benchmarking, Statistics, and Metrics Specification

## Principle
A scheduler project succeeds only if the experiment separates **policy effect** from backend/server differences and reports useful service under realistic load rather than a single best-case throughput number.

## 1. Baseline taxonomy

### A — external reference
Pinned unmodified `llama-server`.

Purpose: practical comparison to a mature server.

### B — internal scientific control
InferScale runtime with FIFO policy and otherwise identical engine/API/configuration.

Purpose: isolate policy changes.

### C — adaptive treatment
InferScale + SA-WDS + chunked prefill + KV-aware admission.

### D — feedback-adaptive treatment
C + bounded adaptive prefill-budget controller.

### E — optimized treatment
D (or the best validated static variant) + the retained Phase-4 optimization.

### Causal language rule

```text
B vs C -> scheduling/runtime-policy causality
C vs D -> feedback-controller causality
D vs E (or declared static base vs E) -> optimization causality
A vs C/D/E -> practical external comparison only
```

## 2. Workload dimensions

Every workload specifies exact distributions, not labels alone.

### Arrival process

- closed-loop fixed concurrency;
- open-loop fixed rate;
- open-loop Poisson-like arrivals;
- burst schedule.

### Prompt tokens
Example buckets only; final manifests must contain exact values/distributions:

```text
short:      32–256
medium:     257–1024
long:       1025–4096
very-long:  4097–context-safe limit
```

### Generation tokens

```text
short generation
medium generation
long generation
```

Use fixed or declared distributions and preserve requested vs actual token counts.

### Class/SLO
At least:

```text
interactive
batch
```

Each benchmark that reports goodput defines its SLO before runs are analyzed.

### Cancellation and slow-reader behavior
Cancellation time/probability and stream read rate must be workload parameters, not ad-hoc manual actions.

## 3. Coordinated-omission-safe open loop

For open-loop experiments, intended arrival times are generated independently of prior request completion.

Record:

```text
intended_arrival
actual_send
server_accept/response start
```

If the load-generator host cannot keep up and send lateness exceeds a declared tolerance, flag/invalidate the run.

Closed-loop tests are still useful for concurrency capacity but must not be used alone to characterize overload latency.

## 4. Latency definitions

Use one monotonic time domain per process and document cross-process measurement semantics.

### Client-observed TTFT

```text
request send start -> first complete streamed token/event available to client
```

### Server TTFT

```text
request accepted -> first token queued for stream
```

Report client-observed TTFT as the primary user metric and server internal components for diagnosis.

### Queue wait

Break down where possible:

```text
admission wait
scheduler-ready wait
stream-backpressure wait
```

### ITL
Report both a token-level distribution and per-request aggregate if useful; do not mix them silently.

### E2E

```text
request send/accept -> terminal response completion
```

State whether cancelled/failed requests are excluded or reported separately.

## 5. Throughput and goodput

### Raw throughput

- output tokens/s;
- processed prompt+output tokens/s where accounting is comparable;
- completed requests/s.

### SLO goodput

```text
goodput_rps = successful requests satisfying declared SLO / elapsed second
```

Optionally:

```text
goodput_tokens_s = output tokens from SLO-satisfying requests / second
```

Always report raw throughput alongside goodput.

## 6. Tail percentiles and sample size

Report:

```text
p50
p90
p95
p99
```

For headline p99 claims, target enough observations that the tail is supported by substantially more than a few samples. The recommended principal workload target is **>=10,000 measured requests per configuration across or within trials**, subject to run cost and independence considerations.

If fewer are used, report the exact N and widen the uncertainty discussion.

Never imply high precision from 500 requests where p99 is determined by ~5 observations.

## 7. Repetition and confidence

For headline comparisons:

- at least 5 full trials, preferably 7+;
- rotate/randomize configuration order;
- use paired seeds/workload traces where appropriate;
- compute bootstrap confidence intervals for the metric difference or another predeclared method;
- publish effect size, not only p-value;
- preserve all valid trials.

Example reporting:

```text
adaptive vs FIFO p99 TTFT difference:
median effect = -X ms (-Y%)
95% bootstrap CI = [a, b]
N requests = ...
trials = ...
```

Do not fill values until measured.

## 8. Saturation curve

One of the central figures:

```text
x-axis: offered request rate or concurrency
series:
  throughput
  SLO goodput
  p99 TTFT
  rejection rate
```

The goal is to show:

```text
underload -> efficient region -> saturation knee -> controlled overload
```

A runtime that rejects boundedly can be healthier than one that accepts everything and produces unbounded latency.

## 9. Scheduler/fairness metrics

By class:

- scheduler wait p50/p95/p99/max;
- service share;
- starvation-threshold violations;
- isolated-vs-mixed slowdown;
- deadline miss rate;
- prefill chunks before first decode opportunity;
- decode service gaps.

### Service-share error

```text
observed_share - configured_target_share
```

Interpret only under workloads where both classes remain continuously eligible.

## 10. Prefill-interference metrics

A specific mixed benchmark should pair:

```text
long prompt arrivals
+
short interactive requests
```

Report:

- short-request TTFT with no long work;
- short-request TTFT with unchunked long prefill;
- short-request TTFT with chunked prefill;
- throughput tradeoff;
- long-request completion slowdown.

This is one of the strongest demonstrations of InferScale's purpose.

## 11. KV/resource metrics

Track:

```text
hard usable capacity
soft reserved tokens
committed tokens
pressure state over time
per-request committed peak
admission defer/reject reason
hard-growth denials
backend allocation errors
GPU memory baseline/peak
```

Do not report logical blocks/pages as physical fragmentation eliminated unless a physical paged backend is actually implemented.

## 12. Cancellation metrics

Report distributions for:

```text
cancel signal -> request becomes scheduler-ineligible
cancel signal -> final BatchTicket retirement
cancel signal -> backend sequence release
post-cancel executed tokens/work items
```

This turns cancellation from a correctness footnote into measurable resource behavior.

## 13. CPU/GPU efficiency metrics

From traces/profiling:

- scheduler epoch duration;
- batch-build duration;
- executor idle gap;
- batch token occupancy;
- batch sequence occupancy;
- GPU active/idle timeline;
- launch/synchronization gaps;
- CPU utilization;
- GPU clock/temperature/power where available.

## 14. Thermal validity

Particularly on laptop GPUs, performance can drift with thermal/power state.

Record per trial when available:

```text
GPU temperature
SM/memory clock samples
power draw/limit
throttle reason
```

Rotate configuration order and allow a documented stabilization/cooldown protocol. Flag materially throttled trials.

## 15. Experiment manifests

Every run directory contains a resolved manifest such as:

```yaml
run_id: mixed-open-r12-adaptive-seed1337
trial: 3
policy: sa_wds
variant: adaptive_chunked_kv

model:
  id: <model>
  sha256: <hash>
  quantization: <quant>

software:
  inferscale_commit: <commit>
  llama_commit: <commit>
  compiler: <version>
  cuda: <version>
  driver: <version>

hardware:
  cpu: <model>
  gpu: <model>
  vram_bytes: <n>

workload:
  file: workloads/mixed_slo.yaml
  sha256: <hash>
  arrival: open_loop_poisson
  offered_rps: 3.5
  seed: 1337
  measured_requests: 10000

server:
  resolved_config: resolved_config.yaml

measurement:
  warmup_requests: 100
  trial_order_index: 2
  client_host: local
```

## 16. Run validity checks

Automated analyzer should reject/flag a run when:

- missing raw records;
- event-log drop count exceeds threshold;
- load generator send lateness exceeds tolerance;
- server crashed unexpectedly;
- model/config/checksum mismatch;
- thermal throttling exceeds declared rule;
- measurement N below workload's minimum;
- timestamp ordering is impossible;
- request accounting does not balance.

## 17. Required ablations

At minimum:

```text
FIFO vs adaptive
adaptive unchunked vs chunked
KV-aware admission off vs on under pressure
static vs feedback-adaptive prefill budget
retained optimization off vs on
```

If an optional feature is added, it gets its own ablation.

## 18. Required final charts

1. raw throughput vs offered load;
2. SLO goodput vs offered load;
3. p99 TTFT vs offered load;
4. TTFT CDF near saturation;
5. short-request slowdown under long-prefill interference;
6. batch wait/starvation/service share under sustained interactive load;
7. KV reserved/committed + rejection timeline during burst;
8. cancellation stop/release latency distribution;
9. batch token/sequence occupancy distribution;
10. profiling before/after for retained optimization.

Each chart embeds or links its run IDs.

## 19. Resume claim rule
A metric can become a resume bullet only when:

- the causal comparison is valid;
- exact workload/hardware context is stated;
- result repeats across trials;
- uncertainty is reported in project evidence;
- raw data is retained;
- the claim does not generalize beyond the tested model/hardware/workload.
