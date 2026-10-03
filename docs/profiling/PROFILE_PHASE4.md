# Phase-4 representative GPU profile

## Capture manifest

| Field | Value |
|---|---|
| Branch / source base | `phase3-4/adaptive-scheduling-gpu` / `517b95e8235f2178276097a4a05cd081aa8cb645` (M2 base with working-tree Phase-3/4 changes) |
| Working-tree diff SHA-256 at capture | `843d0eb7f6d0baa72bc5454106e5fe5e2fa21e66f1d5ff659e6baf05155c8423` |
| Server binary SHA-256 | `223734d45b9a12f2149220da3cbd047003f7005962fdd387a19412bce618e975` |
| Model SHA-256 | `9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0` |
| llama.cpp commit | `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4` (`v0.4.1`) |
| Workload / seed | `phase3_mixed`, 80 open-Poisson arrivals at 8 RPS, concurrency 32, seed 32, C4 SA-WDS + chunking + resource reserve + adaptive control |
| Request result | 80/80 completed, 69 interactive + 11 batch, 0 load-generator record drops, no admission rejections |
| GPU / runtime | RTX 4060 Laptop, 8188 MiB, driver 580.178.04, CUDA 13.0, Nsight Systems 2025.3.2 |
| GPU sampled peak | 2135 MiB, 62 C, 95% utilization peak across 14 one-second samples |
| C4 load run ID | `616a0f33-0473-467a-a9e8-55372b03380e` |
| Trace | `build/phase3-evidence/final-tree/profile-C4-seed32/nsys-c4-seed32.nsys-rep`, 60,148,031 bytes, SHA-256 `0690cdf35cb4193aa20b7ff0cc340bd76a525c3bd368231c38cb65287640f155` |
| Compact reports | `build/phase3-evidence/final-tree/profile-C4-seed32/nsys-stats.txt`, `nsys-projections.txt`, and `stats-exit-codes.json` |

The capture predates the final Python-only change that waits briefly for a
fresh `/metrics` snapshot. That change does not alter requests, the C++ server
binary, or the trace. The binary and worktree-diff hashes above preserve the
profiled implementation identity. Large Nsight files remain local under
ignored `build/`; they are deliberately not committed.

## Readout

The profiled load generated 6.64 completed requests/s and 234.37 output
tokens/s over 12.045 measured seconds. Overall TTFT p99 was 3.470 s and E2E
p99 8.423 s. Interactive TTFT p99 was 409 ms; batch TTFT p99 was 4.276 s.
This is a short development profile, not release-grade latency evidence.

Nsight attributed 11.318 s of projected GPU operations to 11.878 s of
`LlamaEngine/BatchExecution` ranges (1000 batches). The projection is an
overlap-aware timeline association, not a standalone hardware utilization
measurement. Quantized matrix-vector kernels led the GPU summary; the largest
single row was a `mul_mat_vec_q` variant at 1.554 s, 19.1% of captured GPU
kernel/memory-operation time. CUDA API time included 42,196
`cudaStreamSynchronize` calls totaling 6.928 s and 854,714 kernel launches
totaling 2.224 s. These API durations overlap other activity and must not be
added as disjoint wall time.

The project-owned CPU ranges were small by comparison: 1236 SA-WDS ranges
totaled 5.81 ms (4.70 microseconds average), WorkPlan construction totaled
9.40 ms, and 1000 batch-plan builds totaled 4.28 ms (4.28 microseconds
average). Batch population totaled 0.68 ms. The scheduler epoch range totals
10.64 ms. `SSE/SocketWrite` has a large NVTX inclusive duration because the
async operation spans suspension and network waiting; that duration is not
CPU execution time. OS runtime wait totals also aggregate overlapping threads.

`perf` sampling was unavailable: matching kernel tools were absent and the
host's `perf_event_paranoid` policy prevented the requested sampling. No
kernel, sysctl, permission, driver, or package changes were made. Nsight
Systems still captured CPU scheduling ranges, CUDA calls, GPU operations,
NVTX nesting, and OS runtime events.

**Bottleneck classification:** llama.cpp model execution and its CUDA
launch/synchronization path dominate this workload. The trace does not show a
material InferScale scheduler or batch-population CPU hot path. It does not
establish that each backend synchronization is avoidable or independently
responsible for end-to-end latency.

## Separate final-tree GPU check

An unprofiled C3 mixed-load run after the final load-generator metrics-poll
change used the same model, workload, offered rate, seed, and host. Run ID
`4b517c61-a092-4d83-80b7-6822382e8431` completed 75 of 80 requests; five
interactive admissions were explicitly rejected. It reported 6.81 successful
requests/s, 6.45 SLO-goodput requests/s, and 239.38 output tokens/s, with
overall TTFT p99 2.198 s and E2E p99 5.269 s. The final metrics poll waited
59.6 ms and confirmed its completion counters: 64 interactive and 11 batch
completions. After drain, KV charge was zero, peak logical charge was 6275
tokens, and all 16 sequence slots were available. The separate profiled C4
run is not a paired optimization comparison.
