# Prompt-3 benchmark status and measured results

## Current release status

The Prompt-3 release-grade policy comparisons did not complete under the
predeclared thermal validity rule. The laptop GPU reached the 85 C stop during
the first treatment in two sustained campaigns. Those runs have no completed
load-generator summary or indexed trial and are excluded from every estimate.
No Prompt-3 treatment effect, release p99, or confidence interval is claimed.

| Campaign | Predeclared workload | Attempt | Observed | Result |
|---|---|---|---|---|
| `20261003T142113Z` | `phase5_interactive`, 16 RPS | 10 trials × 1,100 offers, FIFO/static SA-WDS/adaptive | First randomized treatment was FIFO; started at 49 C, hit 85 C after 63 GPU samples; 0 completed index rows | Invalid, excluded |
| `20261003T142325Z` | `phase5_mixed`, 8 RPS | 5 trials × 600 offers, FIFO/static SA-WDS/adaptive | First randomized treatment was adaptive SA-WDS; started at 53 C, hit 85 C after 52 GPU samples; 0 completed index rows | Invalid, excluded |
| `20261003T132616Z` | Earlier 24 RPS attempt | Partial trial | Reached the 85 C stop | Invalid, excluded |

The runner stopped each load generator as soon as a sample reached 85 C. The
hardware was allowed to cool between attempts. No thermal cutoff, clock, power
limit, driver, or system setting was changed to extend a run.

Five bounded 4 RPS smoke trials did complete: the in-tree runner smoke and
four clean-worktree reproductions each accepted 8/8 requests. The in-tree smoke
peaked at 57 C, with maximum sender lateness 1.25 ms. The first clean smoke
started at 61 C and peaked at 67 C, with 2.019 ms maximum lateness. The clean
reproduction of source commit `27963bcd` started at 59 C and peaked at 64 C,
with 2.245 ms maximum lateness and 90% server-metrics coverage. The final clean
reproduction of source commit `26a5cce` started at 56 C and peaked at 59 C,
with 2.203 ms maximum lateness and 90% server-metrics coverage. The latest
clean reproduction, analyzer source commit `887bb0b`, started at 57 C and
peaked at 59 C, with 2.424 ms maximum sender lateness and 90% server-metrics
coverage. Its analyzer indexed one valid harness run and zero
policy-comparison-eligible runs. The analyzer emits static-vs-FIFO,
adaptive-vs-FIFO, and adaptive-vs-static effects for eligible paired trials;
this smoke supplied no comparison data. The in-tree run resolved the CUDA
backend, 16 sequences, 2,048-token context/batch/ubatch, and 29,491 logical
hard tokens. Corrected GPU telemetry
reported a 100 W enforced limit, 29.37 W mean average draw, and 31.31 W mean
instantaneous draw. All five smokes are explicitly marked reproduction-only;
their request rates, TTFT, and token throughput are not policy performance
evidence. All five smokes are explicitly marked reproduction-only. The clean
requests, metrics, GPU samples, manifests, and analysis are retained in
[`artifacts/release-v1/reproduction/clean-smoke`](../artifacts/release-v1/reproduction/clean-smoke).
Captured server/load-generator console logs remain local; their paths, sizes,
and SHA-256 checksums appear in the raw index.

The attempt summaries, raw telemetry, and run indexes are retained under
[`artifacts/release-v1/benchmark`](../artifacts/release-v1/benchmark). The
first 16 RPS and 8 RPS campaigns have empty indexes plus explicit
`campaign-error.txt` files. Analysis fails closed on incomplete campaigns.

## Prompt-2 development comparison

Until thermally valid repeated Prompt-3 trials can be collected, these
measurements remain the best same-runtime policy evidence. They used the
recorded RTX 4060 Laptop GPU, TinyLlama-1.1B Chat Q4_K_M, pinned llama.cpp
v0.4.1, a mixed workload at 8 offered requests/s, three paired seeds with
80 offers per treatment, and resolved batch/ubatch 2048/2048. They are
development evidence, not stable release confidence estimates.

| Same-runtime policy | Interactive TTFT p99 (mean trial ms) | SLO goodput (req/s) | Output throughput (tokens/s) | Rejections / 240 offers |
|---|---:|---:|---:|---:|
| FIFO, chunk 512, static | 471 | 6.48 | 258.23 | 25 |
| SA-WDS C2, chunk 512, static | 421 | 5.24 | 215.01 | 48 |
| SA-WDS C3, chunk 512, static, reserve 64 | 442 | 5.35 | 218.07 | 43 |
| SA-WDS C4, adaptive over C3 | 318 | 5.13 | 210.43 | 57 |

Static C3 improved the development mean interactive p99 by 29 ms relative to
FIFO while reducing mean SLO goodput by 1.13 requests/s (17.4%), reducing
output throughput by 15.6%, and adding 18 rejections per 240 offers. C2 had a
50 ms lower interactive p99 than FIFO but a larger goodput reduction of 1.24
requests/s. Adaptive C4 reduced mean interactive p99 by 124 ms relative to C3,
while reducing goodput by 0.22 requests/s and adding 14 rejections. These
tradeoffs support FIFO as the conservative default and keep SA-WDS and
adaptive mode opt-in pending workload-specific evaluation.

The reported p99 values are calculated from completed responses within each
small trial. Each 80-offer mixed trial has only about 50 interactive
observations, so its p99 is a coarse upper-tail statistic; rejected requests
can also remove slow requests from the successful-response distribution. The
goodput and rejection counts above must be read with the p99 values. No
confidence interval or release-scale p99 claim is supported by these samples.

The C1-to-C2 development ablation did not show a consistent goodput or tail
gain from smaller chunks. Chunking remains bounded interleaving behavior, not
a demonstrated throughput optimization. C2-to-C3's reserve change recovered
five admissions and about 0.11 requests/s mean goodput across 240 offers; the
effect was small and seed-sensitive. The zero-reserve comparison retains the
same hard logical capacity and growth authorization; it does not turn off
safety admission.

The external `llama-server` results are preserved in
[`BASELINE.md`](BASELINE.md) as practical system context. They differ in
implementation and do not support scheduler-causal claims. Same-runtime FIFO
is the scheduler control.

## Frozen method and release figures

[`RELEASE_CAMPAIGN_PLAN.md`](RELEASE_CAMPAIGN_PLAN.md) records the protocol
revision for any future campaign: open-loop seeded Poisson arrivals; maximum
sender lateness 100 ms; randomized policy order; paired request seeds; 10 ×
1,100 offers at the selected 16 RPS interactive point; paired starts within 5 C;
and 20,000-resample percentile-bootstrap intervals over at least seven paired
trials. Headline p99 requires at least 10,000 successful interactive TTFT
observations per treatment. Rejection and completion effects are reported with
successful-response p99. The planned secondary load curve and ablations
disclosed their smaller populations in advance; earlier aborted campaigns used
plan revision 1, recorded by its SHA-256 in the plan.

Because no sustained campaign completed, the required release comparison
figures (throughput/goodput/p99 curves, TTFT CDF, chunking, fairness, pressure,
controller, cancellation, and profile summary) are recorded as not generated
in [`artifacts/release-v1/benchmark/figures/NOT_RUN.md`](../artifacts/release-v1/benchmark/figures/NOT_RUN.md).
The Prompt-2 NVTX/Nsight evidence remains valid for the unchanged runtime hot
path and is indexed in [`profiling/README.md`](profiling/README.md). No new
profile trace or optimization claim is made.
