# Prompt-3 final validation and release gate record

Recorded for the local `phase5/final-validation-release` candidate on
2026-10-03. The candidate is **not eligible for `v1.0-resume-grade`**: the
predeclared sustained GPU campaigns were invalidated by the 85 C thermal stop,
and the required soak was not run. No release tag was created.

## Repository and frozen inputs

Repository root: `/home/neel/Projects/InferScale`.

| Item | Verified value |
|---|---|
| Prompt-3 starting commit | `0b2c198d9138314e5f75ce9e68b66856e412c095` |
| Candidate branch | `phase5/final-validation-release` |
| Campaign harness / power telemetry commit | `6dd775a7073724862b2fd399865a3237a2353a8d` |
| Final benchmark validity hardening commit | `27963bcd41665ad71f59950084dc49a7a816e246` |
| Headline TTFT population fix commit | `26a5cce221c6114e1a631315e0e147e153fe766c` |
| Direct adaptive/static comparison fix | `887bb0b70c56aaa05326526c1b12c2836d40eab2` |
| Clean reproduction commit | `887bb0b70c56aaa05326526c1b12c2836d40eab2` |
| M1 tag commit | `030c7b9dcba4019f445dec1b3f1a92af8a134cc7` |
| M2 tag commit | `517b95e8235f2178276097a4a05cd081aa8cb645` |
| M3 tag commit | `935d811b3fff52cef789205243b0046720bdaa77` |
| M4 tag commit | `04712f5f3cd58bf8cebe07f5854f8a3ae95a751c` |
| llama.cpp | v0.4.1, `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4` |
| Model | `TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf`, 668,788,096 bytes |
| Model SHA-256 | `9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0` |
| Host | Linux kernel `7.0.0-38-generic`; i9-13980HX, 32 logical CPUs; 15.23 GiB (16.35 GB) RAM |
| GPU | NVIDIA GeForce RTX 4060 Laptop GPU; 8,188 MiB; compute capability 8.9 |
| Driver / CUDA | `580.178.04` / CUDA 13.0; `nvcc 13.0.88` |
| Compiler / build | GCC 13.3.0; CMake 3.28.3; Ninja 1.11.1; Python 3.12.3 |
| Direct C++ dependencies | Boost 1.83.0; GoogleTest 1.14.0; nlohmann/json 3.11.3 |
| Python dependencies | `httpx==0.28.1`; `PyYAML==6.0.1` |

Runtime configuration resolved in the CUDA smoke: 16 sequences, context 2,048
per sequence, batch 2,048, ubatch 2,048, all model layers on CUDA, and 29,491
logical hard tokens. KV accounting is logical/conservative; llama.cpp owns
physical KV. The recommended default remains FIFO. SA-WDS is opt-in; adaptive
prefill is opt-in.

Frozen SLO values are interactive TTFT ≤750 ms, interactive scheduler-ready
wait p99 ≤500 ms, interactive E2E ≤8 s, and a 5 s batch starvation alarm. The
batch threshold is an operational alarm, not a deadline guarantee.

## Build, test, and reproduction evidence

