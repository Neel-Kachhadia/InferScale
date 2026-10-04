# Prompt 3 release campaign plan

Revision 2 was prepared after the first treatment in each sustained campaign
hit the thermal stop. The attempts used revision 1 (SHA-256
`39e653a9ff988c296666232b5e024f038151b258f631af251a63e48a32fa8deb`); neither
completed a treatment trial, so no valid paired estimate is being reinterpreted.

This plan has a prospective methodology amendment after the first treatment in
each sustained Prompt-3 campaign hit the thermal stop. No complete policy trial
or valid paired estimate existed when the amendment was made; the aborted raw
runs remain invalid evidence and cannot enter analysis.

## Frozen inputs and validity

- Build candidate: Prompt 3 validation branch after the committed harness fixes.
- Backend: llama.cpp v0.4.1, commit
  `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`.
- Model: TinyLlama-1.1B-Chat Q4_K_M, SHA-256
  `9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0`.
- GPU validity: start at or below 65 C after cooldown; invalidate at 85 C or
  on any thermal slowdown; record one-second temperature, clocks, power,
  memory, utilization, and slowdown reasons. A paired trial is comparable only
  when treatment start temperatures differ by at most 5 C.
- Load validity: seeded open-loop Poisson arrivals; schedule is generated
  independently of completions; invalidate above 100 ms maximum sender
  lateness, on missing server metrics, or on recorder drops.
- Treatment validity: compare runtime-resolved scheduler, adaptive state,
  context, sequence count, batch, ubatch, logical hard capacity, prefill chunk,
  and generation reserve with the requested treatment; any mismatch invalidates
  that run. Each campaign records the InferScale revision, tracked diff digest,
  server binary digest, backend revision, model digest, and workload digest.
- Release analysis: per-trial p99; paired trial is the bootstrap unit; 20,000
  deterministic percentile-bootstrap resamples; at least seven complete valid
  pairs before emitting a 95% interval. Headline interactive p99 also requires
  at least 10,000 successful interactive TTFT observations in each arm.
  Rejections and success fraction are reported with successful-response p99.
  Smaller populations are labeled secondary and do not receive a headline
  interval. Fewer than seven pairs is `insufficient_ci_pairs`.
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

Every campaign records its purpose (`release_comparison`,
`targeted_correctness`, or `reproduction_smoke`). The analysis tool isolates
smokes from policy comparisons and writes into `artifacts/release-v1/analysis`
by default so it cannot overwrite the curated benchmark evidence tables.

No safe production switch disables hard resource admission. The reserve-zero
ablation is the supported resource-admission comparison; it retains the same
hard capacity, soft admission bound, and growth authorization. No custom CUDA
optimization treatment is planned because Prompt 2 profiling retained none.

## Revision 3: prospectively frozen short-trial release campaign

This amendment is written before collecting any new B/C/D treatment results.
It changes only the trial size/repetition structure after the frozen 1,100-offer
interactive trial and 600-offer mixed trial repeatedly ran into the 85 C stop.
All earlier aborted runs remain `INVALID_THERMAL`; none is reinterpreted.

| Run family | Treatments and workload | Frozen population | Purpose and reporting limit |
| --- | --- | --- | --- |
| Headline interactive primary | FIFO, static SA-WDS, adaptive SA-WDS; `phase5_interactive`; 16 offered requests/s | 16 paired trials × 700 offers per treatment (11,200 offers per arm) | Headline p99 only if each arm has at least 10,000 successful TTFT observations and at least seven complete valid paired trials. Per-trial p99 remains the inferential unit. |
| Predeclared interactive thermal fallback | Same treatments/workload; 12 offered requests/s | 16 paired trials × 700 offers per treatment | Used only if the complete 16 RPS campaign has fewer than seven valid pairs or fewer than 10,000 TTFT observations in any arm because trials were invalidated by the frozen thermal rule. Do not use it to compensate for policy rejections, poor goodput, or an unfavorable effect. If its TTFT population is short, report secondary metrics without a headline p99. |
| Mixed SLO / fairness | FIFO, static SA-WDS, adaptive SA-WDS; `phase5_mixed`; 8 offered requests/s | 20 paired trials × 200 offers per treatment | Short independent trials with more paired repetitions. Goodput, class service, scheduler wait, starvation, and rejections are the primary outcomes; p99 is secondary and each population is disclosed. |

All paired trials keep the revision-2 configuration and validity contract:
16 sequences, 2,048 context/batch/ubatch, 512-token scheduler chunks, 64-token
generation reserve, pinned backend/model, same GPU/power settings, seeded open
Poisson arrivals, maximum sender lateness 100 ms, treatment start at or below
65 C, maximum paired start spread 5 C, and abort at 85 C or thermal slowdown.
The campaign's existing seeded treatment shuffle and shared request seed remain
in force. The runner records an invalid thermal treatment row with its context,
partial request records, GPU samples, and logs, then cools before continuing to
the next randomized treatment. Invalid runs remain visible in the index and are
excluded from paired inference. Nonthermal setup/configuration failures stop the
campaign after their invalid row is recorded.

The 12 RPS fallback is the only alternate interactive load. Existing pilot
evidence shows 12 RPS accepted all 100 offers for each policy with maximum
sender lateness below 2.3 ms, while the 16 RPS pilot produced 2, 4, and 0 HTTP
errors in 120 offers for FIFO, static, and adaptive respectively; 24 RPS
produced 24–30 HTTP errors per 120 offers. These brief pilots motivate 16 RPS
as the near-knee primary and 12 RPS as the single lower-load fallback; they are
not release effect evidence. The exact rows are retained in
`artifacts/release-v1/pilot-loadcurve/20261003T125048Z` and
`artifacts/release-v1/pilot-highload/20261003T125742Z`.

For Gate C, the separate FIFO resource soak is 20 minutes of 60-second mixed
open-Poisson blocks at 2 RPS. Every fourth block adds 20 cancellation offers
at 2 RPS; every fifth adds an 8-request burst using the memory-pressure
manifest. The soak requires five consecutive GPU samples at or below 60 C
before launch, one-second GPU/runtime telemetry, and the same 85 C or thermal
slowdown stop. It fails closed on missing telemetry, request/sequence/lease
drift, hard-capacity violation, invalid pressure state, recorder drops,
avoidable CUDA OOM markers, or failure to reach the documented RSS/GPU-memory
plateau and clean post-drain state. These limits are frozen before execution.
