# Resume and interview evidence

These bullets separate implementation evidence from measured policy effects.
Prompt-2 workload measurements below are development evidence; Prompt-3 did
not produce valid release-scale comparisons after the GPU reached its
predeclared thermal stop.

## Defensible resume bullets

- **ACTION:** Built a C++20 single-GPU LLM runtime that directly drives pinned
  libllama and owns HTTP/SSE, request lifecycle, FIFO/SA-WDS scheduling,
  cancellation, and backpressure. **SYSTEM CONTEXT:** TinyLlama-1.1B Chat
  Q4_K_M on an RTX 4060 Laptop GPU, llama.cpp v0.4.1 commit
  `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`. **MEASURED RESULT:** A clean
  detached worktree at commit `887bb0b70c56aaa05326526c1b12c2836d40eab2`
  configured and built the CUDA target, passed CPU/FakeEngine CTest 3/3 and
  ASan/UBSan 3/3, passed Python tests 26/26, and completed an eight-request
  CUDA smoke. **TRADEOFF:** Physical KV allocation remains
  backend-owned; the clean smoke validates reproducibility, not throughput.
  **EVIDENCE PATH:** `docs/REPRODUCTION.md`, committed clean-worktree campaign
  `artifacts/release-v1/reproduction/clean-smoke/20261003T162240Z`.

- **ACTION:** Added generation-fenced request keys and ticket-pinned sequence
  reuse around asynchronous backend work. **SYSTEM CONTEXT:** Runtime and
  FakeEngine lifecycle tests with cancellation, stale completion, failure,
  queue pressure, and shutdown ordering. **MEASURED RESULT:** The deterministic
  lifecycle model exercised 256 serialized seeds with up to 64 state operations
  each; separate directed tests inject cancellation with a ticket in flight,
  stale completion, duplicate completion, and sequence reuse. ASan/UBSan and
  the supported TSan suite passed. **TRADEOFF:** The randomized model itself
  does not create overlapping ticket races; TSan cannot instrument Boost.Asio
  `atomic_thread_fence`; automatic backend restart is unsupported. **EVIDENCE
  PATH:** `tests/runtime_test.cpp`,
  `docs/FAILURE_MODEL.md`, `docs/PROMPT3_GATES.md`.

- **ACTION:** Implemented conservative hard logical resource admission with
  leases, growth authorization, soft-pressure behavior, chunked prefill, and
  FIFO/SA-WDS controls. **SYSTEM CONTEXT:** 16 sequences × 2,048 context tokens;
  runtime resolved 29,491 logical hard tokens on the RTX 4060. **MEASURED
  RESULT:** Boundary, arithmetic, released-lease immutability, generation,
  pressure, ticket-retirement, and clear-ack tests pass. **TRADEOFF:** Logical
  token accounting is not physical KV-page accounting or PagedAttention; the
  planned sustained soak was blocked by the thermal stop. **EVIDENCE PATH:**
  `docs/KV_ACCOUNTING.md`, `tests/policy_test.cpp`,
  `tests/runtime_test.cpp`, `docs/PROMPT3_GATES.md`.

- **ACTION:** Added SLO-aware weighted-deficit service and bounded adaptive
  prefill control. **SYSTEM CONTEXT:** Prompt-2 mixed workload, TinyLlama on
  RTX 4060, three paired seeds × 80 offers per treatment at 8 offered RPS.
  **MEASURED RESULT:** Static C3 mean interactive TTFT p99 was 442 ms versus
  FIFO's 471 ms. **TRADEOFF:** Mean goodput was 5.35 versus 6.48 requests/s,
  output throughput 218.07 versus 258.23 tokens/s, and rejections 43 versus 25
  per 240 offers. The comparison resolved batch/ubatch to 2048/2048; each
  80-offer trial had about 50 interactive completions, and successful-response
  p99 can be affected by rejection shedding. Adaptive C4 observed p99 another
  124 ms below C3 while goodput was 0.22 requests/s lower and rejections were
  14 higher.
  These are development comparisons, not release-grade confidence claims.
  **EVIDENCE PATH:**
  `docs/BENCHMARKS_PHASE3.md`, `docs/BENCHMARKS.md`.

- **ACTION:** Profiled the mixed inference path with NVTX, Nsight Systems, and
  CPU/runtime timelines; rejected a buffer-reuse optimization hypothesis.
  **SYSTEM CONTEXT:** Pinned llama.cpp CUDA execution on the tested laptop.
  **MEASURED RESULT:** Batch-plan construction was about 0.036% of the backend
  execution range; backend CUDA execution and synchronization dominated.
  **TRADEOFF:** No custom CUDA speedup is claimed. **EVIDENCE PATH:**
  `docs/profiling/PROFILE_PHASE4.md`,
  `docs/profiling/OPTIMIZATION_EXPERIMENT.md`.

## Interview stories

### A. Stale completion and sequence reuse

An HTTP request can be cancelled while a backend ticket still owns its
sequence. The reactor records terminal intent and stops future scheduling,
while the ticket pin and logical charge remain until completion. A stale
generation cannot update the current request. Only after ticket retirement,
backend synchronization, sequence clear, and matching clear acknowledgement
can the sequence be reused. Tests inject cancellation, slot reuse, duplicate
completion, and late output.

### B. Long prefill interference

A large prompt is split into exact half-open token ranges. The cursor advances
only after successful, generation-valid completion. The scheduler can
reconsider interactive work between chunks; a failed or cancelled chunk never
causes a gap or double commit. Prompt-2's small ablation did not establish a
general goodput improvement, so chunking is described as a bounded
interleaving mechanism.

### C. Resource pressure

The hard logical capacity is a safety invariant. Soft admission reserve and
pressure signals influence whether new work is accepted or deferred; priority
and deadlines cannot exceed the hard cap. Existing work may grow only after
per-ticket authorization. Unknown backend growth stays charged until a
successful sequence clear. Backend memory remains outside this logical ledger.

### D. Fairness

FIFO remains a transparent control. SA-WDS accumulates bounded class-level
token credits, applies aging and deadline buckets only after safety
eligibility, and retains finite prefill opportunity under decode backlog.
Weights describe scheduled-token credit, not a promise of equal GPU time or
raw token share for requests with different costs. Five seconds is an
operational starvation alarm, not a deadline guarantee.

### E. Adaptive controller

The controller uses observable latency/backlog signals with EWMA, hysteresis,
cooldown, and explicit min/max bounds. It changes only prefill work budget;
it cannot modify the hard resource limit, cancellation, or request state.
Prompt-2 showed lower interactive tail but lower goodput and more rejections,
so adaptive mode remains opt-in.

### F. Negative optimization result

Profiling showed the backend's CUDA work dominated and the InferScale batch
planning range was about 0.036% of the execution range. Reusing buffers would
have added persistent mutable state for little measurable opportunity, so it
was rejected and no CUDA speedup was claimed.

### G. Benchmark causality

The external llama-server comparison provides practical system context only.
FIFO uses the same runtime, model, backend, and serving path as SA-WDS and is
the scheduler control. Open-loop intended arrivals do not depend on response
completion; repeated paired trials and trial-level uncertainty were
predeclared. Prompt-3 thermal-aborted trials were retained as invalid rather
than used to tell a performance story.