| Check | Status | Evidence |
|---|---|---|
| CUDA build | PASS | Existing fresh Ninja Release build `/tmp/inferscale-phase5-cuda`, CUDA 13, pinned backend. |
| Clean worktree build | PASS | Detached worktree `/tmp/inferscale-release-check-887bb0b` at `887bb0b70c56aaa05326526c1b12c2836d40eab2`; fresh CUDA configure/build succeeded. |
| Compiler warnings | PASS WITH LIMITATION | Build succeeds. Existing `-Wmissing-field-initializers` warnings occur in aggregate test/runtime setup; pinned llama.cpp emits a `-Wshadow` warning. Warnings are not treated as errors; no new warning specific to the release harness was identified. |
| CPU/FakeEngine CTest | PASS | Clean reproduction ran `runtime`, `policy`, `http_server`: 3/3. |
| Model-backed CTest | PASS WITH LIMITATION | Earlier CUDA suite ran `runtime`, `policy`, `http_server`, `llama_runtime`, `llama_cli_help`, `llama_http_slow_reader`: 6/6. Longer model-backed scenarios were not repeated in the clean-worktree run after thermal stops. |
| Python tests | PASS WITH LIMITATION | Current-source `python3 -m unittest discover -s tests -p 'test_*.py'`: 36 tests, 7 loopback-dependent tests skipped by the sandbox; the updated HTTP integration CTest runs separately with local networking enabled. |
| Randomized lifecycle | PASS | 256 deterministic seeds, up to 64 operations per seed, up to eight requests; assertions check bounds, plans, credits, tickets, lease totals, and final cleanup. |
| Parser fuzz smoke | PASS WITH LIMITATION | HTTP FakeEngine integration ran 2,048 deterministic trailing-garbage mutations plus strict-field/body-bound cases. This is not coverage-guided fuzzing. |
| ASan | PASS WITH LIMITATION | Current-source CTest 3/3 in `/tmp/inferscale-prompt3-asan`, `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`; address instrumentation passed, but leak checking was disabled for the separately documented host limitation. |
| UBSan | PASS | Current-source ASan/UBSan CTest 3/3 with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. |
| TSan | PASS WITH LIMITATION | `setarch x86_64 -R ctest --test-dir /tmp/inferscale-prompt3-tsan --output-on-failure`: runtime, policy, HTTP 3/3, including 256-request cancellation/drain. GCC notes Boost.Asio `atomic_thread_fence` is not modeled. |
| LSan | BLOCKED | Current-source CTest with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` failed at teardown with `LeakSanitizer does not work under ptrace`; no LSan PASS is claimed. |
| CI | PASS WITH LIMITATION | Workflow covers CPU CTest, Python/schema/config checks, ASan/UBSan, and detect-secrets. Equivalent local checks passed. GitHub Actions itself was not run in this session; no GPU CI is claimed. |
| Secret scan | PASS WITH LIMITATION | Full tracked-file `detect-secrets-hook` passes with an audited baseline. It records 153 reviewed nonsecret matches: 144 new artifact checksum/commit/path/label false positives and nine pre-existing source pin matches. No credentials or model weights were found or committed. |
| Clean reproduction | PASS | `scripts/reproduce_release.sh` completed from the detached clean worktree: CUDA configure/build, CPU CTest 3/3, ASan/UBSan 3/3, Python 26/26, schema/workload checks, statistics self-check, and guarded GPU smoke. |

The latest clean smoke manifest, raw files, and analysis are retained under
`artifacts/release-v1/reproduction/clean-smoke/20261003T162240Z`. It accepted
8/8 offers, started at 57 C, peaked at 59 C, and had 2.424 ms maximum sender
lateness. Analysis recorded one valid harness run and zero
policy-comparison-eligible runs. It is a reproducibility check, not a
performance result. The clean analyzer now emits the direct adaptive-versus-
static comparison as well as both FIFO comparisons when paired release rows
exist; the smoke has no policy effect estimate. Earlier clean smokes remain
archived beside it.

## Failure and recovery matrix

| Failure or path | Injection / test | Observed behavior | Status |
|---|---|---|---|
| Disconnect before admission | No dedicated HTTP test that closes before runtime admission | Not separately exercised | NOT RUN |
| Disconnect while waiting | Runtime cancellation-before-submit and in-flight cancellation tests; model-backed cancellation closes client during prefill | No later request work; issued ticket remains pinned until retirement and clear; subsequent model request succeeds | PASS WITH LIMITATION |
| Cancel during prefill chunk | `cancellation_between_prefill_chunks_stops_later_ranges`; `cancellation_inflight_prefill_keeps_ticket_pinned_without_cursor_commit` | No later chunk; cursor does not commit early; ticket/sequence remain pinned | PASS |
| Cancel during decode / stale output | `cancellation_inflight_fences_stale_generation_and_discards_output`; FakeEngine barrier | No future scheduling after reactor event; stale output is discarded | PASS WITH LIMITATION |
| Stale generation after slot reuse | `cancellation_fences_stale_generation_and_discards_output`; duplicate-completion test | Old generation cannot mutate the active key or retire a newer ticket | PASS |
| Queue overflow | `bounded_queue_is_fifo_and_rejects_overflow`; concurrent producer test | Bounded explicit rejection; FIFO order retained | PASS |
| Oversized body / strict JSON | `http_server`; 65 KiB body plus malformed, unsupported, and trailing-data requests | Body gets 413; invalid envelope gets 400 before engine work | PASS |
| Context too large | `impossible_context_is_rejected_before_sequence_allocation` | Rejected before sequence allocation | PASS |
| Soft pressure | `SoftWatermarkRestrictsNewAdmissionWhileExistingGrowthCanUseHardHeadroom` | New admission follows soft bound; already admitted work can use authorized hard headroom | PASS |
| High pressure / hard growth denial | `ReserveCommitAndPressureBoundariesAreExact`, `GrowthCanUseOnlyActuallyUnchargedHardHeadroom`, hard-stall victim test | Pressure is visible; no overcommit; priority cannot bypass hard capacity | PASS |
| Deadline expiry / urgency | `deadlines_are_rechecked_at_ticket_issue_and_completion`; `deadline_prevents_submission_and_shutdown_releases_waiters`; `AgingAndDeadlineOrderingAreStableWithinAClass` | Expired request is terminal; among non-expired same-class candidates, the nearer frozen deadline bucket is selected first | PASS |
| Slow reader | `llama_http_slow_reader`, model-backed | Five-second socket write timeout; bounded test buffer; later request completes | PASS |
| Counter saturation | `KvBudgetTest.TelemetrySaturatesWithoutBlockingCapacityRelease` | Saturating cumulative release telemetry does not prevent a lease from releasing capacity; benchmark recorder-drop behavior is tested and reported separately | PASS |
| SIGTERM while idle | No separate idle-process signal test in Prompt 3 | Not separately exercised | NOT RUN |
| SIGTERM with waiting work | Runtime shutdown-with-waiters test exists; no dedicated process SIGTERM waiting test | Runtime releases waiters; process-signal variant not separately injected | PASS WITH LIMITATION |
| SIGTERM with in-flight model work | Prompt-1 frozen `MODEL-BACKED SHUTDOWN` evidence; no Prompt-3 repeat | Connection closes, in-flight state retires, GPU allocation returns to idle level; retained Prompt-1 local evidence is documented in `docs/BASELINE.md` | PASS WITH LIMITATION |
| Hard shutdown timeout fallback | 10 s hard stop is implemented; timeout was not deliberately forced | Exit-124 fallback not fault-injected | NOT RUN |
| Backend request-recoverable error | `recoverable_engine_failure_retires_and_clears`; status classifier test | Request fails, ticket retires, sequence clear succeeds, capacity returns | PASS |
| Engine-restart-required error | `fatal_backend_status_is_not_hidden_by_cancellation`; `EngineRestartRequired` injection | Runtime becomes unhealthy/not ready; external service/process restart is required; no automatic engine restart is claimed | PASS WITH LIMITATION |
| Process-fatal classification | `fatal_engine_failure_closes_waiting_requests_before_shutdown`; HTTP FakeEngine fatal drain | Admitted streams close and health/readiness return 503 in controlled test; HTTP process itself remains alive | PASS WITH LIMITATION |
| Unknown growth accounting | `unknown_growth_stays_charged_until_successful_sequence_clear` | Charge remains until successful backend clear acknowledgement | PASS |
| Sequence clear failure | `sequence_clear_failure_quarantines_resource_and_stops_admission` | Slot and lease remain quarantined, admission stops, and clean shutdown remains incomplete; process exit-124 fallback is not fault-injected | PASS WITH LIMITATION |
| 24 RPS thermal-aborted attempt | Earlier partial campaign `20261003T132616Z` | Reached thermal stop before a completed trial | INVALID / EXCLUDED |
| 16 RPS headline attempt | `20261003T142113Z`, randomized first treatment FIFO | Started at 49 C, reached 85 C after 63 GPU samples; campaign index has zero completed rows | INVALID / EXCLUDED |
| 8 RPS mixed attempt | `20261003T142325Z`, randomized first treatment adaptive | Started at 53 C, reached 85 C after 52 GPU samples; campaign index has zero completed rows | INVALID / EXCLUDED |
| Sustained soak | Not run after the sustained GPU campaigns reached 85 C | No soak-based RSS/VRAM/lease/sequence drift claim | BLOCKED |

Automatic engine restart is not implemented. `EngineRestartRequired` and
`ProcessFatal` stop runtime admission and return unhealthy readiness; an
external supervisor/operator must restart the process. Unknown backend errors
map conservatively to `ProcessFatal`.

## Final exit table

| Area | Status | Evidence / reason |
|---|---|---|
| Repository baseline and milestone tags 1–4 | PASS | Exact peeled commits above; history and tags preserved. |
| Final branch | PASS | `phase5/final-validation-release`, based on `milestone-4-profiled-optimization`. |
| Environment and model checksum | PASS | Frozen host/toolchain/backend/model recorded above and in run manifests. |
| Backend pin and dependency pins | PASS | Exact llama.cpp commit; direct C++ and Python dependency pins verified. |
| Build and compiler warning policy | PASS WITH LIMITATION | Clean CUDA build succeeds; legacy initializer/backend shadow warnings remain. |
| Unit, FakeEngine, parser, and scheduler tests | PASS | Runtime/policy/HTTP plus 26 Python tests. |
| Model-backed integration tests | PASS WITH LIMITATION | Earlier CUDA CTest 6/6 and model-backed slow reader/cancellation; not repeated after thermal stop. |
| Randomized state campaign | PASS | 256 seeds × up to 64 operations. |
| Fuzzing | PASS WITH LIMITATION | 2,048 deterministic parser mutations; no coverage-guided fuzzer. |
| ASan / UBSan | PASS WITH LIMITATION / PASS | ASan leak detection disabled for the documented host ptrace limitation; supported suite clean. |
| TSan | PASS WITH LIMITATION | Supported targets 3/3 under per-process ASLR workaround; atomic fences are not modeled. |
| Leak/long-run resource stability | BLOCKED | No sustained soak after two runs hit the 85 C stop; LSan teardown is host-limited. |
| Real GPU smoke / single request | PASS WITH LIMITATION | In-tree plus four clean-worktree 8-request smoke trials completed; no sustained campaign inference. |
| Concurrent, long prefill/generation, mixed GPU coverage | PASS WITH LIMITATION | Prior model-backed tests and Prompt-2 mixed-load evidence; no final sustained matrix. |
| Burst, memory-pressure, high-cancellation, and final slow-reader campaigns | NOT RUN | Sustained campaign stopped for thermal validity; no dedicated final samples. |
| Cancellation, stale generation, sequence reuse, deadline | PASS WITH LIMITATION | Deterministic FakeEngine/runtime tests; model-backed prefill cancellation; no separate real decode cancellation. |
| Backpressure / slow reader | PASS | Bounded buffer and real-model slow-reader recovery test. |
| Queue/body/context bounds | PASS | Unit and HTTP integration tests. |
| Hard capacity / lease accounting / pressure | PASS | Boundary, overflow, unknown-growth, pressure, clear, and generation tests. |
| Fairness / starvation | PASS WITH LIMITATION | Deterministic policy coverage and Prompt-2 development measurements; no release-scale results. |
| Adaptive controller / static fallback | PASS WITH LIMITATION | Bounds, hysteresis, cooldown, fallback tests and development ablation; final comparison blocked. |
| Engine recoverable / restart-required / fatal paths | PASS WITH LIMITATION | Controlled FakeEngine injection; external restart policy is documented; no automatic restart. |
| SIGTERM idle / waiting / active / hard timeout | PASS WITH LIMITATION | Prompt-1 active model shutdown and runtime waiter tests are frozen evidence; idle signal and forced 10 s fallback not run in Prompt 3. |
| Runtime event queue bounds | PASS WITH LIMITATION | Queue overflow and completion backpressure have directed tests. The separate benchmark record writer reports drops per request run; the KV counter-saturation test does not exercise that recorder. |
| Security / secret scan | PASS WITH LIMITATION | Detect-secrets hook passes with reviewed nonsecret checksum, commit, path, and label false positives in `.secrets.baseline`; no credential or model weight is committed. |
| Open-loop method / sender lateness | PASS WITH LIMITATION | Method is independent of response completion; latest clean smoke max lateness 2.424 ms under its 100 ms gate. Failed campaigns retained and excluded. |
| Headline sample size / repeated trials / confidence intervals | BLOCKED | No sustained campaign completed; there are zero valid paired Prompt-3 trials and no release CI. |
| Trial ordering / analysis method | PASS WITH LIMITATION | Randomized order, paired seeds and 20,000-resample trial-level bootstrap are implemented and predeclared; a 95% interval requires seven valid pairs, and none were obtained. |
| Thermal validity | PASS WITH LIMITATION | 65 C start / 85 C stop enforced; both sustained trials were invalidated at 85 C. |
| External llama-server reference | PASS WITH LIMITATION | Prompt-1 baseline retained as system context only, not scheduler-causal evidence. |
| FIFO control / static SA-WDS / adaptive SA-WDS | PASS WITH LIMITATION | Implementations and prior same-runtime development comparison pass; Prompt-3 campaign has no valid pair. |
| Chunking / resource / controller ablations | PASS WITH LIMITATION | Prompt-2 development tests/data exist; release-grade ablation blocked. |
| Goodput / p99 TTFT / fairness / cancellation metrics | PASS WITH LIMITATION | Metrics are emitted and prior development evidence exists; no final release-grade effect. |
| Required release figures | BLOCKED | No complete valid paired inputs; exact not-run list retained under `artifacts/release-v1/benchmark/figures/`. |
| NVTX / Nsight / bottleneck / negative optimization | PASS | Prompt-2 profiling remains valid for unchanged C++ hot path; no custom CUDA speedup claim. |
| README / architecture / scheduling / KV / adaptive docs | PASS | Updated and aligned with code. |
| Failure / benchmark / limitation / resume docs | PASS | Current failure model and non-release conclusion are documented. |
| Release manifest / reproduction script | PASS | Machine-readable manifest and clean script retained under `artifacts/release-v1/`. |
| Clean worktree configure/build/test/GPU smoke | PASS | `887bb0b` detached worktree passed clean reproduction and archived its smoke artifacts. |
| GitHub Actions | PASS WITH LIMITATION | Workflow finalized and local equivalents passed; hosted Actions was not run. |
| Gate A — architecture ownership | PASS | Direct pinned libllama; llama-server reference only; docs say backend owns physical KV. |
| Gate B — lifecycle/concurrency | PASS WITH LIMITATION | Generation/ticket/cleanup/cancel/shutdown regressions pass; idle signal and forced hard deadline are not separately injected. |
| Gate C — resource safety | BLOCKED | Hard invariants pass, but required soak/leak trend was not run after thermal stops. |
| Gate D — scheduler | PASS WITH LIMITATION | FIFO/SA-WDS/chunks/controller deterministic tests pass; statistical fairness/performance remains development-only. |
| Gate E — API/backpressure | PASS | Real SSE, strict parser, bounded body/output, disconnect cancellation, health/readiness and slow-reader recovery covered. |
| Gate F — failure/recovery | PASS WITH LIMITATION | Request/fatal/restart-required behavior tested conservatively; no automatic engine restart or forced process hard-timeout test. |
| Gate G — memory/thread/tooling | BLOCKED | ASan/UBSan/TSan-supported tests pass with documented scope, but soak and LSan clean teardown are unavailable. |
| Gate H — benchmark integrity | BLOCKED | Protocol is frozen and invalid raw runs retained; no valid repeated treatment completed. |
| Gate I — performance evidence | BLOCKED | No release-grade B/C/D comparison, paired effects, confidence intervals, or required figure set. |
| Gate J — profiling discipline | PASS | NVTX/Nsight conclusion retained; optimization negative result remains honest. |
| Gate K — repository/reproducibility | PASS WITH LIMITATION | Clean build/test/smoke succeeds; hosted CI and remote publication are not executed. |
| Gate L — resume/claim integrity | PASS WITH LIMITATION | Claims are scoped to historical development evidence; no final Prompt-3 win is claimed. |
| `v1.0-resume-grade` tag | BLOCKED / NOT CREATED | Gates C, G, H, and I are not satisfied. No tag was created or moved. |
| Remote push | NOT RUN | No publication authorization was given. |
| Final diff/status | PASS WITH LIMITATION | Final evidence changes pass `git diff --check`; user-owned `graphify-out/` remains untouched and untracked. |

## Advisory review findings and dispositions

The Frontier wrapper review was completed through the installed Claude Code
CLI, `firstParty` provider, and Claude Pro authentication from a host-capable
execution context. Both native JSON responses were consumed. Initial sandboxed
Opus and Sonnet attempts surfaced `EAI_AGAIN` before a model result and are
classified as execution-context/provider-access failures. Each received one
controlled host-capable retry. No Anthropic API key, PAYG route, or repeated
retry loop was used. Native usage categories remain separate in the current
Frontier usage log: Opus input 2, cache creation 200,609, cache read 535,
output 29,168; Sonnet input 2, cache creation 195,575, cache read 535, output
36,509. The low uncached input count does not describe the total cached
context; no reasoning-token quantity is asserted here.

| Finding | Classification | Disposition / evidence |
|---|---|---|
| Release documentation and smoke evidence were untracked or pointed only into temporary worktrees. | MATERIAL REQUIRED FIX | Clean smoke artifacts and compact release evidence are retained under `artifacts/release-v1/`; clean summary now binds to `887bb0b`. |
| Limitations overstated smoke thermal result and README implied a soak had run. | MATERIAL REQUIRED FIX | `docs/LIMITATIONS.md`, `docs/BENCHMARKS.md`, and README distinguish smoke temperatures and state that the soak was not run. |
| Failure table and randomized-model text overstated Prompt-3 SIGTERM evidence and race concurrency. | MATERIAL REQUIRED FIX | Gate Q now cites frozen Prompt-1 evidence with a limitation; resume text says the 256-seed model serializes events and directed tests cover overlap/fencing. |
| Runner and analyzer could accept treatment or runtime configuration mismatch. | MATERIAL REQUIRED FIX | Commit `27963bcd` compares resolved policy, adaptive state, limits, chunking, reserve and epoch against the requested arm; regression tests cover mismatch. |
| Paired thermal comparison, sample-size, interval, rejection, and smoke eligibility were not enforced tightly enough. | MATERIAL REQUIRED FIX | Plan revision 2 and runner/analyzer enforce paired starts ≤5 C, seven paired trials for 95% intervals, 10,000 interactive TTFT observations per arm for headline p99, rejection reporting, smoke isolation, and a separate analysis output directory. Focused Python tests pass in clean reproduction. |
| Compiler provenance could describe a different CMake source checkout. | MATERIAL REQUIRED FIX | Campaign runner checks `CMAKE_HOME_DIRECTORY` against the source root and records source commit, tracked-diff digest, backend and binary hashes. |
| Soak evidence was requested but the harness was not safely run. | VALID BUT OUT OF SCOPE / FUTURE WORK | No soak result is claimed. Thermal stops are recorded; Gate C/G remain blocked. |
| Prior build/test evidence referenced a temporary clean worktree. | MATERIAL REQUIRED FIX | Current `887bb0b` clean build, tests, and smoke are reproduced and archived; prior clean smokes are separately archived. |
| The review alleged the Prompt-2 scheduler comparison resolved ubatch 512. | UNSUPPORTED BY EVIDENCE | Checked C3 trial logs resolve batch/ubatch 2048/2048; 512 belongs to an older baseline/profile setup. The correction is stated in benchmark and resume evidence. |
| Successful-response p99 can benefit from shedding slow requests. | VALID BUT OUT OF SCOPE / FUTURE WORK | Prompt-2 p99 values remain development evidence; offer-denominator goodput and rejections are shown alongside. No Prompt-3 effect is claimed. |
| Headline p99 N counted interactive offers rather than successful responses with observed TTFT. | MATERIAL REQUIRED FIX | Commit `26a5cce` adds `ttft_observations` to stats and uses it in the analyzer; a regression test proves 8,995 TTFT observations do not satisfy a 10,000 threshold when 10,000 were offered. |
| Analyzer did not emit the required direct adaptive-versus-static comparison. | MATERIAL REQUIRED FIX | Commit `887bb0b` adds that paired effect beside static-vs-FIFO and adaptive-vs-FIFO. The regression checks p99 observed populations and the additional rejection delta. The current dataset still has zero eligible policy rows. |

## Sol High benchmark-methodology checkpoint

The Sol High read-only review first examined source commit `26a5cce`; the
direct-comparison omission it identified was fixed and tested in `887bb0b`.
Its findings are classified here after checking the current harness and tests:

| Finding | Classification | Disposition / evidence |
|---|---|---|
| Trial validity omitted exact count/IDs, recording drops, fresh metrics, resolved configuration, thermal stop, or server provenance. | MATERIAL REQUIRED FIX | `bench/run.py` validates complete unique IDs and counts, zero recorder drops, metrics freshness/coverage, and sender lateness. The campaign runner enforces binary/source/backend hashes, runtime-resolved treatment config, and thermal validity. Regression tests cover the failure cases. |
| Setup and baseline metrics consumed sender-lateness budget. | MATERIAL REQUIRED FIX | The load generator anchors arrivals after client setup and the baseline metrics scrape. `test_metrics_setup_delay_does_not_spend_sender_lateness_budget` passes in the clean 26-test run. |
| Ready-wait p99 was cumulative and could include warm-up/earlier trials. | MATERIAL REQUIRED FIX | The loader subtracts per-bucket histogram counters between baseline and final metrics snapshots; emitted values are explicitly labeled bucketed upper bounds over work-item waits. |
| Failure workloads were rejected by the API limit and collapsed all HTTP outcomes. | MATERIAL REQUIRED FIX | Current `cancel.yaml` and `slow_reader.yaml` request at most 128 tokens; load records preserve HTTP status and error class. Tests retain exact HTTP status and reject incomplete/malformed SSE as success. |
| Output-token throughput could count partial successful usage. | MATERIAL REQUIRED FIX | `bench/stats.py` emits output-token throughput only when usage is complete for every successful response; regression coverage passes. |
| Trial orchestration and paired uncertainty were missing, with no frozen effect statistic or meaningful p99 N. | MATERIAL REQUIRED FIX | Campaign plans record randomized order and paired seeds; analyzer reports median paired effects and 20,000-resample paired-trial bootstrap, requires seven valid pairs for a 95% interval, and 10,000 observed successful interactive TTFTs per arm for headline p99. Regression tests validate the gates. |
| Direct adaptive-versus-static paired effect was omitted. | MATERIAL REQUIRED FIX | Fixed in `887bb0b`; regression verifies direct p99 and rejection effects. There are no valid release pairs from which to estimate it. |
| Wait statistic is not request-level fairness and buckets do not identify an exact 500 ms cutoff. | VALID BUT OUT OF SCOPE / FUTURE WORK | It remains labeled as scheduler-ready wait p99 upper bound over issued work items. Maximum wait and starvation counters are also reported; no real-time guarantee or exact 500 ms quantile claim is made. |
| Zero starvation-event count alone could miss requests still waiting beyond the alarm. | VALID BUT OUT OF SCOPE / FUTURE WORK | Maximum ready wait and class-level wait summaries accompany event counts; final sustained policy campaign did not run, so no starvation performance conclusion is claimed. |
| Warm-up metrics refresh may lag its `[DONE]` event, and thermal-reason fields can be `N/A`. | VALID BUT OUT OF SCOPE / FUTURE WORK | Both are documented in `docs/LIMITATIONS.md` and `docs/BENCHMARK_METHODOLOGY.md`; the final smoke is not used for policy or sustained-thermal claims. |

The itemized Prompt-3 exit ledger is
[`artifacts/release-v1/exit-table.csv`](../artifacts/release-v1/exit-table.csv).

## Advisory model review and provider recovery

The earlier Opus attempt failed before inference with an EAI_AGAIN/network
error. The one controlled retry used the installed Claude Code CLI through
`claude_call.py` from the host-capable execution context, `claude-opus-5-5`,
medium effort, provider `firstParty`, and Claude Pro authentication. It
returned native JSON and its findings were consumed. No Anthropic PAYG key or
fallback was used. Usage was written to
`/home/neel/.codex/usage/frontier-engineering-max-claude.jsonl`; native
categories recorded input, cache creation, cache read, and output separately.
The first party review did not override objective evidence: the 85 C thermal
stops remain release blockers. Sonnet's one high-effort advisory review was
also completed by the same direct firstParty path; its findings and fixes are
listed above. Detailed review input is preserved in
`docs/PROMPT3_SONNET_REVIEW_TASK.md`.
