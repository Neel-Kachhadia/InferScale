# InferScale — Resume, GitHub, and Interview Evidence

## Positioning
InferScale should be presented as a **C++/CUDA inference-runtime scheduling and performance-engineering project**, not as a claim to have reinvented llama.cpp, vLLM, or PagedAttention.

The differentiator is the combination of:

```text
direct backend control
single-owner lifecycle safety
chunked prefill
SLO-aware fair scheduling
KV-aware admission
overload/backpressure
cancellation-safe retirement
profiling + rigorous benchmark evidence
```

## Weak resume wording to avoid

> Built an LLM inference server using C++ and CUDA.

> Implemented PagedAttention for faster inference.

> Made llama.cpp faster.

These are either vague or potentially inaccurate.

## Strong bullet templates
Use measured values only.

> Built a C++20/CUDA single-GPU LLM serving runtime on direct `libllama`, with generation-fenced request lifecycles, chunked prefill, bounded streaming backpressure, KV-aware admission, and SLO-aware weighted-deficit scheduling.

> Reduced mixed-workload p99 time-to-first-token by **X%** versus the same-runtime FIFO control at **Y req/s**, while maintaining **Z req/s SLO goodput** and zero batch-starvation violations across **N** measured requests on **[GPU/model]**.

> Designed cancellation-safe batch retirement using request generations and in-flight tickets; deterministic race/failure tests verified zero stale-generation mutation and exactly-once resource release across **N** randomized event sequences.

> Profiled CPU/GPU execution with NVTX, Nsight Systems, and perf; removed **[measured bottleneck]**, improving **[end-to-end metric]** by **X%** with an isolated ablation and preserved raw benchmark evidence.

Do not fill placeholders until release data exists.

## GitHub evidence hierarchy

A recruiter should be able to verify the project quickly from:

```text
README architecture diagram
final benchmark table
one strong saturation/goodput chart
one prefill-interference chart
one failure/lifetime diagram
reproduction command
```

A senior interviewer should then be able to drill into:

```text
docs/ADR/
docs/SCHEDULING.md
docs/KV_ACCOUNTING.md
docs/FAILURE_MODEL.md
docs/BENCHMARK_METHODOLOGY.md
bench/releases/v1/
tests/model/
tests/integration/
docs/profiling/
```

## Core interview narrative

### Situation
Mixed prompt lengths and service classes create a throughput/TTFT/fairness/memory tradeoff on a single GPU.

### Why not just use `llama-server`?
`llama-server` is the external reference. InferScale needs direct `libllama` control so its own scheduler decides exact prefill/decode work; otherwise it would be benchmarking one scheduler wrapped around another.

### Architectural decision
A single-owner runtime reactor receives network/executor events. This makes request state, cancellation, and resource retirement deterministic enough to model-test.

### Hard concurrency problem
A client can cancel while an engine batch still references its sequence. InferScale marks terminal intent immediately but delays sequence/KV release until all generation-tagged `BatchTicket`s retire.

### Hard scheduling problem
Long prefills are split into bounded token chunks. Weighted class credits plus aging preserve batch progress while decode protection limits interactive token gaps.

### Hard resource problem
InferScale does not pretend to own physical PagedAttention in v1. It enforces hard logical/backend capacity and soft reservations, with runtime growth authorization and backend reconciliation.

### Measurement discipline
`llama-server` is an external comparison, while InferScale FIFO is the scheduler control. Open-loop load avoids coordinated omission. Tail claims use large populations, repeated trials, and uncertainty intervals.

## Interview failure story candidates
Only use stories that actually occurred during implementation.

Potential categories:

```text
stale completion after request-slot reuse
cancelled request released sequence before batch retirement
long prefill destroyed interactive TTFT
adaptive scheduler improved TTFT but starved batch service
soft KV reservations were too conservative and cut throughput
CUDA micro-optimization helped kernel time but hurt end-to-end behavior
thermal throttling invalidated an apparent performance win
```

A strong story includes symptom, evidence, root cause, redesign, and regression test.

## Evidence expected before high-stakes resume use

```text
clean public repository
pinned backend and dependency revisions
architecture + ADRs
unit/model/integration tests
sanitizer evidence
coordinated-omission-safe load generator
external and same-runtime baselines
raw benchmark data
confidence/variation reporting
p50/p95/p99 + goodput
fairness/starvation evidence
memory/overload evidence
cancellation measurements
Nsight trace and optimization decision
limitations document
reproduction script
```

## Roles this project supports most strongly

- AI infrastructure SWE;
- inference/runtime engineer;
- ML systems engineer;
- C++ systems engineer;
- GPU software/performance engineer;
- backend infrastructure engineer.

The project is less directly targeted at model-training research or frontend/product roles.

## Claim discipline
The repository should make it easier to underclaim than overclaim. A narrower bullet with raw evidence is stronger than a universal "faster inference" statement.
