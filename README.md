# InferScale

InferScale is a C++20 single-GPU inference runtime for decoder LLMs. It owns request lifecycle, admission, scheduling, cancellation, and SSE backpressure. The pinned llama.cpp library loads the model, owns physical KV state, and runs inference. The external llama-server is only a practical reference endpoint, not part of InferScale's request path.

## What the runtime does

- FIFO is the conservative default and same-runtime control.
- SA-WDS adds interactive/batch weighted service credits, bounded aging, deadline urgency, and decode protection.
- Chunked prefill lets the scheduler reconsider long prompts between exact, contiguous token ranges.
- KvBudgetManager accounts conservative logical sequence-token capacity, reserve, and commit. llama.cpp owns physical KV memory; this is not PagedAttention or physical page allocation.
- Adaptive prefill changes a bounded epoch prefill budget using hysteresis and cooldown. Static control remains available.
- Cancellation and overload stop future scheduling after reactor cancellation, retain in-flight ticket ownership until retirement, and reject work at explicit queue, sequence, or resource bounds.

RuntimeReactor is the single lifecycle and scheduler-state writer. RequestKey generations and BatchTicket pins prevent stale completions from mutating reused requests or sequences. The backend synchronizes and clears a sequence before its ID becomes reusable.

## Default policy and evidence

FIFO remains the recommended default. Prompt-2 development measurements on TinyLlama-1.1B Chat Q4_K_M and an RTX 4060 Laptop GPU showed a tradeoff: static SA-WDS C3 mean interactive TTFT p99 was 442 ms versus FIFO's 471 ms, while mean SLO goodput changed from 6.48 to 5.35 requests/s, output throughput from 258.23 to 218.07 tokens/s, and rejections from 25 to 43 per 240 offers. Adaptive C4 had a lower development p99 than static C3, with lower goodput and more rejections. These small-sample measurements remain development evidence, not release-grade estimates. See [benchmark results](docs/BENCHMARKS.md).

The final Prompt-3 campaign indexed 156 planned treatment arms: 151 remain `INVALID_THERMAL`; of the five arms that passed the campaign runner's thermal/request checks, four pass the final full-sample GPU-configuration check and one is `INVALID_CONFIG_MISMATCH`. No complete matched B/C/D trial survived: the trial-1 FIFO, Static SA-WDS, and Adaptive SA-WDS arms started at 47/61/59 C, a 14 C span above the frozen 5 C pair limit. No Prompt-3 p99, goodput, throughput, or rejection effect is estimated. FIFO stays the conservative engineering default; the evidence does not establish that it outperforms the other policies. SA-WDS and adaptive mode remain opt-in. The RTX 4060 Laptop could not yield a thermally matched comparative campaign under the frozen rules. A matched campaign on a better-cooled or dedicated benchmark host is optional publication-grade evidence work, not a software-release requirement. The `v1.0-resume-grade` tag denotes implementation completion, validation, and reproducibility; it does not claim publication-grade thermal performance proof. See the [campaign record](docs/BENCHMARKS.md) and [Gate A-L decision](docs/PROMPT3_GATES.md).

The dedicated FIFO resource soak ran for 20.26 minutes with 2,462 offers, zero invariant or hard-capacity violations, and clean request/sequence/lease drain. GPU memory flattened in the final ten minutes; host RSS stayed within the frozen drift allowance but continued rising, so an RSS plateau or absence of slow growth is not established. Gate C is PASS WITH LIMITATION for the tested workload. Gate G is PASS WITH LIMITATION: ASan/UBSan/LSan passed the supported suite; TSan required the documented per-process ASLR workaround and does not model Boost.Asio atomic_thread_fence. Gates H and I are PASS WITH LIMITATION: the campaign and analyzer preserve invalid evidence and refuse unmatched inference, while no thermally matched scheduler comparison was obtained. See [resource soak evidence](docs/RESOURCE_SOAK.md) and [benchmark method](docs/BENCHMARK_METHODOLOGY.md).

