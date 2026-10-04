# Prompt-3 benchmark results

## Release comparison decision

No thermally matched policy comparison was obtained. The frozen method used a 65 C maximum start temperature after cooldown, a 5 C maximum spread within a paired set, a stop at 85 C or thermal slowdown, open-loop seeded Poisson arrivals, a 100 ms sender-lateness limit, and trial-level paired inference. Thermal-invalid runs remain indexed and were excluded. Benchmark integrity is PASS WITH LIMITATION because the method preserves invalid runs and the analyzer refuses incomplete, unmatched, or invalid inference. This is not a performance conclusion.

Revision 3 prospectively froze the 16 RPS interactive primary (16 paired trials × 700 offers per treatment), one 12 RPS interactive fallback (same population), and an 8 RPS mixed SLO campaign (20 paired trials × 200 offers per treatment). The 12 RPS fallback was triggered by the primary's failure to meet its pair and p99-population gates. No further interactive load point was tried.

| Campaign | Planned arms | Runner-valid arms | Analyzer-valid standalone arms | INVALID_THERMAL | INVALID_CONFIG_MISMATCH | Eligible B/C/D paired trials | Treatment effects |
|---|---:|---:|---:|---:|---:|---:|---|
| 16 RPS interactive primary, 20261004T101418Z | 48 | 4 | 3 | 44 | 1 | 0 | Not estimated |
| 12 RPS interactive fallback, 20261004T104044Z | 48 | 0 | 0 | 48 | 0 | 0 | Not estimated |
| 8 RPS mixed SLO, 20261004T111143Z | 60 | 1 | 1 | 59 | 0 | 0 | Not estimated |
| **Total** | **156** | **5** | **4** | **151** | **1** | **0** | **Not estimated** |

At 16 RPS, FIFO, Static SA-WDS, and Adaptive SA-WDS were runner-valid in trial 1. Their starts were 47 C, 61 C, and 59 C, a 14 C span above the frozen 5 C pairing limit, so the triplet is not comparable. A second runner-valid Adaptive singleton in trial 3 had a changing GPU enforced-power-limit trace; the final analyzer classified it `INVALID_CONFIG_MISMATCH`. Its FIFO and Static SA-WDS partners were thermally invalid. At 8 RPS mixed, only FIFO trial 1 passed all final validity checks. The 12 RPS fallback had no thermally valid arms. The combined analyzer output has four valid standalone arms, 151 `INVALID_THERMAL` arms, one `INVALID_CONFIG_MISMATCH`, zero paired effects, and zero interval rows. The configured interval path requires at least seven complete paired trials, and headline interactive p99 additionally requires at least 10,000 successful TTFT observations per treatment. Neither threshold was met; no interval coverage claim is made from the absent comparison.

Older Prompt-3 attempts remain invalid historical evidence: 24 RPS campaign 20261003T132616Z, 16 RPS campaign 20261003T142113Z, and 8 RPS campaign 20261003T142325Z all reached the frozen thermal stop before completing valid treatment trials. They remain indexed, are not revived, and do not enter these totals or any estimate.

## Final policy evidence table

The table reports planned and valid arms within each frozen campaign. Counts are not pooled across different workloads or offered rates.

| Campaign | Policy | Planned arms | Analyzer-valid standalone | INVALID_THERMAL | INVALID_CONFIG_MISMATCH | Complete valid paired trials |
|---|---|---:|---:|---:|---:|---:|
| 16 RPS interactive primary | FIFO | 16 | 1 | 15 | 0 | 0 |
| 16 RPS interactive primary | Static SA-WDS | 16 | 1 | 15 | 0 | 0 |
| 16 RPS interactive primary | Adaptive SA-WDS | 16 | 1 | 14 | 1 | 0 |
| 12 RPS interactive fallback | FIFO | 16 | 0 | 16 | 0 | 0 |
| 12 RPS interactive fallback | Static SA-WDS | 16 | 0 | 16 | 0 | 0 |
| 12 RPS interactive fallback | Adaptive SA-WDS | 16 | 0 | 16 | 0 | 0 |
| 8 RPS mixed SLO | FIFO | 20 | 1 | 19 | 0 | 0 |
| 8 RPS mixed SLO | Static SA-WDS | 20 | 0 | 20 | 0 | 0 |
| 8 RPS mixed SLO | Adaptive SA-WDS | 20 | 0 | 20 | 0 | 0 |

