# Prompt-3 final validation and release gate record

Recorded 2026-10-04 for the local release candidate on branch `phase5/final-validation-release`. The release policy treats a thermally matched scheduler comparison as optional publication-grade evidence, not a software-release gate. All previous thermal aborts remain `INVALID_THERMAL`; no confidence interval or paired treatment effect is computed from invalid or unmatched runs. The `v1.0-resume-grade` tag means implementation-complete, thoroughly validated, reproducible, and resume-grade. It does not claim publication-grade thermally controlled performance proof.

## Repository and frozen inputs

| Item | Verified value |
|---|---|
| Repository root | `/home/neel/Projects/InferScale` |
| Fresh-session starting commit | `19235fa831ca3d54b91c0e07172573b8bac426d3` |
| Earlier Prompt-3 baseline | `0b2c198d9138314e5f75ce9e68b66856e412c095` |
| Final branch | `phase5/final-validation-release` |
| Runtime source candidate | `850db0c05bc3f2f87a0ad14649b1e35034a4d594` |
| Analysis hardening | `86e4f46dbea5249311acecca28b08cf0991355ea` |
| Milestone 1 | `030c7b9dcba4019f445dec1b3f1a92af8a134cc7` |
| Milestone 2 | `517b95e8235f2178276097a4a05cd081aa8cb645` |
| Milestone 3 | `935d811b3fff52cef789205243b0046720bdaa77` |
| Milestone 4 | `04712f5f3cd58bf8cebe07f5854f8a3ae95a751c` |
| llama.cpp | v0.4.1, `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4` |
| Model | TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf, 668,788,096 bytes |
| Model SHA-256 | `9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0` |
| Host | Ubuntu 24.04.5, kernel 7.0.0-38-generic; Intel i9-13980HX, 32 logical CPUs; 15.23 GiB RAM |
| GPU | NVIDIA GeForce RTX 4060 Laptop GPU, 8,188 MiB, compute capability 8.9 |
| Driver / CUDA / nvcc | 580.178.04 / 13.0 / 13.0.88, previously verified; a later `nvidia-smi` query could not communicate with the driver |
| Compiler / build | GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3 |

The model checksum, backend pin, and environment were reverified for this continuation. Runtime configuration resolved to 16 sequences, 2,048 context/batch/ubatch, 99 GPU layers, and 29,491 logical hard tokens. Physical KV remains llama.cpp-owned. FIFO is the conservative default; Static SA-WDS and Adaptive SA-WDS remain opt-in. Frozen SLOs are interactive TTFT ≤750 ms, ready-wait p99 ≤500 ms, E2E ≤8 s, and a five-second batch starvation alarm; the alarm is not a deadline guarantee.

## Build, tests, sanitizers, and reproduction

| Check | Status | Evidence |
|---|---|---|
| Clean CUDA/Ninja Release build | PASS | Fresh detached worktree at runtime source commit `850db0c`; 256 Ninja steps, pinned llama.cpp, external model. |
| CPU/FakeEngine CTest | PASS | Runtime, policy, and HTTP targets: 3/3. |
| ASan | PASS | Current-source supported CTest 3/3 with address checks and leak detection enabled. |
| UBSan | PASS | Supported CTest 3/3 with halt-on-error. |
| TSan | PASS WITH LIMITATION | Supported runtime, policy, and HTTP targets 3/3 with the documented per-process ASLR workaround; GCC does not model Boost.Asio `atomic_thread_fence`. |
| LSan | PASS WITH LIMITATION | Host-capable ASan CTest 3/3 with `detect_leaks=1` and no leak report. A separate sandboxed teardown failed with the documented ptrace limitation; both outcomes are retained. |
| Python suite | PASS | Clean worktree at analysis hardening `86e4f46`: 42/42, zero skips, loopback enabled. |
| Randomized lifecycle | PASS | 256 seeds, up to 64 operations and eight requests per seed; cleanup invariants checked. |
| Parser mutation | PASS WITH LIMITATION | 2,048 deterministic mutations; not coverage-guided fuzzing. |
| Historical model-backed integration | PASS WITH LIMITATION | Earlier CUDA CTest 6/6, including llama runtime, CLI help, and slow-reader path; source was `7f27cf33`, not rerun in this continuation. |
| Current-source GPU smoke | PASS WITH LIMITATION | Runtime source `850db0c`, current model: 8/8 requests, start 62 C, peak 68 C, zero thermal-slowdown samples, max sender lateness 2.295 ms. Smoke only; no performance or endurance claim. A later driver query failed. |
| Secret scan | PASS WITH LIMITATION | Tracked-file detect-secrets hook passes with 1,049 reviewed nonsecret baseline entries for evidence digests, commit/run identifiers, paths, and the scanner field label; no model weights or credentials. |

