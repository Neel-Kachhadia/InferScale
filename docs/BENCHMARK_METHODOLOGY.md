# Benchmark methodology

The async client supports closed-loop workers, precomputed fixed-rate open arrivals, seeded Poisson arrivals, and deterministic burst schedules. Each open-loop intended timestamp is generated before request completion can affect it. A bounded arrival queue caps client-side pending work; queue delay is reflected in `send_ns - intended_arrival_ns`. The run is invalid when any recorded send lateness exceeds the configured limit (default 100 ms) or the bounded JSONL recorder drops a row.

All `*_ns` values are client `time.perf_counter_ns()` readings from the same process. `accepted_ns` is when response headers arrive at the client, not a server clock. Cross-process monotonic timestamps are not subtracted. Primary TTFT and E2E include sender queueing by measuring from `intended_arrival_ns`; `service_ttft_ms` and `service_e2e_ms` additionally report send-to-response latency.

`prompt_tokens` is the requested workload size; the generated prompt is repeated text and may tokenize differently. `actual_prompt_tokens` and `actual_output_tokens` remain null unless the server returns usage. Throughput based on output tokens is null unless every successful response has an exact output-usage count. `token_arrivals_ns` records content-bearing SSE event arrival; ITL is therefore inter-event latency and must not be described as exact model-token ITL unless the backend stream contract guarantees one event per token. Latency percentiles use successful requests only; errors remain in raw records and every SLO-labeled failure remains an attainment miss.

The analyzer computes p50/p90/p95/p99 using linear interpolation on observed values, retaining failed and cancelled requests in the raw file. Requests with configured SLO labels stay in the SLO denominator even when they fail; only successful records marked `slo_met` count as goodput. It reports successful request rate and SLO goodput separately. The rate denominator is measured wall time from the start of the scheduled run through completion drain; finite seeded Poisson schedules may span less or more than their expected duration. A sweep writes all four TTFT, E2E, ITL, and arrival-lateness percentiles, request/output/processed-token rates where exact, error counts and statuses, sender lateness, validity, and run ID to CSV.

Release analysis accepts a policy comparison only when the exact treatment set
declared in the campaign manifest is present for the same workload, rate, trial,
and seed, and every planned arm passes validity and configuration checks. It
also requires each per-run summary and campaign index to mark its arm valid,
successful GPU query coverage to meet the declared 90% row threshold, recorded
start temperatures to be within the manifest limit, and paired starts to be
within 5 C. For release comparisons, every recorded GPU sample must also report
the enforced power limit at the driver-reported default; the limit must stay
constant throughout the run and match across paired treatments. A surviving
singleton is never comparison-eligible; incomplete
pairs have no paired temperature delta. The campaign must have no
`campaign-error.txt`. Invalid or non-comparable runs remain in `per-trial.csv`
for audit and never enter paired effects or figures. The analyzer's configured
minimum is seven complete paired trials for a *nominal* 95% percentile-bootstrap
interval; this minimum does not establish 95% coverage at such a small trial
count. The independent paired trial effect is the bootstrap unit (20,000
resamples); requests within a trial are not treated as independent experiments.
No release interval was emitted in Prompt-3. A publication-grade extension
should prospectively justify its interval coverage and use enough independent
trials. Headline interactive p99 additionally
requires at least 10,000 successful interactive TTFT observations per arm.
Per-trial p99 values are summarized across trials. The primary same-runtime
comparisons are static SA-WDS versus FIFO, adaptive SA-WDS versus FIFO, and
adaptive SA-WDS versus static SA-WDS; the last comparison isolates the
controller treatment. Figure whiskers show the exact observed min-to-max trial
range and are explicitly not confidence intervals.

The release campaign keeps GPU start temperature at or below 65 C, stops a
trial at 85 C or on any thermal slowdown, requires at least 90% one-second GPU
telemetry coverage, and flags sender lateness above 100 ms. It records GPU
temperature, graphics clock, utilization, power draw, memory, and throttle
reasons. Paired treatment start-temperature spread is bounded at 5 C; graphics
clock remains a reported covariate. A run that crosses a validity threshold is
retained as invalid and excluded from headline analysis.