The four analyzer-valid standalone arms are retained below for audit. They are not policy comparisons: the 16 RPS trial-1 triplet violates the paired temperature-spread criterion, and the mixed FIFO singleton has no valid partners. Validity was concentrated at the start of each campaign, so arm-level descriptives may be affected by run order and thermal carryover.

| Campaign / trial | Policy | Offers / server admitted / server rejected / client completed / HTTP errors | Interactive TTFT N | p50 / p95 / p99 TTFT (ms) | E2E p99 (ms) | SLO goodput (req/s) | Output (tokens/s) | Max interactive / batch ready wait (ms) | Starvation events (interactive / batch) | Peak GPU (C) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 16 RPS / 1 | FIFO | 700 / 678 / 22 / 678 / 22 | 678 | 72.95 / 288.01 / 693.85 | 1,355.70 | 14.913 | 361.37 | 55 / N/A | 0 / N/A | 75 |
| 16 RPS / 1 | Static SA-WDS | 700 / 667 / 33 / 667 / 33 | 667 | 86.83 / 188.58 / 235.08 | 1,306.14 | 14.751 | 356.49 | 82 / N/A | 0 / N/A | 81 |
| 16 RPS / 1 | Adaptive SA-WDS | 700 / 681 / 19 / 681 / 19 | 681 | 87.34 / 255.40 / 429.60 | 1,244.99 | 15.057 | 363.70 | 64 / N/A | 0 / N/A | 79 |
| 8 RPS mixed / 1 | FIFO | 200 / 187 / 13 / 187 / 13 | 161 | 128.50 / 314.06 / 407.77 | 3,783.15 | 8.286 | 242.02 | 191 / 138 | 0 / 0 | 79 |

“Server admitted” and “server rejected” are scheduler metric deltas; “client completed” is successful HTTP/SSE completion, and “HTTP errors” are load-generator outcomes. In these valid arms, server admissions match client completions, and every rejection was HTTP 429 (sequence-capacity rejection). The mixed FIFO arm's 13 rejections were 11 interactive and two batch; it completed 161 interactive and 26 batch requests. Batch wait and starvation are N/A for interactive-only trials. The mixed FIFO goodput is 187 client SLO-compliant responses divided by its 22.569-second measured run, including completion drain. Its 200 seeded Poisson arrivals spanned 22.466 seconds versus a 25-second expected span at nominal 8 RPS, so this finite realization's measured goodput of 8.286 req/s can exceed the nominal arrival rate. This is a descriptive denominator detail, not evidence of a scheduler effect.

The listed per-arm p99 values have populations of 161–681 and are descriptive observations only. The mixed campaign defines p99 as secondary. They do not satisfy the 10,000-observation headline rule and do not estimate scheduler effects.

The trial-3 Adaptive arm passed the original runner checks but its GPU trace changed enforced power limit from 40 W through 55 W to 100 W against a reported 100 W default; its mean graphics clock was 2,325 MHz. The cause is unknown. The final analyzer classifies this arm `INVALID_CONFIG_MISMATCH`, excludes it from standalone summaries and all inference, and checks every GPU sample for every release arm. Other four analyzer-valid arms stayed at 100 W in every sample. Five additional arms with non-default or changing power telemetry were already `INVALID_THERMAL`; their original thermal classifications remain unchanged.

## Paired treatment effects

| Comparison | Interactive TTFT p99 Δ / % | SLO goodput Δ / % | Output throughput Δ / % | Rejection count / rate Δ | 95% uncertainty interval |
|---|---|---|---|---|---|
| Static SA-WDS vs FIFO | Not estimated | Not estimated | Not estimated | Not estimated | Not available; 0 paired trials |
| Adaptive SA-WDS vs Static SA-WDS | Not estimated | Not estimated | Not estimated | Not estimated | Not available; 0 paired trials |
| Adaptive SA-WDS vs FIFO | Not estimated | Not estimated | Not estimated | Not estimated | Not available; 0 paired trials |

