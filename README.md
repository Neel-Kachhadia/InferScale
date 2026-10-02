# InferScale

InferScale is a C++20 single-GPU inference runtime. Prompt 1 now has a direct llama.cpp v0.4.1 backend at commit `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`, real TinyLlama Q4_K_M CUDA execution, and FIFO request scheduling through bounded SSE streams.

InferScale owns HTTP/SSE, lifecycle, FIFO `WorkPlan`s, `LlamaBatchBuilder`, `BatchTicket` lifetime fencing, cancellation, and stream backpressure. Pinned libllama owns model loading, tokenization, llama batches, execution, sampling, and sequence memory. The unmodified `llama-server` is an external baseline endpoint only. Prompt 1 does not implement SA-WDS, adaptive scheduling, restart campaigns, or physical paged KV.

The [baseline report](docs/BASELINE.md) records the same-model external/InferScale open-loop sweep, run IDs, latency/throughput metrics, and telemetry. The [gate report](docs/PROMPT1_GATES.md) records all Prompt-1 evidence and limits. The [architecture](docs/ARCHITECTURE.md), [failure model](docs/FAILURE_MODEL.md), and [environment manifest](docs/environment.json) describe ownership, supported behavior, and exact validation inputs.

## Build and run

With the pinned llama.cpp checkout at `build/llama-source`, configure and build the CUDA runtime:

```sh
cmake --preset cuda-release -DFETCHCONTENT_SOURCE_DIR_LLAMA_CPP="$PWD/build/llama-source"
cmake --build build/cuda-release --target inferscale_llama_http_tests inferscale_llama_tests inferscale_llama_server inferscale_runtime_tests inferscale_http_tests -j2
MODEL=/path/to/model.gguf
build/cuda-release/inferscale_llama_server --model "$MODEL" --host 127.0.0.1 --port 8080 --ctx-size-per-seq 2048 --parallel 16 --batch-size 2048 --ubatch-size 512 --gpu-layers 99 --threads 8 --threads-batch 8 --threads-http 4
```

The strict `/v1/completions` endpoint accepts a `local-model` alias, prompt text, streaming, optional usage, and `max_tokens` from 1 to 128. It tokenizes the supplied prompt directly; callers provide any chat template text. Sampling is greedy. The GGUF remains outside Git.

## Verification

```sh
INFERSCALE_TEST_MODEL="$MODEL" INFERSCALE_TEST_GPU_LAYERS=99 ctest --test-dir build/cuda-release --output-on-failure
cmake --preset asan-ubsan
cmake --build build/asan-ubsan -j2
ctest --preset asan-ubsan
python3 -B -m unittest discover -s tests -v
```

See [docs/PROMPT1_GATES.md](docs/PROMPT1_GATES.md) for the TSan host workaround and the exact Prompt-1 gate status. Build trees and raw benchmark records remain local under ignored `build/`.
