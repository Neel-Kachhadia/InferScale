# Prompt-1 real-model baseline

## Host and model

The verified host, compiler, CUDA, and runtime manifest is [environment.json](environment.json). The host is Ubuntu 24.04.5 LTS on kernel `7.0.0-38-generic`, with a GeForce RTX 4060 Laptop GPU (8188 MiB, compute capability 8.9), driver `580.178.04`, CUDA Toolkit `13.0`, and `nvcc` `13.0.88`. No driver, kernel, or CUDA installation changes were made.

Both endpoints used `/home/neel/models/inferscale/tinyllama/TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf` (668788096 bytes; SHA-256 `9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0`). The model is local and excluded from Git. Both CUDA builds use pinned llama.cpp v0.4.1 commit `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`, configured with `GGML_CUDA=ON`; CMake auto-detected `89-real`.

Direct libllama validation loaded the GGUF, tokenized and prefetched `The capital of France is`, sampled four tokens and rendered ` Paris.`, then removed the sequence and destroyed the context/model. Backend logs identified the RTX 4060 at CC 8.9 and reported all 23/23 model layers offloaded to CUDA0 (601.02 MiB model buffer). InferScale startup reported the same offload. This plus runtime GPU memory/utilization samples establishes real GPU activity; a standalone CUDA kernel smoke is not used as project evidence.

## Build and run commands

The unmodified external reference build is reproducible with:

```sh
scripts/build_llama_reference.sh
MODEL_PATH=/path/to/TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf PORT=18080 scripts/run_reference.sh
```

`scripts/run_reference.sh` recorded this effective configuration in ignored `build/reference-command.txt` and `build/reference-run-metadata.txt`: 99 GPU layers; total context 32768, 16 slots (2048 each); batch 2048, ubatch 512; continuous batching on; unified KV off; flash attention off; 8 model threads, 8 batch threads, 4 HTTP threads; f16 K/V; greedy sampling (`seed=1`, `temp=0`, `top_k=1`, `top_p=1`). The server returned a real completion (`Hey, it’s you!`, 9 tokens, stop) and shut down cleanly. It is external comparison reference A and is not InferScale's internal execution path.

The integrated CUDA build used:

```sh
cmake --preset cuda-release -DFETCHCONTENT_SOURCE_DIR_LLAMA_CPP="$PWD/build/llama-source"
cmake --build build/cuda-release --target inferscale_llama_tests inferscale_llama_server inferscale_llama_smoke -j2
```

The model was rehashed before the direct proof:

```sh
MODEL="/home/neel/models/inferscale/tinyllama/TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf"
test -s "$MODEL"
sha256sum "$MODEL"
build/cuda-release/inferscale_llama_smoke "$MODEL" "The capital of France is" 99
```

The same pinned backend and model were used by the InferScale CUDA server, configured for 2048 context per sequence, 16 sequences, batch 2048/ubatch 512, 99 GPU layers, 8/8 model threads, 4 HTTP threads, greedy sampling, flash attention off, and separate per-sequence KV. The production configuration did not change the 4096-byte stream-buffer bound.

## Coordinated-omission-safe sweep

The async load generator precomputes fixed-rate arrivals and anchors primary TTFT/E2E to intended arrival time. Each point used workload `interactive_short` (seed 711, prompt size 32–128 repeated `word` inputs, requested output 16–64 tokens, TTFT SLO 500 ms, E2E SLO 5000 ms), 80 requests, concurrency limit 128, and rates 2/8/24/64 requests/s. Both sides used this exact command except for URL and output directory:

```sh
python3 bench/sweep.py --workload bench/workloads/interactive_short.yaml --rates 2,8,24,64 --requests 80 --concurrency 128 --url http://127.0.0.1:PORT/v1/completions --model local-model --output build/prompt1-baseline/ENDPOINT --seed 711
```

Primary latency percentiles below are measured from intended arrival; ITL is inter-arrival between content-bearing SSE events, not exact per-model-token latency. Percentiles use successful requests only; rejected/error requests remain in raw records and error counts. Output-token and processed-token rates are null where request errors prevent exact usage for every sample.