Full details are in [reproduction](REPRODUCTION.md), [sanitizer evidence](../artifacts/release-v1/sanitizer-summary.md), and the release manifest.

## Gate C resource-safety soak

The separate FIFO soak at `artifacts/release-v1/soak/20261004T094228Z` ran 20.2555 minutes and collected 1,190 samples at 100% runtime telemetry coverage. It offered 2,462 requests, completed 2,438, cancelled 24, and had zero rejection, recorder drop, resource invariant violation, hard-capacity violation, avoidable OOM marker, or event-queue rejection. Maximum sender lateness was 2.652 ms. KV charged peaked at 15,269 tokens, committed KV at 15,090; reserved and committed counters returned to zero. All 16 sequence slots returned, and requests, sequences, tickets, queues, and stream buffers drained. Shutdown was clean.

Host RSS was 364,628 KiB at start and 658,448 KiB at peak/end. The final-third median was 46,728 KiB above the warm-up-third median, within the frozen 65,536 KiB allowance, but RSS continued rising (about 3.5 MiB/min overall and 4.8 MiB/min in the final five minutes); a plateau is not established. GPU memory was 2,021 MiB at start and 2,313 MiB at peak/end, near that level for roughly the final ten minutes. The soak ran the recovered `7f27cf33` server binary; its SHA-256 and provenance limitation are in `artifacts/release-v1/soak/source-binding.json`. The raw build did not attest worktree cleanliness. Current runtime hardening has directed regression and sanitizer coverage, but did not receive a second sustained CUDA soak. Gate C is based on this resource-soak/accounting evidence and current-source tests, not the comparative performance campaigns. It does not establish absence of slow RSS growth, arbitrary-load safety, physical KV protection, or SA-WDS/adaptive endurance.

The first soak attempt remains `INVALID_HARNESS_INTERRUPTED` and excluded. Compact resource data, raw hashes, and the plotted timeline are linked from [resource soak evidence](RESOURCE_SOAK.md) and `artifacts/release-v1/resource-soak-index.json`.

## Release campaign and thermal evidence

Revision 3 of `docs/RELEASE_CAMPAIGN_PLAN.md` was hashed before collection. It specified the 16 RPS primary, the sole 12 RPS fallback, 8 RPS mixed workload, trial counts, 700/200 offers, paired seeds, randomized order, 65 C maximum start after cooldown, ≤5 C paired-start spread, 85 C stop, sender-lateness limit, and paired-trial analysis. The stop and validity rules were not relaxed. The current campaign summary includes 156 planned treatment arms:

| Campaign | Arms | Runner-valid | Analyzer-valid standalone | `INVALID_THERMAL` | `INVALID_CONFIG_MISMATCH` | Eligible B/C/D pairs |
|---|---:|---:|---:|---:|---:|---:|
| 16 RPS interactive primary, `20261004T101418Z` | 48 | 4 | 3 | 44 | 1 | 0 |
| 12 RPS interactive fallback, `20261004T104044Z` | 48 | 0 | 0 | 48 | 0 | 0 |
| 8 RPS mixed SLO, `20261004T111143Z` | 60 | 1 | 1 | 59 | 0 | 0 |
| **Total** | **156** | **5** | **4** | **151** | **1** | **0** |

The 16 RPS trial-1 FIFO/Static/Adaptive arms were individually accepted by the runner but started at 47/61/59 C, a 14 C span above the frozen 5 C pairing limit. A separate Adaptive singleton had a GPU power-limit trace of 40→55→100 W against the reported 100 W default. The hardened analyzer checks every telemetry sample and classifies that arm `INVALID_CONFIG_MISMATCH`; it is excluded from valid standalone summaries and inference. Its FIFO and Static partners are thermally invalid. Five other primary arms with non-default or changing limits remain classified `INVALID_THERMAL`; those raw classifications were not changed. The mixed FIFO singleton has no valid partners. Earlier 24 RPS, 16 RPS, and 8 RPS thermal-aborted attempts remain separately indexed and invalid.

The analyzer accepted four standalone arms, found zero comparison-eligible treatment sets, and emitted zero paired effects and zero confidence intervals. The valid standalone arm with the largest interactive sample had 681 TTFT observations; the headline threshold is 10,000 per treatment, and the configured paired bootstrap requires at least seven valid pairs. No release-grade scheduler speedup, performance interval, or matched treatment effect is claimed. Prompt-2 numbers remain development evidence only. These thermal and sample-size outcomes do not block the software release under the revised policy. The RTX 4060 Laptop sustained-thermal limitation is explicit; a matched B/C/D campaign on a better-cooled or dedicated benchmark host remains optional publication-grade future work.

