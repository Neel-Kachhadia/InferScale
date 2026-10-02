# Prompt-1 gate record

Statuses below use the required vocabulary. `PASS WITH LIMITATION` records a completed gate whose scope or present-session evidence access has a stated ceiling. The real-model runs, GPU telemetry, build logs, and raw sweep JSONL remain locally under ignored `build/`; the checked-in baseline report is the compact reproducible summary.

The repository started at `d37f931` on `phase1-2/foundation-runtime`, with no tags and only the pre-existing untracked `graphify-out/`. The InferScale GPU FIFO endpoint was required for comparison B, so the implementation and its measurements were completed before formal milestone tagging. The M1 tag therefore includes the combined Prompt-1 runtime implementation; history is preserved and is not rewritten. Tag rows below are `NOT RUN` until their refs are created and recorded in a follow-up evidence commit.

| Gate | Status | Evidence / limitation |
|---|---|---|
| REPOSITORY BASELINE | PASS | Root `/home/neel/Projects/InferScale`; branch `phase1-2/foundation-runtime`; starting commit `d37f931`; no pre-existing tags. |
| ENVIRONMENT MANIFEST | PASS WITH LIMITATION | [environment.json](environment.json) records the verified host, model, pin, compiler, and build. At final recheck the NVIDIA module remained loaded but this tool session exposed no `/dev/nvidia*` nodes and `nvidia-smi` could not communicate; all CUDA runs and telemetry were captured earlier while GPU access was available. |
| DEPENDENCY PINNING | PASS | llama.cpp v0.4.1, commit `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`; CMake verifies the exact checkout before building the external reference. |
| EXTERNAL LLAMA-SERVER BUILD | PASS | Built unmodified pinned server with Release/Ninja, `GGML_CUDA=ON`, server/tools on, tests/examples/UI off; build command and resolved `89-real` architecture are in [BASELINE.md](BASELINE.md). |
| EXTERNAL LLAMA-SERVER REAL MODEL RUN | PASS | Same verified GGUF, CUDA offload, real completion, then clean shutdown; effective command/settings in ignored `build/reference-command.txt` and `build/reference-run-metadata.txt`. |
| DIRECT LIBLLAMA PROOF | PASS | `inferscale_llama_smoke MODEL "The capital of France is" 99` loaded, tokenized, prefetched, sampled ` Paris.`, cleared sequence, destroyed context/model, and exited 0. Logs report 23/23 layers on RTX 4060 CUDA0. |
| CUDA BUILD | PASS | `cmake --preset cuda-release -DFETCHCONTENT_SOURCE_DIR_LLAMA_CPP="$PWD/build/llama-source"`; build targets `inferscale_llama_http_tests inferscale_llama_tests inferscale_llama_server inferscale_runtime_tests inferscale_http_tests` with `-j2`; Release, `GGML_CUDA=ON`, CMake auto `89-real`. |
| REAL GPU OFFLOAD | PASS WITH LIMITATION | Direct and InferScale logs report RTX 4060 CC 8.9 and 23/23 layers offloaded; concurrent-run samples reached 95–96% reported GPU utilization. Final read-only device-node access was unavailable in this tool session. |
| LOAD GENERATOR | PASS | `bench/sweep.py` supports closed-loop and precomputed fixed-rate open-loop, seeded Poisson, and burst schedules; bounded queue/raw JSONL and run IDs recorded. |
| OPEN-LOOP TIMING VALIDATION | PASS | All eight baseline runs had zero raw drops and maximum sender lateness 31.83 ms against a 100 ms validity bound; TTFT/E2E anchor to intended arrivals. |
| RAW EVENT SCHEMA | PASS | Raw request lifecycle, intended/send/accept/token/finish timing and response/error fields retained in per-run JSONL under ignored `build/prompt1-baseline/`. |
| STATISTICS VALIDATION | PASS | Analyzer validates fields and reports p50/p90/p95/p99, failures, rates, SLO goodput, and null token rates where usage is incomplete; Python tests pass. |
| BASELINE SATURATION SWEEP | PASS WITH LIMITATION | Same model/host/GPU/workload/seed and 80 requests per point at 2/8/24/64 offered rps for external llama-server and InferScale FIFO. One seed is practical reference evidence, not a stable research p99 or scheduler-causality claim; InferScale rejects overload at 24/64 rps. Full table and run IDs in [BASELINE.md](BASELINE.md). |
| RUNTIME REACTOR | PASS | Single-owner reactor remains the only writer of request state, scheduling, allocation, and cleanup. Runtime and deterministic race tests pass. |
| REQUEST STATE MACHINE | PASS | Admission, prefill/decode, terminal-pending, cancellation/deadline, ticket retirement, backend clear, and exactly-once release covered by runtime tests. |
| GENERATION FENCING | PASS | Old generation completion is ignored; model-backed sequence reuse increments generation only after prior ticket retirement and acknowledged clear. |
| FIFO SCIENTIFIC CONTROL | PASS | Deterministic FIFO WorkPlan scheduling, with no hidden priority/admission in BatchBuilder. Runtime tests pass. |
| BATCH BUILDER | PASS | Direct pinned `llama_batch` inspection verifies WorkItem translation, exact positions/sequence IDs, logits flags, and logits mapping; two builder tests pass. |
| LLAMA ENGINE | PASS WITH LIMITATION | Production pinned backend initializes, loads, tokenizes, decodes, samples, renders pieces, checks EOG, clears sequence memory, and translates failures. Scope is decoder-only, greedy, and max 128 output tokens per request. |
| REAL WORKPLAN-TO-LLAMA EXECUTION | PASS | Runtime issues BatchTicket through the normal executor path; BatchBuilder creates pinned `llama_batch`; `llama_decode` results return through `BatchCompletedEvent` to the reactor. Real-model integration test passes. |
| BATCH TICKET OWNERSHIP | PASS | Exact issued ticket ID and entries are matched before retirement; cancellation/stale output retains pins until completion and clear. FakeEngine barriers and real backend runtime test pass. |
| SEQUENCE ALLOCATOR | PASS | Backend sequence IDs are held through ticket retirement and successful llama memory removal; same slot is reused only after matching clear acknowledgement. |
| REAL TOKENIZATION | PASS WITH LIMITATION | Pinned llama vocabulary tokenization handles supplied prompt and BOS options. HTTP accepts plain prompt text; chat template is caller-supplied. |
| REAL TOKEN PIECES | PASS | Real variable-length token pieces are appended with per-sequence incomplete-UTF-8 carry; direct model output is non-empty and valid text. |
| PER-REQUEST EOS | PASS | EOG is checked for the owning sequence; concurrent model requests completed at different natural lengths (3 and 8 tokens) while a third continued. |
| OUTPUT LIMIT | PASS | Requested output lengths 2, 8, and 128 were honored unless EOG occurred earlier; the configured API ceiling is 128. |
| ASYNC SSE | PASS | Existing async Beast streaming integration, strict parser corpus, stream completion and shutdown tests pass. |
| MODEL-BACKED SSE | PASS | Real TinyLlama text streamed over `/v1/completions`; usage and exactly-one `[DONE]` validated in server E2E records. |
| STREAM BACKPRESSURE | PASS | Reactor stream high-water suppresses decode scheduling; writable notifications resume progress; executor does not wait on socket writes. |
| MODEL-BACKED BACKPRESSURE | PASS WITH LIMITATION | Real-model reactor test filled a 186-byte test buffer to the observed 98-byte high-water, paused/resumed four times, bounded occupancy, and resumed to 128 tokens. Slow HTTP reader reached the production 5 s write timeout with test-only 186-byte stream/1024-byte socket buffers, then a later request succeeded. |
| DEADLINES | PASS | Reactor checks deadlines before batch issue and on completion; deterministic runtime tests cover expired/in-flight work and terminal cleanup. |
| CANCELLATION | PASS | Cancellation enters the reactor, prevents future work for the key, retires already-issued tickets, suppresses stale output, and delays clear/reuse until safe. FakeEngine race suite passes. |
| MODEL-BACKED CANCELLATION | PASS | Client disconnected during real long prefill; cancellation retired safely and a subsequent real request completed on the same engine. |
| FAKE ENGINE | PASS | Retained deterministic barriers, randomized lifecycle/model tests, and cancellation/ticket race coverage. |
| ERROR CLASSIFIER FOUNDATION | PASS WITH LIMITATION | Pinned decode/clear errors map into the existing request/engine/restart/process taxonomy; clear failure quarantines sequence and marks engine unhealthy. Full restart and process-fatal campaigns remain later-phase work. |
| GRACEFUL SHUTDOWN | PASS | Stop acceptance, quiesce runtime, retire/clear ownership; bounded 10 s guard. CPU and HTTP shutdown tests pass. |
| MODEL-BACKED SHUTDOWN | PASS | SIGTERM after a real streamed piece closed the connection, retired in-flight state, exited in 0.061 s, and returned GPU allocation to the idle level. |
| UNIT TESTS | PASS WITH LIMITATION | Runtime GoogleTests 28/28 and CPU BatchBuilder tests 2/2 pass; the configured real-model run added its third llama integration test. Python unittest ran 10 cases; five passed and five loopback cases were skipped by the final tool-session sandbox (the open-loop baseline itself ran successfully). |
| INTEGRATION TESTS | PASS WITH LIMITATION | Earlier CUDA CTest completed 5/5 (`runtime`, `http_server`, `llama_runtime`, `llama_cli_help`, `llama_http_slow_reader`) and the CPU HTTP/parser/body-bound/fatal-drain/shutdown tests passed directly. A final rerun in this tool session could not open loopback sockets (HTTP test exit 77; five Python socket integrations skipped by the sandbox). |
| MODEL/RANDOMIZED TESTS | PASS | 32 seeded randomized FakeEngine lifecycle runs pass; real-model tests cover single, concurrent unequal output lengths, EOG, limit, cancellation, backpressure and generation reuse. |
| FUZZ SMOKE | PASS WITH LIMITATION | Strict HTTP parser corpus plus 128 deterministic fuzz-smoke inputs pass; this is a bounded smoke, not a long-running coverage-guided campaign. |
| ASAN | PASS WITH LIMITATION | Earlier combined ASan/UBSan CTest completed 2/2 with no findings. A final direct instrumented runtime rerun executed all 28 tests successfully but LeakSanitizer then exited fatally with its ptrace limitation; no test assertion or sanitizer diagnostic preceded teardown. |
| UBSAN | PASS | Combined ASan/UBSan build and CTest completed 2/2 with no findings. |
| TSAN-COMPATIBLE SUITE | PASS WITH LIMITATION | Default TSan runtime aborted before `main` with `unexpected memory mapping`; per-process `setarch x86_64 -R ctest --test-dir build/tsan --output-on-failure` ran runtime and HTTP suites 2/2 (28 runtime tests). No global ASLR setting changed; Boost.Asio reports its known fence-model warning. |
| REAL GPU E2E | PASS WITH LIMITATION | Direct proof plus model-backed single/concurrent SSE, variable output/EOG/limit, real cancellation, safe sequence reuse, saturated backpressure, and active shutdown all ran with CUDA0/23-layer RTX 4060 offload. Final tool-session device nodes were unavailable after these runs. |
| SECURITY/HYGIENE | PASS | High-confidence credential scan found no key/token patterns in source/docs; Git contains no build tree or GGUF; raw sweep/model/build data are excluded by `.gitignore`; existing untracked `graphify-out/` is preserved and uncommitted. |
| DOCUMENTATION | PASS | Backend pin, host/model hashes, ownership, SSE, cancellation, backpressure, baseline method, constraints, and tests are recorded in this document and linked docs. |
| MILESTONE-1 TAG | PASS | Annotated `milestone-1-baseline` points to `030c7b9`; real-model A/B sweep, external CUDA reference, and direct GPU libllama proof are recorded above. |
| MILESTONE-2 TAG | NOT RUN | Create annotated tag only after the safe-runtime implementation, tests, and review are committed. |
| GIT DIFF CHECK | PASS | `git diff --check` completed clean; the complete staged change set will be checked again before commit. |

