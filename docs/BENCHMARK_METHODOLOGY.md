# Benchmark methodology

The async client supports closed-loop workers, precomputed fixed-rate open arrivals, seeded Poisson arrivals, and deterministic burst schedules. Each open-loop intended timestamp is generated before request completion can affect it. A bounded arrival queue caps client-side pending work; queue delay is reflected in `send_ns - intended_arrival_ns`. The run is invalid when any recorded send lateness exceeds the configured limit (default 100 ms) or the bounded JSONL recorder drops a row.

All `*_ns` values are client `time.perf_counter_ns()` readings from the same process. `accepted_ns` is when response headers arrive at the client, not a server clock. Cross-process monotonic timestamps are not subtracted. Primary TTFT and E2E include sender queueing by measuring from `intended_arrival_ns`; `service_ttft_ms` and `service_e2e_ms` additionally report send-to-response latency.

`prompt_tokens` is the requested workload size; the generated prompt is repeated text and may tokenize differently. `actual_prompt_tokens` and `actual_output_tokens` remain null unless the server returns usage. Throughput based on output tokens is null unless every successful response has an exact output-usage count. `token_arrivals_ns` records content-bearing SSE event arrival; ITL is therefore inter-event latency and must not be described as exact model-token ITL unless the backend stream contract guarantees one event per token. Latency percentiles use successful requests only; errors remain in raw records and every SLO-labeled failure remains an attainment miss.

The analyzer computes p50/p90/p95/p99 using linear interpolation on observed values, retaining failed and cancelled requests in the raw file. Requests with configured SLO labels stay in the SLO denominator even when they fail; only successful records marked `slo_met` count as goodput. It reports successful request rate and SLO goodput separately. A sweep writes all four TTFT, E2E, ITL, and arrival-lateness percentiles, request/output/processed-token rates where exact, error counts and statuses, sender lateness, validity, and run ID to CSV.

Release analysis accepts a policy-comparison run only when both the per-run
summary and campaign index mark it valid, the purpose is `release_comparison`,
the runtime-resolved configuration matches the requested treatment, successful
GPU query coverage meets the declared 90% row threshold, the recorded start
temperature is within the manifest limit, paired treatments start within 5 C,
and the campaign has no
`campaign-error.txt`. Invalid or non-comparable runs remain in `per-trial.csv`
for audit and never enter paired effects or figures. A 95% percentile-bootstrap
interval requires at least seven complete paired treatment trials; lower counts
report the median effect without an interval. The independent paired trial
effect is the bootstrap unit (20,000 resamples); requests within a trial are
not treated as independent experiments. Headline interactive p99 additionally
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