No release-grade conclusion is available about whether Static SA-WDS reduces p99, its goodput or throughput cost, whether Adaptive improves the tail further, or whether the differences are stable. No starvation comparison is available. FIFO remains the default as the conservative engineering choice; the campaign did not establish a comparative winner or a reproducible benefit for the other policies. SA-WDS and adaptive remain opt-in for workload-specific evaluation.

The laptop's sustained-thermal behavior prevented a thermally matched B/C/D comparison under the frozen protocol. A new campaign on a better-cooled or dedicated benchmark host is future optional publication-grade work. It must use a prospectively frozen configuration and protocol, retain the 85 C stop and validity rules, and collect complete valid pairs before computing effects or uncertainty intervals. The existing INVALID_THERMAL runs remain invalid and are not candidates for reclassification.

## Prompt-2 development comparison

These earlier measurements remain development evidence. They used the same RTX 4060 Laptop GPU, TinyLlama-1.1B Chat Q4_K_M, llama.cpp v0.4.1, a mixed workload at 8 offered requests/s, three paired seeds with 80 offers per treatment, and resolved batch/ubatch 2048/2048.

| Same-runtime policy | Interactive TTFT p99 (mean trial ms) | SLO goodput (req/s) | Output throughput (tokens/s) | Rejections / 240 offers |
|---|---:|---:|---:|---:|
| FIFO, chunk 512, static | 471 | 6.48 | 258.23 | 25 |
| SA-WDS C2, chunk 512, static | 421 | 5.24 | 215.01 | 48 |
| SA-WDS C3, chunk 512, static, reserve 64 | 442 | 5.35 | 218.07 | 43 |
| SA-WDS C4, adaptive over C3 | 318 | 5.13 | 210.43 | 57 |

Static C3's development mean p99 was 29 ms lower than FIFO, while mean goodput was 1.13 requests/s lower, output throughput was 15.6% lower, and rejections were 18 higher per 240 offers. Adaptive C4's development p99 was 124 ms lower than C3, while goodput was 0.22 requests/s lower and rejections 14 higher. Each 80-offer trial had about 50 interactive observations, so its p99 is coarse and may benefit from rejection shedding. These values must not be presented as release confidence estimates or generalized speedups.

## Thermal and artifact record

The campaigns used the same model, backend pin, server configuration, load generator, SLO definitions, and frozen 85 C stop. The requested GPU default was 100 W, but telemetry showed a non-default or changing enforced power limit in six primary-campaign arms. Five were already `INVALID_THERMAL`; the otherwise runner-valid Adaptive trial-3 arm is now `INVALID_CONFIG_MISMATCH`. The four analysis-valid arms held 100 W in every recorded GPU sample and peaked at 81 C. All 151 `INVALID_THERMAL` arms remain excluded from inference. Maximum sender lateness among the four valid standalone arms was 2.286 ms, below the frozen 100 ms limit. No complete paired comparison or treatment effect is available.

The 20.26-minute FIFO resource soak is separate safety evidence, not a treatment comparison. Its timeline is [resource-soak-timeline.svg](../artifacts/release-v1/benchmark/figures/resource-soak-timeline.svg), and the validity counts are [thermal-validity-summary.svg](../artifacts/release-v1/benchmark/figures/thermal-validity-summary.svg). The raw campaign paths, sizes, and SHA-256 hashes are in [raw-index.json](../artifacts/release-v1/benchmark/raw-index.json). The corrected analyzer output is [final-analysis-summary.json](../artifacts/release-v1/benchmark/final-analysis-summary.json), with row-level validity in [final-analysis-per-trial.csv](../artifacts/release-v1/benchmark/final-analysis-per-trial.csv) and exact valid-arm details in [valid-arm-details.csv](../artifacts/release-v1/valid-arm-details.csv). Comparative p99, goodput, throughput, fairness, and paired-effect figures were not generated because there are no complete valid paired inputs; see [NOT_RUN.md](../artifacts/release-v1/benchmark/figures/NOT_RUN.md).

The external llama-server results remain practical system context only. The Prompt-2 NVTX/Nsight finding is unchanged: backend CUDA execution and synchronization dominate, InferScale batch-plan construction was about 0.036% of that execution range, and no custom CUDA optimization was retained.