## Baseline context

The external endpoint completed all requests; its throughput remained around 20–21 requests/s between 24 and 64 offered requests/s while p99 TTFT increased. InferScale FIFO returned 35 and 56 HTTP errors at 24 and 64 offered requests/s because its 16 active-sequence limit is explicit. The two endpoints are practical system references; this comparison does not isolate scheduler causality. GPU samples showed peak utilization 95% external and 96% InferScale; maximum sampled temperatures were 69 C and 76 C. Thermal-throttle state was not measured.

## Review and known limits

GPT-6.1 Sol High reviewed the pinned API/batch and token semantics; GPT-6.1 Sol Max reviewed ownership, cancellation, reuse, and shutdown. GPT-6 Luna Max implemented and exercised the repository work. FirstParty Claude Sonnet 5.5 High reviewed the production path and found no material required fixes. Its supported future-work findings are documented in [ARCHITECTURE.md](ARCHITECTURE.md): generation-history entries grow with unique request IDs, tokenization shares a mutex with decode, and over-context/admission failures currently map to HTTP 429. Unproven or out-of-scope suggestions were not treated as required fixes. No Opus review was needed.

The initial Claude review request failed before inference with provider DNS `EAI_AGAIN`; the same single focused request succeeded on retry through the authorized FirstParty Claude Pro route. No Anthropic PAYG API was used.
