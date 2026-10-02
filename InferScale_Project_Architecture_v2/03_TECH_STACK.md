# InferScale — Technology Stack and Dependency Policy

## 1. Core language/runtime

### C++20
Use C++20 for the serving runtime and policy hot path.

Required engineering practices:

- RAII for engine, CUDA, socket, timer, and trace resources;
- `std::chrono::steady_clock` for latency measurement;
- `std::span` / `std::string_view` where lifetime is explicit;
- `std::jthread` / stop tokens where they simplify shutdown;
- move-only ownership for batch tickets and leases where practical;
- bounded queues instead of unbounded producer/consumer growth;
- minimal shared mutable state;
- no exceptions crossing the engine/runtime ABI boundary without classification.

The runtime reactor should remain straightforward enough that lifecycle invariants can be reviewed manually.

## 2. Inference backend

### Direct `libllama` adapter
InferScale links directly against a **pinned llama.cpp commit** and uses its public C/C++ inference interfaces to construct sequence-aware batches and execute them.

Responsibilities retained by llama.cpp:

- model loading and GGUF handling;
- tokenization/model vocabulary primitives;
- tensor graph construction;
- actual sequence/KV memory representation;
- ggml backend execution;
- mature CUDA kernels;
- sampling primitives when chosen.

Responsibilities owned by InferScale:

- external request model;
- admission/backpressure;
- queueing;
- scheduling policy;
- prefill chunk boundaries;
- batch composition policy;
- lifecycle/deadlines/cancellation;
- logical KV reservation and safety accounting;
- metrics/traces/benchmark controls.

### External baseline
Build the same pinned llama.cpp revision's `llama-server` separately and preserve the exact CLI configuration used in reference benchmarks.

## 3. CUDA

Use the CUDA version supported by the machine and pinned benchmark environment. Record exact driver/toolkit/backend revisions in release manifests.

Initial rule:

> Use backend kernels first. Write custom CUDA only after Nsight identifies a specific bottleneck that InferScale can legally and cleanly own.

Candidate experiments after profiling:

- metadata packing/copy path;
- sampling/post-processing;
- small host-to-device control structures;
- CUDA Graph launch-overhead experiment where workload shape permits;
- a small fused control/postprocess kernel.

Do not reimplement attention merely to add a CUDA file.

## 4. Networking

### Boost.Asio + Boost.Beast
Recommended for resume-grade v1 because it provides explicit asynchronous I/O, timers, connection lifetime control, and streaming without forcing InferScale to invent a networking framework.

Design constraints:

- bounded request body;
- bounded per-connection output buffer;
- slow-reader timeout;
- keep-alive limits;
- cancellation on disconnect;
- health/readiness endpoints independent of model traffic where possible.

OpenAI-compatible chat/completions support can be intentionally narrow: implement only the fields the project documents and reject unsupported fields explicitly.

## 5. Serialization/configuration

Recommended:

- `nlohmann/json` or equivalent well-understood JSON library for API compatibility;
- `toml++` or YAML library for static configuration;
- machine-readable benchmark manifests in YAML/JSON.

Do not optimize JSON parsing until profiling proves it matters.

## 6. Build system

### CMake + Ninja

Required presets:

```text
dev
release
asan-ubsan
tsan
cuda-release
```

Recommended build properties:

- `compile_commands.json` export;
- warnings-as-errors in project code;
- hidden/default symbol policy documented;
- reproducible dependency revisions;
- optional CUDA target;
- LTO experiment only after correctness gates pass.

## 7. Dependency pinning

Dependencies must be pinned in one reproducible mechanism: CMake FetchContent with commit hashes, CPM, git submodules, or a lockfile-based package manager. Do not use floating `master` in benchmark releases.

At minimum pin:

- llama.cpp commit;
- networking/JSON/test libraries;
- Python requirements;
- benchmark container/environment metadata when used.

## 8. Testing stack

Recommended:

- **GoogleTest** for unit/integration tests;
- CTest orchestration;
- deterministic `FakeEngine`;
- randomized/model-based tests using deterministic seeds;
- Clang libFuzzer targets for request parsing and lifecycle event sequences where practical;
- ASan + UBSan every milestone;
- TSan on CPU/fake-engine paths;
- NVIDIA Compute Sanitizer for retained custom CUDA code.

Most scheduler/resource correctness must be testable without a GPU.

## 9. Profiling stack

### NVTX
Annotate:

- scheduler epoch;
- admission pass;
- batch build;
- engine submission;
- prefill chunk;
- decode step;
- sampling/post-processing;
- stream enqueue;
- cancellation retirement.

### Nsight Systems
Primary end-to-end CPU/GPU timeline tool.

### Nsight Compute
Use only on a kernel already identified as relevant.

### Linux perf
Use for CPU-side batch construction, queue/scheduler hot paths, locks, cache misses, or unexpected host overhead.

## 10. Metrics/event storage

Keep two layers:

### Online operational metrics
Low-cardinality counters/gauges/histograms suitable for `/metrics` or structured logs.

### Benchmark event log
High-detail per-request/per-epoch data written to JSONL/Parquet/CSV outside the critical path through a bounded recorder. If the recorder falls behind, inference continues and dropped-record count increments.

Never put unbounded logging on the serving hot path.

## 11. Python benchmark stack

Recommended:

- `asyncio` + `httpx`/`aiohttp` for the custom load generator;
- NumPy;
- pandas or Polars;
- SciPy for statistical helpers where needed;
- matplotlib for release plots;
- PyYAML/ruamel for workload manifests.

The open-loop generator must schedule intended arrival times independently of previous response latency to avoid coordinated omission.

## 12. Model strategy

Use at least two model/workload sizes during development:

- a small/fast model for iteration and CI-like GPU smoke tests;
- a larger model that meaningfully stresses the target GPU for final evidence.

Every release result records model hash and quantization. Never compare across quantizations as if only the scheduler changed.

## 13. Security and robustness baseline

Although InferScale is not an internet-hosting product, basic production hygiene improves the architecture score:

- request-size limits;
- strict parsing/validation;
- no shelling out from request handlers;
- no arbitrary local file access via API;
- bounded memory for queues and stream buffers;
- explicit timeouts;
- graceful shutdown;
- dependency and secret scanning in CI;
- no model weights committed to Git.

## 14. Dependency principle

Prefer mature libraries for commodity work and custom code for the project-defining systems problems.

Good custom-code targets:

```text
runtime reactor
scheduler
admission controller
KV budget/lease logic
batch policy
failure model
benchmark semantics
```

Poor custom-code targets unless profiling demands them:

```text
HTTP parser
JSON parser
thread pool
logging framework
generic metrics library
full attention implementation
```
