# Phase 4 — Nsight-Guided CPU/GPU Optimization

## Goal
Use end-to-end profiling to identify the actual limiting path, then implement at most one or two targeted optimizations with correctness proof and ablation evidence.

## Duration target
**4–7 focused days**.

## 1. Instrument before changing performance code

Add NVTX ranges around:

```text
reactor epoch
admission
scheduler plan
KV authorization
batch build
engine submit
prefill chunk
decode step
sampling/postprocess
stream enqueue
executor idle period
batch retirement
```

Attach batch size/token-count metadata where profiling tooling permits without excessive overhead.

## 2. Capture representative Nsight Systems traces

Profile at least:

- low load;
- near saturation;
- mixed short/long workload;
- chunked-prefill interference workload;
- high cancellation load if cancellation overhead appears relevant.

Questions to answer:

```text
Is the GPU starved between launches?
Is batch construction CPU-bound?
Is synchronization serializing work?
Are transfers unexpectedly large?
Does the adaptive scheduler create tiny inefficient batches?
Does sampling/postprocessing dominate small decode steps?
Is networking/streaming delaying scheduling?
```

## 3. Classify the bottleneck

Choose one primary class:

```text
CPU policy/build overhead
GPU kernel/launch overhead
host-device synchronization/transfer
backend configuration
network/stream backpressure
memory pressure / capacity stalls
```

Do not write CUDA until the classification justifies it.

## 4. Candidate optimization experiments

### A. CPU-side batch packing

Potential techniques:

- reuse preallocated vectors/batch buffers;
- remove hot-path heap churn;
- flatten indirection;
- cache stable sequence metadata;
- reduce redundant validation while retaining debug assertions.

### B. CUDA Graph experiment

Only where decode/batch shapes are sufficiently stable. Measure graph capture/instantiation overhead and hit rate; discard if dynamic shapes eliminate benefit.

### C. Async control/metadata transfer

Only if profiling shows meaningful blocking transfer or synchronization.

### D. Sampling/post-processing kernel

Only if sampling/postprocess is a measured bottleneck and ownership is clean.

### E. Backend parameter optimization

A backend configuration sweep can be a legitimate performance result if it is clearly distinguished from InferScale scheduler improvements.

Examples: batch/ubatch or flash-attention configuration where applicable.

## 5. Optimization experiment contract

Before implementation write a short experiment note:

```text
observed bottleneck
trace/run ID
hypothesis
change
expected metric
correctness risk
rollback condition
```

This prevents hindsight-driven stories.

## 6. Correctness requirements

Any retained optimization must preserve:

- token/result correctness under deterministic sampling where comparable;
- lifecycle semantics;
- cancellation safety;
- KV invariants;
- error classification;
- stream ordering.

Custom CUDA additionally requires:

- reference CPU/backend comparison for edge sizes;
- non-multiple block sizes;
- empty/minimal inputs;
- maximum tested input;
- Compute Sanitizer or equivalent tooling;
- launch error checks;
- no hidden synchronization unless documented.

## 7. Measure end-to-end effect

For every candidate collect:

```text
micro metric (if applicable)
CPU batch-build time
GPU active/idle timeline
TTFT/ITL/E2E
throughput
goodput
GPU memory
power/thermal behavior
```

A microbenchmark win with no end-to-end improvement is not a retained optimization unless it has another justified production benefit.

## 8. Required ablation

Final benchmark includes:

```text
adaptive scheduler before optimization
adaptive scheduler after optimization
```

with all other configuration pinned.

## 9. Rejected optimization evidence

At least one rejected experiment may be documented if real evidence exists. This can strengthen the project by demonstrating profiling judgment.

Example format:

```text
CUDA Graph experiment reduced launch overhead in a synthetic stable-shape loop,
but mixed production-shaped batches caused low graph reuse and no statistically
significant goodput improvement, so the change was removed.
```

Do not fabricate a rejected experiment.

## 10. Physical paged KV is not a Phase-4 requirement

Implementing real PagedAttention requires backend memory/block-table and attention-path changes far beyond a normal optimization pass. If pursued, branch it as the separate research program described in `16_RESEARCH_EXTENSION_PAGED_KV.md`.

## Deliverables

- annotated Nsight Systems trace(s);
- bottleneck analysis document;
- before/after flamegraph or timeline evidence where useful;
- optimization experiment notes;
- correctness tests;
- isolated performance ablation;
- retained or explicitly rejected optimization decision.

## Exit gate
Phase 4 is complete when one of the following is true:

1. a measured optimization improves a meaningful end-to-end metric and passes correctness gates; or
2. profiling demonstrates the backend is already dominant/efficient, candidate changes do not help, and the repository contains credible evidence explaining why no custom optimization was retained.

The second outcome is valid engineering, not failure.