Open-loop intended arrivals remain timestamp-driven and sender lateness is checked. Raw runs and `INVALID_THERMAL` classifications are retained and indexed. The analyzer refuses unmatched, invalid, or configuration-mismatched inference. See [benchmark results](BENCHMARKS.md), [method](BENCHMARK_METHODOLOGY.md), and `artifacts/release-v1/benchmark/final-analysis-summary.json`.

## Final Gates A–L

| Gate | Status | Evidence and limitation |
|---|---|---|
| A — Architecture ownership | PASS | Direct pinned libllama runtime; InferScale owns request lifecycle, admission, and scheduling; llama.cpp owns physical KV. |
| B — Lifecycle and concurrency | PASS WITH LIMITATION | RequestKey generation fencing, BatchTicket ownership, cancellation, sequence reuse, and cleanup regressions pass. Fatal-state issue prevention and full-request hard-cap checks are included. The generation-history map grows with unique accepted IDs; idle-process signal injection and forced hard-shutdown timeout are not covered. |
| C — Resource safety | PASS WITH LIMITATION | Hard-cap tests, current-source regressions/sanitizers, and the dedicated soak show zero observed accounting/capacity violations, OOM markers, or post-drain request/sequence/lease state. The soak ran the recovered `7f27cf33` binary; current runtime hardening has no later sustained CUDA soak. RSS rose within the frozen allowance and has no established plateau. This status is based on resource evidence, not comparative performance thermals. |
| D — Scheduler | PASS WITH LIMITATION | FIFO, SA-WDS, chunking, and controller invariants are tested. Prompt-3 paired fairness/performance evidence was not obtained; no policy effect is claimed. |
| E — API and backpressure | PASS WITH LIMITATION | Current CPU/HTTP tests cover parsing, bounds, queues, stream backpressure, and cancellation; model-backed slow-reader recovery exists from an earlier source revision and was not rerun against current source. |
| F — Failure and recovery | PASS WITH LIMITATION | Recoverable, restart-required, fatal, and sequence-clear paths are tested; automatic engine restart and forced hard-timeout testing remain unsupported/unrun. |
| G — Memory, thread, and tooling | PASS WITH LIMITATION | ASan/UBSan pass 3/3; host-capable LSan-enabled suite passes 3/3; supported TSan passes 3/3; lifecycle/parser evidence and operational drain evidence are healthy. TSan needs an ASLR workaround and does not model Boost.Asio `atomic_thread_fence`; the sandboxed LSan ptrace failure remains documented. |
| H — Benchmark integrity | PASS WITH LIMITATION | Open-loop method, frozen validity rules, raw indexing, invalid-run retention, per-sample GPU power validation, and fail-closed paired analyzer are verified. All 151 current `INVALID_THERMAL` arms and the one configuration-mismatched arm remain excluded. No matched comparison or inferential output exists. |
| I — Performance evidence | PASS WITH LIMITATION | Prompt-2 data remains development-only. No release-grade scheduler speedup, paired effect, interval, or final comparative p99/goodput/throughput/rejection claim is made. README and resume evidence state that a thermally matched comparison was not obtained. |
| J — Profiling discipline | PASS | Prompt-2 NVTX/Nsight conclusion remains valid for the unchanged inference hot path; the negative optimization result is preserved. |
| K — Repository and reproducibility | PASS WITH LIMITATION | Clean current-source CUDA/Ninja build, CPU CTest 3/3, Python 42/42, CLI checks, and bounded current-source GPU smoke pass. Hosted CI was not run; raw campaign logs remain local and indexed by hash. |
| L — Resume and claim integrity | PASS WITH LIMITATION | Quantitative claims include action, system context, result, tradeoff, and evidence path. Prompt-2 is development-only, invalid runs remain excluded, and no Prompt-3 scheduler effect is claimed. |

All required gates meet the revised software-release condition as PASS or PASS WITH LIMITATION. This tag does not substitute for publication-grade thermally controlled performance evidence. No 85 C-aborted run is reclassified or included in inference.

## Claude review checkpoints

Reviews, material findings, dispositions, and controlled-retry outcomes are recorded in `docs/CLAUDE_REVIEW_LEDGER.md`. Sonnet's final diff review was unavailable: the normal firstParty call failed before inference with `EAI_AGAIN` and the one host-capable retry stopped before inference on an OAuth refresh race; both had zero token usage. The final Opus blocker review found one evidence-provenance defect. The bounded follow-through corrected all eight malformed historical commit ID occurrences across the manifest, test summary, resource-soak index, clean-worktree summary, and command record to the verified source commit, then refreshed the affected index hashes. All indexed evidence hashes pass with no missing or mismatched files. The review found no other material blocker. Claude availability does not replace or alter objective gate results.

No Anthropic pay-as-you-go route, API key, credential change, global sandbox change, or remote publication was used.
