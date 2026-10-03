# InferScale

InferScale is a C++20 single-GPU inference runtime for decoder LLMs, intended
to serve interactive and batch requests while bounding resource use and
overload. It owns request lifecycle, admission, scheduling, cancellation, and
SSE backpressure; the pinned llama.cpp library loads the model, owns physical
KV state, and runs inference. The external `llama-server` executable is used
only as a practical reference endpoint. It is not part of InferScale's request
path.

## What the runtime does

- **FIFO** is the conservative default and same-runtime control.
- **SA-WDS** adds deterministic interactive/batch weighted service credits,
  bounded aging, deadline urgency, and decode protection.
- **Chunked prefill** lets the scheduler reconsider long prompts between exact,
  contiguous token ranges.
- **KvBudgetManager** accounts conservative logical sequence-token capacity,
  reserve, and commit. llama.cpp still owns physical KV memory; this is not
  PagedAttention or physical page allocation.
- **Adaptive prefill** changes only a bounded epoch prefill budget using
  hysteresis and cooldown. Static control remains available.
- **Cancellation and overload** stop future scheduling after reactor
  cancellation, retain in-flight ticket ownership until retirement, and reject
  work at explicit queue, sequence, or resource bounds.

`RuntimeReactor` is the single lifecycle and scheduler-state writer.
`RequestKey` generations and `BatchTicket` pins prevent stale completions from
mutating reused requests or sequences. The backend synchronizes and clears a
sequence before its ID becomes reusable.

## Default policy and measured tradeoff

FIFO remains the recommended default. Prompt-2 development measurements used
TinyLlama-1.1B Chat Q4_K_M on an RTX 4060 Laptop GPU, a mixed workload at 8
offered requests/s, and three paired seeds with 80 offers per treatment. Static
SA-WDS with a 64-token generation reserve (C3) reduced mean interactive TTFT
p99 from 471 ms to 442 ms, while mean SLO goodput changed from 6.48 to 5.35
requests/s, output throughput from 258.23 to 218.07 tokens/s, and rejections
from 25 to 43 per 240 offers. These are development results, not release-grade
confidence claims. The scheduler comparison resolved batch/ubatch 2048/2048.
Each trial's p99 uses completed responses only; with about 50 interactive
observations per trial, that tail estimate is coarse, and admission shedding
may contribute to a lower successful-response p99. The extra rejections,
goodput, and throughput costs are reported alongside it. The adaptive
treatment reduced mean interactive TTFT p99 by about 124 ms relative to static
C3, while reducing goodput by about 0.22 requests/s and adding 14 rejections.

The frozen interactive SLO is TTFT ≤750 ms, scheduler-ready wait p99 ≤500 ms,
and E2E ≤8 seconds. Batch's 5-second wait threshold is an operational
starvation alarm, not a hard service guarantee.

The predeclared Prompt-3 16 RPS headline trial and 8 RPS mixed trial reached
the 85 C thermal stop before completing. Both are retained as invalid and
excluded. No Prompt-3 paired performance effect or resume-grade release tag is
claimed. See [benchmark evidence](docs/BENCHMARKS.md) and the [Prompt-3 gate
record](docs/PROMPT3_GATES.md).

Prompt-2 profiling found backend CUDA execution and synchronization dominate;
InferScale batch-plan construction measured about 0.036% of the backend
execution range. A buffer-reuse idea did not justify persistent complexity,
and no custom CUDA optimization or speedup claim was retained. See
[profiling](docs/profiling/README.md).

## Build and run

Prerequisites are C++20, CMake, Ninja, CUDA 13, and the external model file.
The CMake dependency file pins llama.cpp v0.4.1 at
`b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`.

```sh
cmake --preset cuda-release
cmake --build build/cuda-release --parallel 4
MODEL=/path/to/TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf
build/cuda-release/inferscale_llama_server \
  --model "$MODEL" --host 127.0.0.1 --port 8080 \
  --ctx-size-per-seq 2048 --parallel 16 --batch-size 2048 \
  --ubatch-size 2048 --gpu-layers 99 --threads 8 \
  --threads-batch 8 --threads-http 4 --scheduler fifo
```

The strict `/v1/completions` API streams SSE and accepts caller-rendered prompt
text. Sampling is greedy. The model file is external and is never committed.
For clean-checkout tests and a bounded model-backed smoke, install the pinned
Python dependencies and run:

```sh
python3 -m pip install -r requirements-bench.txt
MODEL_PATH="$MODEL" scripts/reproduce_release.sh
```

The script fails on missing CUDA/GPU/model/dependency or checksum mismatch. It
uses fresh `/tmp` build trees, deterministic CPU/FakeEngine tests,
ASan/UBSan, and an open-loop eight-request GPU smoke protected by 65 C start
and 85 C stop gates. Full release reproduction and campaign details are in
[docs/REPRODUCTION.md](docs/REPRODUCTION.md).

## Evidence and limitations

- [Architecture](docs/ARCHITECTURE.md), [scheduling](docs/SCHEDULING.md),
  [logical KV accounting](docs/KV_ACCOUNTING.md), and [adaptive prefill](docs/ADAPTIVE_PREFILL.md)
  describe the runtime's owned behavior.
- [Failure model](docs/FAILURE_MODEL.md) states what recovers per request and
  what requires an external service restart.
- [Benchmark method](docs/BENCHMARK_METHODOLOGY.md) and
  [benchmark results](docs/BENCHMARKS.md) distinguish same-runtime FIFO
  comparisons from the external llama-server reference.
- [Limitations](docs/LIMITATIONS.md) and [resume evidence](docs/RESUME_EVIDENCE.md)
  keep claims within tested hardware and workload scope.

Validation covers one RTX 4060 Laptop GPU, 8,188 MiB VRAM, and TinyLlama-1.1B
Chat Q4_K_M. InferScale uses logical resource accounting; it does not implement
physical PagedAttention, multi-GPU or distributed serving, training, or
fine-tuning. The sustained Prompt-3 performance campaigns were invalidated at
the 85 C thermal stop; the planned soak was not run. Benchmark results do not
generalize beyond their recorded setup. SSE-piece intervals are not exact
token-level ITL. Automatic engine restart is unsupported; restart-required or
fatal backend state makes the service unhealthy for an external supervisor to
restart.