Prompt-2 profiling found backend CUDA execution and synchronization dominate; InferScale batch-plan construction was about 0.036% of the backend execution range. A buffer-reuse idea did not justify persistent complexity, and no custom CUDA optimization or speedup claim was retained. See [profiling](docs/profiling/README.md).

## Architecture Maps

Final maps of the implemented release, with GitHub Mermaid diagrams. Each links to its Archify source spec; the interactive HTML viewers are generated locally rather than committed (see [archify-maps/README.md](archify-maps/README.md)).

- [System map](ARCHIFY_SYSTEM_MAP.md)
- [Request flow](ARCHIFY_REQUEST_FLOW.md)
- [Scheduler map](ARCHIFY_SCHEDULER_MAP.md)
- [Resource lifecycle](ARCHIFY_RESOURCE_LIFECYCLE.md)
- [Failure and cancellation](ARCHIFY_FAILURE_AND_CANCELLATION_MAP.md)
- [Benchmark and evidence](ARCHIFY_BENCHMARK_AND_EVIDENCE_MAP.md)
- [Codebase map](ARCHIFY_CODEBASE_MAP.md)

## Build and run

Prerequisites are C++20, CMake, Ninja, CUDA 13, and the external model file. CMake pins llama.cpp v0.4.1 at commit b29c606e28a01b1bc8c1351026a0fa6e616bf6c4.

~~~sh
cmake --preset cuda-release
cmake --build build/cuda-release --parallel 4
MODEL=/path/to/TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf
build/cuda-release/inferscale_llama_server \
  --model "$MODEL" --host 127.0.0.1 --port 8080 \
  --ctx-size-per-seq 2048 --parallel 16 --batch-size 2048 \
  --ubatch-size 2048 --gpu-layers 99 --threads 8 \
  --threads-batch 8 --threads-http 4 --scheduler fifo
~~~

The strict /v1/completions API streams SSE and accepts caller-rendered prompt text. Sampling is greedy. The model file is external and is never committed. For a clean-checkout build, tests, and bounded model-backed smoke, install pinned Python dependencies and run:

~~~sh
python3 -m pip install -r requirements-bench.txt
MODEL_PATH="$MODEL" scripts/reproduce_release.sh
~~~

The clean CUDA/Ninja Release build used runtime commit `850db0c05bc3f2f87a0ad14649b1e35034a4d594`; its bounded model-backed smoke completed 8/8 requests at 62 C start, 68 C peak, and 2.295 ms maximum sender lateness. This is reproduction evidence only, not performance evidence. The analysis-only release hardening at `86e4f46dbea5249311acecca28b08cf0991355ea` passed a separate clean-worktree Python run, 42/42 with no skips. The host NVIDIA driver stopped responding after the smoke. Details are in [reproduction](docs/REPRODUCTION.md).

## Evidence and limitations

- [Architecture](docs/ARCHITECTURE.md), [scheduling](docs/SCHEDULING.md), [logical KV accounting](docs/KV_ACCOUNTING.md), and [adaptive prefill](docs/ADAPTIVE_PREFILL.md) describe owned behavior.
- [Failure model](docs/FAILURE_MODEL.md) states what recovers per request and what requires an external service restart.
- [Benchmark method](docs/BENCHMARK_METHODOLOGY.md) separates same-runtime FIFO comparisons from external llama-server context.
- [Limitations](docs/LIMITATIONS.md) and [resume evidence](docs/RESUME_EVIDENCE.md) keep claims within tested hardware and workload scope.

Validation covers one RTX 4060 Laptop GPU with 8,188 MiB VRAM and TinyLlama-1.1B Chat Q4_K_M. InferScale uses logical resource accounting; it does not implement physical PagedAttention, multi-GPU or distributed serving, training, or fine-tuning. SSE-piece intervals are not exact token-level ITL. Automatic engine restart is unsupported; restart-required or fatal backend state makes the service unhealthy for an external supervisor to restart.
