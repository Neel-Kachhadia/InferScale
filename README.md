# InferScale

InferScale is a C++20 single-GPU inference runtime project built around direct `libllama` execution. This repository contains a CPU-testable runtime ownership core, a deterministic fake engine, an async HTTP/SSE transport for that fake engine, a direct `libllama` proof target, and an asynchronous benchmark client.

## Current status

- The FIFO reactor uses generation-fenced request keys, move-only batch tickets, bounded queues, and clear acknowledgements before sequence reuse. Its GoogleTest suite includes cancellation, stale completion, randomized lifecycle invariants, and shutdown cases.
- `inferscale_fake_server` provides bounded asynchronous `/v1/completions` SSE over the CPU `FakeEngine`; it is a transport/runtime validation endpoint, not model serving.
- The async load generator passed a five-request open-loop smoke against the fake server. Raw data remains under ignored `build/fake-sse-smoke/`.
- The pinned llama.cpp v0.4.1 `llama-server` builds from commit `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`; the direct `libllama` smoke target compiles against the same checkout.
- This host has no accessible NVIDIA runtime and no GGUF model. Direct model output, CUDA execution, external-server serving/saturation, real-GPU E2E, and both milestone tags remain blocked. No InferScale performance claim is made.

## Build and checks

```sh
cmake --preset dev
cmake --build --preset dev -j2
ctest --preset dev
python3 -B -m unittest discover -s tests
```

Loopback integration tests need local TCP socket permission. The CTest case and Python test skip when the sandbox denies it.

The fake SSE endpoint is `build/dev/inferscale_fake_server --host 127.0.0.1 --port 8080`; it accepts only the documented small streaming completion subset and uses deterministic fake tokens. The optional direct backend build is `cmake --preset llama-cpu -DFETCHCONTENT_SOURCE_DIR_LLAMA_CPP="$PWD/build/llama-source"` followed by `cmake --build build/llama-cpu --target inferscale_llama_smoke -j2`. It requires a GGUF path to run.

The pinned external reference is built with `scripts/build_llama_reference.sh`; `scripts/run_reference.sh` requires `MODEL_PATH` and records the resolved CLI in ignored `build/reference-command.txt`.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/BASELINE.md](docs/BASELINE.md), [docs/BENCHMARK_METHODOLOGY.md](docs/BENCHMARK_METHODOLOGY.md), and [docs/FAILURE_MODEL.md](docs/FAILURE_MODEL.md).
