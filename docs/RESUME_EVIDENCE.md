# Resume and interview evidence

These bullets separate implementation evidence, safety evidence, and policy measurements. Prompt-2 policy results are development evidence. Prompt-3 closed the resource soak but did not produce a thermally matched paired policy comparison. The `v1.0-resume-grade` tag means implementation-complete, thoroughly validated, and reproducible; it does not claim publication-grade thermal performance proof. A matched B/C/D campaign on a better-cooled or dedicated host remains optional publication-grade future work.

## Defensible resume bullets

- **ACTION:** Built a C++20 single-GPU LLM runtime that directly drives pinned libllama and owns HTTP/SSE, request lifecycle, FIFO/SA-WDS scheduling, cancellation, and backpressure. **SYSTEM CONTEXT:** TinyLlama-1.1B Chat Q4_K_M on an RTX 4060 Laptop GPU, llama.cpp v0.4.1 commit b29c606e28a01b1bc8c1351026a0fa6e616bf6c4. **MEASURED RESULT:** Runtime source `850db0c05bc3f2f87a0ad14649b1e35034a4d594` completed a clean CUDA Release build with 256 Ninja steps, CTest 3/3, a bounded model-backed GPU smoke 8/8, and current analysis hardening `86e4f46dbea5249311acecca28b08cf0991355ea` passed Python 42/42 with no skips. **TRADEOFF:** The smoke establishes basic operation, not performance or long-duration GPU endurance; no release-scale throughput or policy win was established. Physical KV allocation remains backend-owned. **EVIDENCE PATH:** docs/REPRODUCTION.md, artifacts/release-v1/reproduction/current-source-validation.json, and artifacts/release-v1/reproduction/current-source-smoke-index.json.

- **ACTION:** Added generation-fenced request keys and ticket-pinned sequence reuse around asynchronous backend work. **SYSTEM CONTEXT:** Runtime and FakeEngine lifecycle tests with cancellation, stale completion, failure, queue pressure, and shutdown ordering. **MEASURED RESULT:** The deterministic lifecycle model exercised 256 serialized seeds with up to 64 state operations and eight requests per seed; separate directed tests inject cancellation with a ticket in flight, stale completion, duplicate completion, and sequence reuse. ASan/UBSan and the supported TSan suite passed. **TRADEOFF:** The randomized model itself does not create overlapping ticket races; TSan cannot instrument Boost.Asio atomic_thread_fence; automatic backend restart is unsupported. **EVIDENCE PATH:** tests/runtime_test.cpp, docs/FAILURE_MODEL.md, docs/PROMPT3_GATES.md.

- **ACTION:** Implemented conservative hard logical resource admission with leases, growth authorization, soft-pressure behavior, chunked prefill, and FIFO/SA-WDS controls. **SYSTEM CONTEXT:** 16 sequences × 2,048 context tokens, 29,491 logical hard tokens, TinyLlama on an RTX 4060 Laptop GPU. **MEASURED RESULT:** The 20.26-minute FIFO resource soak processed 2,462 offers and 2,438 successful requests; committed KV peaked at 15,090 tokens against the 29,491-token hard bound, then all KV counters returned to zero with all 16 sequences available. GPU memory rose from 2,021 to 2,313 MiB and remained at 2,313 MiB for roughly the final ten minutes. RSS peaked at 658,448 KiB; its final-third median was 46,728 KiB above the warm-up-third median, within the frozen 65,536 KiB allowance. **TRADEOFF:** RSS kept rising in the final five minutes (about 4.8 MiB/min), so a host-memory plateau or absence of slow growth is not established; accounting is logical, not physical KV-page management, and the soak covers FIFO at 2 RPS only. **EVIDENCE PATH:** docs/RESOURCE_SOAK.md, artifacts/release-v1/resource-soak-summary.json, artifacts/release-v1/resource-soak-timeline.csv.