| Endpoint | Offered/s | Run ID | Success / 80 | Errors | TTFT p50 / p90 / p95 / p99 ms | E2E p50 / p90 / p95 / p99 ms | ITL p50 / p90 / p95 / p99 ms | Completed req/s | Output tok/s | Processed tok/s | Max sender lateness ms |
|---|---:|---|---:|---:|---|---|---|---:|---:|---:|---:|
| External llama-server | 2 | `d24812a7-bd33-4467-b04e-fcf34946fef7` | 80 | 0 | 6.97 / 13.30 / 32.90 / 50.22 | 164.70 / 225.06 / 232.13 / 264.65 | 3.68 / 3.82 / 3.86 / 4.04 | 2.015 | 81.08 | 240.25 | 27.58 |
| External llama-server | 8 | `92ae9806-bc08-40cc-b319-197649b2e9b1` | 80 | 0 | 10.23 / 20.16 / 34.38 / 37.15 | 193.30 / 294.22 / 327.17 / 356.92 | 4.24 / 6.14 / 8.14 / 8.48 | 7.937 | 319.37 | 946.40 | 27.08 |
| External llama-server | 24 | `14a6412d-853c-46e2-8327-188cbbfc671a` | 80 | 0 | 63.51 / 113.82 / 131.40 / 148.98 | 733.74 / 1023.32 / 1066.51 / 1105.40 | 14.01 / 21.19 / 24.39 / 44.13 | 19.861 | 799.14 | 2368.14 | 29.00 |
| External llama-server | 64 | `3980f36d-1f30-41f9-b113-9357a50eaaef` | 80 | 0 | 701.59 / 1502.25 / 1591.25 / 1663.53 | 1385.08 / 2266.29 / 2488.42 / 2553.43 | 14.30 / 22.46 / 23.57 / 26.84 | 21.252 | 855.12 | 2534.00 | 27.85 |
| InferScale FIFO | 2 | `791d10b5-b0c1-46e6-99d3-762cecd8b0a3` | 80 | 0 | 19.37 / 22.55 / 22.84 / 48.65 | 168.39 / 238.07 / 245.12 / 277.48 | 4.12 / 4.43 / 4.56 / 4.78 | 2.014 | 81.04 | 240.16 | 29.67 |
| InferScale FIFO | 8 | `52edd4d5-9390-445d-8a4c-26c53fab0a33` | 80 | 0 | 52.53 / 85.91 / 95.89 / 121.09 | 448.14 / 757.04 / 816.19 / 869.49 | 10.19 / 16.34 / 20.13 / 25.53 | 7.891 | 317.52 | 940.91 | 31.83 |
| InferScale FIFO | 24 | `5d15acd8-82f7-413a-bd09-b44376dee024` | 45 | 35 | 134.33 / 182.55 / 190.08 / 221.20 | 1296.67 / 1823.13 / 2011.58 / 2347.81 | 29.83 / 40.86 / 46.13 / 54.91 | 10.180 | null | null | 31.09 |
| InferScale FIFO | 64 | `2cbbb759-9d91-41cd-a154-a6795350896d` | 24 | 56 | 92.25 / 139.29 / 142.13 / 144.04 | 1236.04 / 1718.78 / 1779.66 / 1804.87 | 28.40 / 39.30 / 42.39 / 45.93 | 10.230 | null | null | 30.89 |

All eight runs had zero raw-recorder drops and passed the configured 100 ms sender-lateness validity bound. InferScale began rejecting overload at 24 offered requests/s (35 HTTP errors) and rejected 56 at 64 requests/s; its active-slot limit is 16. The external reference completed all samples but its p99 TTFT rose sharply between 24 and 64 offered requests/s while throughput stayed near 20–21 requests/s. These are practical system-reference measurements: they do not isolate scheduler causality and do not claim FIFO is faster or slower than llama-server.

Raw JSONL and full summaries remain locally under ignored `build/prompt1-baseline/{external,inferscale}/<run-id>/`. Compact summaries are `saturation.csv` beside those runs. GPU samples and CPU `vmstat` captures are under `build/prompt1-baseline/`. During the A/B capture, external samples recorded 1589–1609 MiB used, 47–69 C, 1.72–89.66 W, and up to 95% reported GPU utilization; InferScale samples recorded 1573–1607 MiB, 51–76 C, 1.92–91.28 W, and up to 96% utilization. Whole `vmstat` capture windows (including setup and idle tail) averaged 3.3% user plus 0.7% system CPU on A and 3.7% user plus 0.8% system on B; their maximum combined user+system samples were 21% and 24%. CPU values are window-wide host context, not per-offered-rate attribution. These are short host/model sample-window maxima, not long-run thermal or power guarantees.

The direct smoke output, model-backed HTTP records, real-reactor backpressure metrics, model-backed slow-reader test, and active-shutdown test are retained under ignored `build/` for local audit. See [ARCHITECTURE.md](ARCHITECTURE.md) and [PROMPT1_GATES.md](PROMPT1_GATES.md) for Prompt-2 readiness and gate evidence.
