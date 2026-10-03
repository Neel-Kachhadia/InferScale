# Limitations

- InferScale is a single-GPU, decoder-oriented serving runtime. Validation is
  on one NVIDIA GeForce RTX 4060 Laptop GPU with 8,188 MiB VRAM.
- The real-model validation uses TinyLlama-1.1B-Chat Q4_K_M and pinned llama.cpp
  v0.4.1 commit `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`.
- llama.cpp owns physical KV state and execution buffers. InferScale's
  `KvBudgetManager` tracks conservative logical sequence-token reservations and
  commits; this is not physical page accounting and is not PagedAttention.
- There is no multi-GPU or distributed serving, training, fine-tuning,
  prefix cache, or speculative decoding.
- Sampling is greedy. The HTTP API takes caller-rendered prompt text and does
  not apply a model chat template.
- HTTP output is SSE. Content-piece interarrival is only a proxy; it is not
  exact token-level ITL. Client TTFT is measured to the first content-bearing
  SSE event from intended open-loop arrival.
- The API supports output limits through 128 tokens. Request bodies are capped
  at 64 KiB and prompt strings at 32 KiB.
- Class token credits are not GPU-time fairness. The 5-second batch threshold
  is an operational starvation alarm, not a real-time service guarantee.
- SLO and benchmark conclusions apply only to the recorded model, backend,
  hardware, configuration, and synthetic workloads. External llama-server is
  useful system context but is not scheduler-causal evidence.
- Adaptive prefill changes only a bounded scheduling budget. Its benefit and
  rejection/goodput tradeoff are workload dependent; it remains opt-in unless
  final paired evidence supports otherwise.
- Profiling found llama.cpp CUDA execution and synchronization dominate.
  No custom CUDA optimization was retained because the measured batch-plan
  build was only 0.036% of backend execution range; no CUDA speedup is claimed.
- Automatic engine restart is unsupported. Restart-required and process-fatal
  backend failures make the service unhealthy and require an external process
  or service restart.
- TSan runs pass with a per-process ASLR workaround on the validation host, but
  GCC warns that Boost.Asio `atomic_thread_fence` calls are not instrumented.
  This is a scope limitation in race detection, not a blanket TSan proof.
- AddressSanitizer and UBSan pass the supported C++ suites. LeakSanitizer runs the
  tests but exits during teardown on this host with `LeakSanitizer does not work
  under ptrace`; leak detection is therefore not claimed. The soak reports RSS,
  VRAM, logical lease charge, and sequence return as separate leak evidence.
- GPU power-limit telemetry is `N/A` on this laptop; power draw is recorded
  when the driver reports it. Temperature, clocks, utilization, memory, and
  thermal throttle reasons are used for run validity.
