# InferScale — SLO-Aware Single-GPU LLM Inference Runtime

## Project class
Production-oriented **C++20/CUDA inference-runtime and performance-engineering project** for AI infrastructure, ML systems, GPU software, backend infrastructure, and high-performance systems roles.

## One-line problem
A single GPU serving mixed LLM traffic must balance interactive latency, batch throughput, finite KV memory, cancellation, fairness, and overload without hiding tradeoffs behind one aggregate tokens/second number.

## What InferScale actually owns
InferScale is **not** a wrapper around `llama-server` and does not claim to replace llama.cpp's model implementation. The runtime links directly against a pinned `libllama` build and owns the serving decisions around model execution:

- asynchronous OpenAI-compatible HTTP/SSE ingress;
- a single-owner request-runtime reactor;
- explicit lifecycle, deadline, and cancellation semantics;
- bounded queues and output backpressure;
- hard/soft admission control;
- SLO-aware scheduling with priority aging and fairness;
- a bounded feedback controller that adapts prefill budget to measured SLO pressure/headroom;
- chunked prefill to bound long-prompt interference;
- dynamic per-epoch batch construction;
- logical KV budgeting and request leases tied to backend sequence state;
- in-flight batch tickets that prevent cancellation/use-after-free races;
- overload, shutdown, and backend-recovery policies;
- request-level tracing and reproducible benchmark evidence;
- profiling-guided CUDA/control-path optimization only where measurement justifies it.

`llama-server` remains an **external production reference baseline**. InferScale also contains an **internal FIFO/static-policy control** so scheduling experiments compare identical code paths except for the policy under test.

## Why this version is technically stronger
The architecture deliberately avoids three weak claims:

1. It does **not** claim that continuous batching is novel; mature llama.cpp serving already provides it.
2. It does **not** call logical token/KV accounting a physical PagedAttention implementation.
3. It does **not** require a custom CUDA kernel merely to make the repository look GPU-heavy.

Instead, the project differentiates itself through measurable runtime policy: SLO-aware admission, chunked prefill, deterministic fairness, cancellation-safe resource ownership, goodput under overload, and controlled ablation studies.

## Core architecture contract

```text
Client / Load Generator
        |
        v
Async HTTP/SSE Layer
        |
        | RuntimeEvent
        v
Single-Owner Runtime Reactor
        |
        +--> Lifecycle / deadlines / cancellation
        +--> Admission Controller
        +--> KV Budget Manager
        +--> Adaptive Scheduler
        |       |
        |       v
        |   WorkPlan
        |   (token ranges, not request IDs only)
        |       |
        +------> Batch Builder
                    |
                    v
              Engine Executor
                    |
                    v
             direct libllama API
                    |
                 ggml/CUDA
                    |
                   GPU

Every stage ---> Metrics + trace event log + benchmark recorder
```

## Architectural non-negotiables

- **One writer for request state.** HTTP and executor threads send events; the runtime reactor owns lifecycle mutation.
- **No resource reuse while in flight.** Request generation IDs and `BatchTicket`s fence cancellation and sequence reuse.
- **Hard capacity is never overridden by priority.** Priority can change who waits, not whether memory exists.
- **Prefill is chunkable and adaptively budgeted.** A long prompt cannot consume an unbounded scheduling epoch, and the controller can only move the budget inside configured safe bounds.
- **The scheduler emits work units.** Plans identify request, generation, phase, token range, and expected KV growth.
- **Every performance claim has a control.** External reference, internal FIFO, adaptive policy, and isolated ablations are kept distinct.
- **Failure classes are explicit.** Request failure, recoverable engine failure, engine-restart-required, and process-fatal paths are not conflated.
- **The benchmark harness is part of the product.** Raw records, manifests, confidence intervals, thermals, and exact software/hardware revisions are preserved.

## Primary implementation languages

- **C++20** — runtime, scheduler, admission, networking integration, engine adapter, observability hot path
- **CUDA C++** — measured GPU experiments and optional targeted kernels
- **Python 3.11+** — load generation, experiment orchestration, statistical analysis, plotting, release validation

## Exactly five implementation phases

1. **External baseline + benchmark harness + embedded-engine proof**
2. **Single-owner runtime + FIFO control + cancellation-safe streaming**
3. **KV budgeting + chunked prefill + adaptive SLO scheduler**
4. **Nsight-guided CPU/GPU optimization and ablations**
5. **Production validation + failure campaign + reproducible benchmark release**

## Scope boundary
Resume-grade v1 is a **single-GPU scheduler/runtime project**, not a full model framework. It deliberately does not implement model training, multi-node parallelism, a tokenizer, full attention kernels, or physical paged KV memory.

A genuine physical paged-KV/PagedAttention backend is documented as an optional research extension in `16_RESEARCH_EXTENSION_PAGED_KV.md`. It must never be represented as complete unless InferScale actually owns physical blocks and the attention path consumes block mappings.

## Target quality
The architecture is designed so that, if implementation and evidence satisfy the definition-of-done gates, the project can credibly reach roughly **9.5–9.9/10 potential across architecture, systems depth, testing, benchmarking, failure handling, GPU credibility, feasibility, novelty, and resume value**. The architecture alone cannot honestly prove 10/10 performance or reliability; those scores require measured code and evidence.

See `15_ARCHITECTURE_SCORECARD.md` for the target-by-category breakdown.

## Recommended reading order

`01_PROBLEM_AND_GOALS.md` → `02_SYSTEM_ARCHITECTURE.md` → `04_COMPONENT_DESIGN.md` → Phases 1–5 → testing/benchmarking → repository/interfaces → definition of done → scorecard → optional paged-KV research extension.

## Non-negotiable evidence rule
A result is resume-worthy only if the exact model, backend commit, InferScale commit, workload distribution, concurrency/arrival process, context limits, hardware, driver, CUDA version, warm-up policy, sampling configuration, thermal state, request population, and statistical method are documented and reproducible.
