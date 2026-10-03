# Prompt 3 release campaign plan

This plan is frozen before the release-grade treatment runs. It resolves the
24 RPS thermal-aborted attempt by using separate headline and load-curve
workloads; that aborted run remains invalid evidence and cannot enter analysis.

## Frozen inputs and validity

- Build candidate: Prompt 3 validation branch after the committed harness fixes.
- Backend: llama.cpp v0.4.1, commit
  `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`.
- Model: TinyLlama-1.1B-Chat Q4_K_M, SHA-256
  `9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0`.
- GPU validity: start at or below 65 C after cooldown; invalidate at 85 C or
  on thermal slowdown; record one-second temperature, clocks, power, memory,
  utilization, and slowdown reasons.
- Load validity: seeded open-loop Poisson arrivals; schedule is generated
  independently of completions; invalidate above 100 ms maximum sender
  lateness, on missing server metrics, or on recorder drops.
- Release analysis: per-trial p99; paired trial is the bootstrap unit; 20,000
  deterministic percentile-bootstrap resamples; at least five complete valid
  pairs before emitting a 95% interval. Fewer pairs are `insufficient_pairs`.
- Policy order is randomized per trial from the recorded campaign seed; each
  policy in a paired trial uses the same request seed.

## Headline and supporting comparisons

| Run family | Treatments and workload | Frozen population | Purpose and reporting limit |
| --- | --- | --- | --- |
| Headline interactive | FIFO, static SA-WDS, adaptive SA-WDS; `phase5_interactive`; 16 offered requests/s | 10 trials x 1,100 offers per treatment (11,000 offers); require at least 10,000 successful TTFT observations per treatment to call p99 headline | Near-knee comparison selected from the short pre-release load pilot: 16 RPS produced a small explicit rejection count; lower load had no comparable overload signal. Report p99, throughput, SLO goodput, and rejection count together. |
| Load curve | FIFO, static SA-WDS, adaptive SA-WDS; a separate `phase5_loadcurve` request manifest; 8, 16, and 32 offered requests/s | 5 trials x 200 offers per treatment/rate | Shows below-knee, near-knee, and above-knee behavior. Descriptive secondary p99; 1,000 offers per treatment/rate is not represented as equal in strength to headline p99 evidence. |
| Mixed SLO | FIFO, static SA-WDS, adaptive SA-WDS; `phase5_mixed`; 8 offered requests/s | 5 trials x 600 offers per treatment | Measures mixed interactive/batch goodput, class wait, service-token share, rejections, and starvation. SLO thresholds are taken from the checked-in workload manifest; batch's 5 s threshold remains an operational starvation alarm, not a guarantee. |
| Chunking ablation | Static SA-WDS, chunk 512 vs batch-sized chunk 2048; same `phase5_mixed` workload and paired seeds | 5 trials x 600 offers per setting | Isolates scheduler chunk size; backend batch/ubatch, model, hard cap, admission reserve, and workload remain fixed. Secondary p99 population. |
| Resource-reservation ablation | Static SA-WDS, generation reserve 0 vs 64; same `phase5_mixed` workload and paired seeds | 5 trials x 600 offers per setting | Isolates the speculative generation reservation. Hard logical capacity and per-ticket growth authorization remain enabled in both settings. |
| Failure/resource scenarios | Single policy and targeted workloads: long context, long generation, burst, memory pressure, cancellation, deadline, slow reader | One bounded run per scenario; request counts from the checked-in manifests | Correctness and bounded-overload evidence only; not policy-comparison or p99 headline evidence. |
| External reference | Pinned `llama-server` baseline on the same model/host where its recorded artifacts remain intact | Existing Prompt 1 request population and raw evidence | Practical mature-system context only; never used to attribute scheduler causality. |

The primary load is selected using pre-release pilot behavior, not a
post-treatment policy winner. The previously attempted 24 RPS run
`20261003T132616Z` reached the 85 C validity stop before a complete treatment
trial; it is recorded as invalid and excluded from all estimates.

No safe production switch disables hard resource admission. The reserve-zero
ablation is the supported resource-admission comparison; it retains the same
hard capacity, soft admission bound, and growth authorization. No custom CUDA
optimization treatment is planned because Prompt 2 profiling retained none.