The periodic GPU sampler currently counts successful query rows rather than
requiring every thermal-reason field to be present and parseable. Missing or
`N/A` throttle reasons are not treated as an affirmative throttle signal; this
is a limitation on thermal claims. The scheduler-wait histogram is differenced
between baseline and final snapshots, but the runner does not synchronize its
baseline snapshot with the warm-up histogram update. A small number of
warm-up work-item waits may be included.

The Prompt-1 sweep in [BASELINE.md](BASELINE.md) is a real-model practical system reference: one seeded 80-request point at each of four fixed offered rates, with exact model checksum, raw records, sender-lateness validity, and host telemetry. It does not claim a statistically stable headline p99 or scheduler causality. Research claims still require the frozen architecture's full sample-count and repeated-trial protocol, raw runs, GPU timeline, and a valid load-generator host.

## Final Prompt-3 collection outcome

Revision 3 froze 16 paired interactive trials of 700 offers per arm at 16 RPS, one permitted 12 RPS fallback with the same population, and 20 paired mixed trials of 200 offers per arm at 8 RPS. The repository's pre-existing campaign rule required a single sampled start at or below 65 C after cooldown with no active thermal slowdown, paired starts within 5 C, a stop at 85 C or any slowdown, and maximum sender lateness no greater than 100 ms. These rules were applied uniformly to FIFO, Static SA-WDS, and Adaptive SA-WDS. Plans, treatment orders, seeds, workload/configuration digests, and rules were recorded in the three protocol-intent files before each campaign. Those files have local pre-run timestamps; there was no independent signed preregistration. Campaign-index SHA-256 values were generated after collection, so they establish artifact integrity from indexing onward rather than independent proof of the pre-run state. Starting temperature is sampled before server/model warm-up. Treatment order was shuffled and recorded per trial, but was not separately balanced across the campaign.

The 16 RPS campaign had four arms that passed the runner's thermal/request checks. FIFO, Static SA-WDS, and Adaptive SA-WDS all had such a trial-1 arm, but that triplet exceeded the paired-start spread by 14 C and was not comparison-eligible. Its frozen rule therefore triggered the sole 12 RPS fallback, which yielded no thermally valid arms. The 8 RPS mixed campaign yielded one valid FIFO arm and no valid static or adaptive arm. Across the three current campaigns, 156 planned treatment arms produced four final analyzer-valid standalone arms, 151 `INVALID_THERMAL` arms, and one `INVALID_CONFIG_MISMATCH`. The trial-3 Adaptive arm's trace changed its enforced GPU power limit from 40 W to 55 W to 100 W against a reported 100 W default. Five other primary-campaign arms showed non-default limits but already remain `INVALID_THERMAL`; no thermal classification was changed. The analyzer emitted zero paired effects and confidence intervals. All invalid runs remain retained and excluded from inference. No policy-level p99, goodput, throughput, rejection, or fairness result is claimed.

The final campaign status and per-arm observations are reported in docs/BENCHMARKS.md. The analyzer reads the full GPU telemetry trace and rejects mid-run or treatment-to-treatment power-limit mismatches. The separate 20.26-minute resource soak is safety evidence and does not supply scheduler treatment effects.

The RTX 4060 Laptop did not sustain a thermally matched B/C/D campaign under
these frozen rules. This is an explicit hardware limitation. Thermal matching
is optional publication-grade evidence for the software release: the invalid
runs remain invalid, and the analyzer must continue to reject unmatched or
invalid inference. A future publication campaign requires a better-cooled or
dedicated benchmark host, prospectively frozen and independently auditable
protocol, a balanced treatment order, warm-up-aware telemetry baseline,
justified interval coverage, and enough complete valid pairs. It must not reuse
the invalid runs or claim a release-grade scheduler effect from them.
