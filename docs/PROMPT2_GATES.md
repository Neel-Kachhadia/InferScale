# Prompt 2 gate record

Recorded on 2026-10-03 for the `phase3-4/adaptive-scheduling-gpu` branch.
The status vocabulary is `PASS`, `PASS WITH LIMITATION`, `FAIL`, `BLOCKED`,
`NOT RUN`, and `NOT APPLICABLE`.

| Gate | Status | Evidence / limitation |
|---|---|---|
| STARTING MILESTONE-1 VERIFIED | PASS | `milestone-1-baseline` resolves to `030c7b9dcba4019f445dec1b3f1a92af8a134cc7`; tag preserved. |
| STARTING MILESTONE-2 VERIFIED | PASS | `milestone-2-safe-runtime` resolves to `517b95e8235f2178276097a4a05cd081aa8cb645`; branch created from this tag. |
| PROMPT-1 SMOKE REGRESSION | PASS | Final CUDA CTest includes real-model runtime and model-backed HTTP slow-reader tests; 6/6 passed. |
| PHASE-3 BRANCH | PASS | Work is on `phase3-4/adaptive-scheduling-gpu`, not `main`. |
| HARD CAPACITY CALIBRATION | PASS WITH LIMITATION | 16 × 2048 resolved logical sequence tokens, 10% margin, 29,491 hard tokens; measured GPU peaks remained 2087–2145 MiB / 8188 MiB. This is logical backend-context calibration, not physical KV measurement. |
| KV BUDGET MANAGER | PASS | `KvBudgetManager` unit, reactor, randomized, and real-GPU paths. |
| KV LEASE | PASS | Generation-owned lease state and exact terminal release tests. |
| RESERVATION | PASS | Bounded prompt + generation allowance + margin; reclaimable soft excess. |
| GROWTH AUTHORIZATION | PASS | Prefill/decode growth is checked before issue against the hard global cap. |
| COMMIT ACCOUNTING | PASS | Successful ticket work commits; uncertain work remains charged through clear. |
| RELEASE ACCOUNTING | PASS | Release follows successful sequence-clear acknowledgement; repeated release is rejected. |
| ACCOUNTING CONSERVATION | PASS | Deterministic resource tests and 32-seed lifecycle/reference campaign assert conservation. |
| PRESSURE STATES | PASS | Normal, soft, high, hard thresholds and admission/scheduler interactions tested. |
| OVERFLOW SAFETY | PASS | Checked/subtraction-based capacity arithmetic and integer-boundary tests. |
| CHUNKED PREFILL | PASS | Exact half-open WorkItem token ranges are used by FIFO and SA-WDS modes. |
| CHUNK BOUNDARIES | PASS | Short, exact, chunk+1, multi-chunk, and final-partial ranges covered. |
| PREFILL CURSOR COMMIT | PASS | Cursor changes only after successful, generation-valid backend completion. |
| CANCEL-DURING-CHUNK | PASS | Before-first, between-chunks, and in-flight cancellation/retirement paths covered. |
| FAILURE-DURING-CHUNK | PASS | Backend failure leaves cursor unadvanced and conservatively reconciles growth. |
| FIFO CONTROL PRESERVED | PASS | FIFO remains available and is the default; same lower-level engine path. |
| SA-WDS | PASS | Separate eligibility, credit, urgency, decode-protection, and resource gates. |
| SCHEDULER DETERMINISM | PASS | Fixed snapshot/config/manual time yields stable plans; shuffled candidates and tie breaks tested. |
| CLASS WEIGHTS | PASS | Interactive 1024 / Batch 256 token credits (4:1) frozen for all treatments. |
| SERVICE CREDIT | PASS | Token-count work cost, issued-work debit, active-class refill, and inactive-class reset tested. |
| CREDIT CAP | PASS | Bounded class credit and idle-class reactivation tested. |
| AGING | PASS | 1 s age quantum, capped at bucket 60; manual-clock tests. |
| DEADLINE URGENCY | PASS | 100 ms discrete slack buckets capped at 600; deadlines cannot override hard safety. |
| DECODE PROTECTION | PASS | 25% decode service reserve plus finite prefill opportunity; decode-only/prefill-only/mixed tests. |
| RESOURCE ELIGIBILITY | PASS | Hard-cap and lifecycle eligibility precede class ordering and urgency. |
| FAIRNESS | PASS WITH LIMITATION | Sustained policy tests show bounded Batch progress and class credit behavior; three-seed real-load evidence is development-grade and raw token share is workload-cost dependent. |
| STARVATION OBSERVABILITY | PASS | 5 s operational alarm, counters and deterministic aged-prefill regression; no measured run crossed the threshold. |
| OVERLOAD CONTROL | PASS | Bounded sequence/KV/queue admission, explicit defer/reject counters, `/healthz` liveness and `/ready` admission state. |
| GOODPUT | PASS | Per-class SLO-completed requests / wall second; rejections remain visible and are not counted as successes. |
| ADMISSION/REJECTION METRICS | PASS WITH LIMITATION | Class/reason counters are bounded; HTTP 429 response body remains generic rather than exposing the internal reason. Final metric polling now waits up to 250 ms for admitted/rejected and completion counters to cover client-visible outcomes. |
| ADAPTIVE PREFILL CONTROLLER | PASS | Reactor-owned controller changes only the prefill epoch budget. |
| CONTROLLER EWMA | PASS | Validated 1/4 TTFT EWMA and bounded backend-idle proxy; absent signals are not fabricated. |
| CONTROLLER HYSTERESIS | PASS | Three consecutive high/low observations required; oscillation sequence tested. |
| CONTROLLER COOLDOWN | PASS | Five-window cooldown and exact one-window edge tested after Sol Max finding. |
| CONTROLLER MIN/MAX | PASS | Comparison bounds 512–2048 tokens; step 256; tested at both bounds and invalid inputs. |
| CONTROLLER STATIC FALLBACK | PASS | Static mode default; absent/invalid inputs return to clamped static budget. |
| SCHEDULER ABLATION | PASS WITH LIMITATION | FIFO vs SA-WDS same-runtime comparison, 3 paired seeds × 80 arrivals; development evidence, not release-grade confidence intervals. |
| CHUNKING ABLATION | PASS WITH LIMITATION | Batch-sized chunks vs 512-token chunks under SA-WDS; 3 seeds; no consistent goodput gain, bounded interleaving preserved. |
| RESOURCE-ADMISSION ABLATION | PASS WITH LIMITATION | Zero vs 64-token soft generation reserve under identical hard authorization; small, seed-sensitive result. |
| CONTROLLER ABLATION | PASS WITH LIMITATION | Static vs bounded adaptive at same workload/config; lower mean interactive TTFT p99 but lower mean goodput and more rejections; static retained as default. |
| MODEL/RANDOMIZED INVARIANTS | PASS | 32 deterministic seeds, up to 64 lifecycle steps per run with eight requests and resource/controller checks after operations; failing seed is reported by the test harness. |
| CANCELLATION REGRESSION | PASS | Runtime and model-backed cancellation, including stale output suppression and in-flight ticket retirement. |
| GENERATION-FENCING REGRESSION | PASS | Stale completion cannot mutate a reused active key. |
| SEQUENCE-REUSE REGRESSION | PASS | Backend sequence remains unavailable until outstanding tickets retire and clear succeeds. |
| BACKPRESSURE REGRESSION | PASS | Bounded stream blocks new decode; slow-reader real-model HTTP test passed. |
| SHUTDOWN REGRESSION | PASS | Fake and real-model shutdown drain tickets/leases and clear sequence state. |
| UNIT TESTS | PASS | C++ runtime/policy/llama tests and Python benchmark/statistics tests. |
| INTEGRATION TESTS | PASS | CUDA CTest 6/6 with `INFERSCALE_TEST_MODEL`; Python suite 12/12 with loopback enabled. |
| ASAN | PASS | CPU/FakeEngine ASan suite; LeakSanitizer disabled because prior runtime tracing teardown was host-limited. |
| UBSAN | PASS | CPU/FakeEngine UBSan suite completed with ASan build. |
| TSAN-COMPATIBLE SUITE | PASS WITH LIMITATION | TSan target compiled; execution stopped at host runtime `FATAL: ThreadSanitizer: unexpected memory mapping`. No host settings were changed. |
| REAL GPU MIXED-LOAD E2E | PASS | Final post-poll C3 run `4b517c61-a092-4d83-80b7-6822382e8431`: 75/80 completed, five explicit interactive rejections, 6.45 SLO-goodput/s, 239.38 output tokens/s, 59.6 ms fresh-metric wait, all leases drained. |
| NVTX | PASS | Runtime, scheduler, resource, admission, batch build/execute, prefill/decode, stream, cleanup, and controller ranges are present. |
| NSIGHT SYSTEMS | PASS | Representative 80-request mixed C4 trace and successful compact CUDA, NVTX, OS-runtime, and projection reports. |
| CPU PROFILE | PASS WITH LIMITATION | `perf` unavailable because matching kernel tools are absent and host `perf_event_paranoid` blocks sampling; Nsight CPU/NVTX/OS runtime timelines used. |
| BOTTLENECK CLASSIFICATION | PASS | Backend/model CUDA execution and synchronization dominate; measured scheduler/batch planning CPU ranges are small. |
| OPTIMIZATION EXPERIMENT | PASS WITH LIMITATION | Profiled batch-plan allocation candidate bounded at 0.036% of backend execution-range time and rejected before adding stateful complexity. |
| OPTIMIZATION CORRECTNESS | PASS | No optimization source patch retained; CUDA CTest and real mixed-load checks validate the profiled final C++ implementation. |
| OPTIMIZATION ABLATION | PASS WITH LIMITATION | No meaningful before/after exists because no optimization was retained; C3/C4 final-tree runs are explicitly not called an optimization pair. |
| PHASE-3 DOCS | PASS | Scheduling, logical KV accounting, adaptive control, benchmark methodology/results, and ADR-003/004/005. |
| PHASE-4 DOCS | PASS | Profile manifest/readout and measured optimization decision in `docs/profiling/`. |
| MILESTONE-3 TAG | PASS | `milestone-3-adaptive-scheduling` created only after the resource, scheduling, controller, and same-runtime evidence gates passed. |
| MILESTONE-4 TAG | PASS | `milestone-4-profiled-optimization` created after representative Nsight profiling and the documented no-retained-optimization decision. |
| SECURITY/HYGIENE | PASS | No model, raw load dumps, Nsight trace, credentials, system files, or host configuration added to Git. Existing `graphify-out/` is preserved and remains untracked. |
| GIT DIFF CHECK | PASS | `git diff --check` clean before final commit/tag. |
| FINAL GIT STATUS | PASS WITH LIMITATION | Tracked files clean; pre-existing untracked `graphify-out/` intentionally preserved. |

## Independent review availability

Sol High reviewed API/accounting/scheduler semantics; Sol Max reviewed
resource/lifecycle correctness. Two requested first-party Claude Pro calls
(Sonnet 5.5 High and Opus 5.5 Medium) were attempted once each and both failed
before returning a review with `EAI_AGAIN` (API DNS/network unavailable).
Those failures were not replayed. No external review result is claimed.
