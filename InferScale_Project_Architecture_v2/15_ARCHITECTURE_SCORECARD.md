# InferScale v2 — Architecture Quality Scorecard

## Purpose
This scorecard rates **architecture potential**, not completed implementation quality. A design can make a 9.9/10 implementation possible; only code, tests, traces, and benchmark evidence can earn the final result.

## Target scores after this revision

| Category | v1 estimate | v2 target potential | Why v2 is stronger |
|---|---:|---:|---|
| Problem definition | 9.5 | **9.9** | precise mixed-workload problem, explicit SLO/goodput/failure hypotheses |
| Systems architecture | 8.0 | **9.9** | direct libllama boundary, single-owner reactor, execution tickets, explicit backpressure/recovery |
| C++ systems depth | 9.0 | **9.9** | lifecycle generations, move-only ownership, event-driven control, bounded queues, model-based concurrency tests |
| GPU/CUDA credibility | 8.0 | **9.7** | direct backend execution, NVTX/Nsight first, targeted CUDA only when evidence supports it |
| Testing strategy | 9.5 | **9.9** | deterministic fake engine, model-based randomized tests, fuzzing, sanitizers, real-GPU seam tests |
| Benchmark methodology | 9.0 | **9.9** | external vs internal controls, coordinated-omission-safe open loop, goodput, CIs, thermal validity, run validation |
| Failure handling | 9.0 | **9.9** | four-class error taxonomy, engine restart path, stale-generation tests, bounded shutdown, slow-reader handling |
| Feasibility on one GPU | 7.0 | **9.7** | physical PagedAttention removed from core; v1 focuses on buildable scheduler/runtime depth |
| Technical novelty | 6.5 | **9.7** | novelty shifts from already-common continuous batching to SLO-aware chunking, fair service, cancellation/resource safety, plus bounded feedback adaptation with static-policy ablation |
| Resume/interview potential | 9.0 | **9.9** | clear ownership, defensible causal evidence, hard concurrency story, measurable tradeoffs |
| Reproducibility | 8.5* | **9.9** | pinned manifests, resolved configs, raw runs, run validators, reproducible release bundle |
| Production discipline | 8.5* | **9.8** | bounded ingress/egress, graceful shutdown, failure taxonomy, recovery, CI/fuzz/sanitizer gates |

`*` Added as explicit categories in v2; v1 did not have a separate numeric assessment.

## Why no category is marked 10.0
A 10/10 architecture claim before implementation would be false precision. The remaining gap is intentionally reserved for evidence that only implementation can supply:

```text
measured backend/API constraints
bugs discovered under concurrency
real p99 behavior
actual Nsight bottlenecks
thermal sensitivity
recovery behavior on target hardware
statistical repeatability
```

If v1.0 passes all Definition-of-Done gates with strong data, most categories can credibly be described as **near-10 portfolio quality**.

## What specifically raised the weakest categories

### Feasibility: 7.0 -> 9.7
The core no longer promises physical paged KV while relying on backend-owned tensors. Logical KV budgeting is honest and implementable. Real PagedAttention becomes a separate optional research branch.

### Novelty: 6.5 -> 9.5
The project no longer sells "continuous batching" as the differentiator. It studies a coherent runtime policy combining:

```text
chunked prefill
SLO-aware weighted-deficit scheduling
decode protection
aging/fairness
hard/soft KV admission
slow-reader backpressure
generation-fenced cancellation
controlled overload
```

plus a bounded feedback controller that adapts prefill budget to SLO pressure/headroom, and proves contributions through same-runtime ablations.

### GPU credibility: 8.0 -> 9.7
The project is GPU-real without CUDA theatre. It directly executes a GPU backend, instruments CPU/GPU boundaries, profiles representative load, and requires any custom kernel to survive end-to-end ablation.

### Systems architecture: 8.0 -> 9.9
The single-writer reactor + request generations + batch-ticket retirement creates a much stronger concurrency model than shared mutation plus a cancellation flag.

## The only path to a true 10-level project
After the resume-grade core is complete, one or more of the following can push the project into research/open-source territory:

```text
upstream a useful llama.cpp issue/PR/benchmark improvement
implement the physical paged-KV research extension correctly
publish a rigorous technical report with reproducible workload data
add a second backend and demonstrate policy portability
validate across multiple GPU classes/models without weakening causality
```

Do not add these until the core v1 evidence is finished.
