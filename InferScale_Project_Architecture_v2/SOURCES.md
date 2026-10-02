# Current Upstream References and Versioning Rules

These sources justify architectural boundaries. Benchmark releases must pin exact revisions rather than relying on whatever is current later.

## llama.cpp

Repository:

- https://github.com/ggml-org/llama.cpp

Server documentation:

- https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md

Public API header:

- https://github.com/ggml-org/llama.cpp/blob/master/include/llama.h

Server benchmark tooling:

- https://github.com/ggml-org/llama.cpp/tree/master/tools/server/bench

### Architectural implication
Current `llama-server` already supports multi-user/parallel decoding and continuous batching. InferScale therefore keeps it as an external reference and drives `libllama` directly for its own scheduler experiments.

The public API exposes sequence-aware batch/decode interfaces that make a direct adapter possible; exact API names/semantics must be verified against the commit pinned by the implementation.

## Paged-KV research context

llama.cpp discussion/design work has explored a genuine paged-KV + scheduler implementation:

- https://github.com/ggml-org/llama.cpp/discussions/21961

This reinforces the distinction between a logical KV budget manager and a true physical paged-KV/attention implementation. InferScale v1 deliberately chooses the former; `16_RESEARCH_EXTENSION_PAGED_KV.md` defines what the latter would require.

## NVIDIA CUDA / profiling

CUDA documentation:

- https://docs.nvidia.com/cuda/

CUDA Programming Guide:

- https://docs.nvidia.com/cuda/cuda-programming-guide/

CUDA release notes:

- https://docs.nvidia.com/cuda/cuda-toolkit-release-notes/

Nsight Systems:

- https://developer.nvidia.com/nsight-systems

Nsight Compute:

- https://developer.nvidia.com/nsight-compute

Compute Sanitizer:

- https://docs.nvidia.com/compute-sanitizer/

## Reproducibility rule
Never write "latest" in a benchmark result. Record exact:

```text
InferScale commit
llama.cpp commit
compiler/build flags
CUDA toolkit/runtime
driver
model checksum/quantization
resolved server config
workload manifest checksum
analysis code commit
```

A source URL may evolve; a benchmark manifest must not.
