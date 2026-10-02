# Baseline status

## Environment and pin

The captured host manifest is [environment.json](environment.json). The pinned backend is llama.cpp v0.4.1 at `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`. Its checkout was verified locally from the release tag.

No GGUF model was found in the inspected home/project/cache locations. The NVIDIA kernel module reports GeForce RTX 4060 Laptop GPU and driver 595.91.07, but `nvidia-smi` fails to communicate with it, reports no accessible device node, and `nvcc` is absent. VRAM capacity and CUDA toolkit/runtime version are therefore unknown.

GPU temperature, power, utilization, and throttling state are unknown because `nvidia-smi` is unavailable; the environment manifest records these as null.

## Reproduction commands

Pinned llama.cpp source build (CPU-only host validation):

```sh
scripts/build_llama_reference.sh
```

The unmodified pinned reference `llama-server` built successfully in Release, CPU-only mode; its `--help` output was checked. The exact serving argv used by `scripts/run_reference.sh` is:

```text
llama-server --model $MODEL_PATH --host 127.0.0.1 --port 8080 --ctx-size 4096 --parallel 4 --batch-size 2048 --ubatch-size 512 --cont-batching --flash-attn off --n-gpu-layers 0 --threads 8 --threads-batch 8 --threads-http 4 --cache-type-k f16 --cache-type-v f16 --seed 1 --temp 0 --top-k 1 --top-p 1 --no-webui
```

Model serving requires `MODEL_PATH` to point at an independently obtained GGUF file. The reference remains unrun because this host has no model file.

Run the load generator against an OpenAI-compatible completion endpoint:

```sh
python3 bench/run.py --workload bench/workloads/mixed_slo.yaml --url http://127.0.0.1:8080/v1/completions
python3 bench/sweep.py --workload bench/workloads/mixed_slo.yaml --rates 0.5,1,2,4,8 --url http://127.0.0.1:8080/v1/completions
```

## Evidence and limits

The 5-request fake-server smoke (run ID `c1e0a36f-472b-48a4-8ab2-d6137709429a`) completed with 0 recorder drops, 44.295 ms maximum sender lateness against a 100 ms limit, and all rows carrying fake usage. Raw records are at ignored `build/fake-sse-smoke/c1e0a36f-472b-48a4-8ab2-d6137709429a/requests.jsonl`; the summary is beside it. Latencies are anchored to intended arrivals and include sender delay. This is a transport smoke, not a baseline result. The real external server cannot be started or benchmarked without a model. The direct libllama target compiles independently but has not produced model output. No saturation curve, GPU E2E, or performance comparison was collected.