- **ACTION:** Evaluated FIFO, static SA-WDS, and adaptive SA-WDS under frozen open-loop campaigns with an 85 C abort rule and fail-closed paired analysis. **SYSTEM CONTEXT:** RTX 4060 Laptop GPU, TinyLlama-1.1B Q4_K_M, pinned llama.cpp; 16 RPS interactive primary, 12 RPS sole fallback, and 8 RPS mixed workload. **MEASURED RESULT:** Across 156 treatment arms, four are analyzer-valid standalone arms, 151 remain `INVALID_THERMAL`, one is `INVALID_CONFIG_MISMATCH`, and zero complete valid pairs or confidence intervals exist. **TRADEOFF:** No Prompt-3 scheduler p99, goodput, throughput, rejection, fairness effect, release-grade speedup, or confidence claim is supported. The laptop's sustained-thermal limitation is explicit; matched B/C/D measurement on a better-cooled or dedicated host remains optional publication-grade work. FIFO remains the conservative default without a measured policy win. **EVIDENCE PATH:** docs/BENCHMARKS.md, artifacts/release-v1/benchmark/final-analysis-summary.json, artifacts/release-v1/benchmark/raw-index.json, artifacts/release-v1/benchmark/run-validity.csv.

- **ACTION:** Profiled the mixed inference path with NVTX, Nsight Systems, and CPU/runtime timelines, then rejected a buffer-reuse optimization hypothesis. **SYSTEM CONTEXT:** Pinned llama.cpp CUDA execution on the tested laptop. **MEASURED RESULT:** Batch-plan construction was about 0.036% of the backend execution range; backend CUDA execution and synchronization dominated. **TRADEOFF:** No custom CUDA speedup is claimed. **EVIDENCE PATH:** docs/profiling/PROFILE_PHASE4.md, docs/profiling/OPTIMIZATION_EXPERIMENT.md.

## Interview stories

### A. Stale completion and sequence reuse

An HTTP request can be cancelled while a backend ticket still owns its sequence. The reactor records terminal intent and stops future scheduling, while the ticket pin and logical charge remain until completion. A stale generation cannot update the current request. Only after ticket retirement, backend synchronization, sequence clear, and matching clear acknowledgement can the sequence be reused. Tests inject cancellation, slot reuse, duplicate completion, and late output.

### B. Long prefill interference

A large prompt is split into exact half-open token ranges. The cursor advances only after successful, generation-valid completion. The scheduler can reconsider interactive work between chunks; a failed or cancelled chunk never causes a gap or double commit. Prompt-2's small ablation did not establish a general goodput improvement, so chunking is described as a bounded interleaving mechanism.

### C. Resource pressure

The hard logical capacity is a safety invariant. Soft admission reserve and pressure signals influence whether new work is accepted or deferred; priority and deadlines cannot exceed the hard cap. Existing work may grow only after per-ticket authorization. Unknown backend growth stays charged until a successful sequence clear. Backend memory remains outside this logical ledger.

### D. Fairness

FIFO remains a transparent control. SA-WDS accumulates bounded class-level token credits, applies aging and deadline buckets only after safety eligibility, and retains finite prefill opportunity under decode backlog. Weights describe scheduled-token credit, not a promise of equal GPU time or raw token share for requests with different costs. Five seconds is an operational starvation alarm, not a deadline guarantee.

### E. Adaptive controller

The controller uses observable latency/backlog signals with EWMA, hysteresis, cooldown, and explicit minimum/maximum bounds. It changes only prefill work budget; it cannot modify the hard resource limit, cancellation, or request state. Prompt-2 showed a lower development tail with lower goodput and more rejections, so adaptive mode remains opt-in.

### F. Negative optimization result

Profiling showed backend CUDA work dominated and the InferScale batch-planning range was about 0.036% of the execution range. Reusing buffers would have added persistent mutable state for little measured opportunity, so it was rejected and no CUDA speedup was claimed.

### G. Benchmark causality

The external llama-server comparison provides practical system context only. FIFO uses the same runtime, model, backend, and serving path as SA-WDS and is the scheduler control. Open-loop intended arrivals do not depend on response completion; paired-trial uncertainty was frozen in advance. Prompt-3 thermal-invalid arms were retained as invalid rather than used to tell a performance story.
