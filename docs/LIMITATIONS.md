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
- AddressSanitizer and UBSan pass the supported C++ suites. LeakSanitizer runs
  the tests but exits during teardown on this host with `LeakSanitizer does not
  work under ptrace`; leak detection is therefore not claimed. The soak
  harness is configured to sample RSS, VRAM, logical lease charge, and sequence
  return, but it was not executed in this validation; no sustained soak result
  exists.
- NVIDIA's `power.limit` query alias returns `N/A` on this laptop; the campaign
  records the supported `enforced.power.limit` (100 W), default limit, maximum
  limit, average draw, and instantaneous draw separately. One idle query
  returned an impossible 590 W instantaneous reading against a 140 W maximum;
  raw samples are retained and impossible instantaneous values invalidate
  only the instantaneous-power summary. No power comparison is made from such
  a run.
- The final sustained Prompt-3 16 RPS interactive and 8 RPS mixed attempts
  reached the 85 C stop during their first treatment. They are invalid and
  excluded. The in-tree eight-request smoke peaked at 57 C; the first clean
  reproduction started at 61 C and peaked at 67 C; a fresh clean reproduction
  of source commit `27963bcd` started at 59 C and peaked at 64 C; the final clean
  reproduction at `26a5cce` started at 56 C and peaked at 59 C; the latest
  clean reproduction at `887bb0b` started at 57 C and peaked at 59 C. Each
  validates only a short execution path and none establishes sustained thermal
  validity. The soak and release-scale policy campaign remain unrun; the
  v1.0-resume-grade tag was not created.
- The laptop-specific thermal stop means release-grade scheduler performance
  remains unproven on this machine. Prompt-2 three-seed/80-offer results are
  explicitly development evidence only; they do not replace the frozen
  Prompt-3 sample and trial requirements.
- The load generator observes warm-up completion at `[DONE]`, but the campaign
  does not explicitly wait for the periodically refreshed metrics snapshot to
  include that request before taking the trial baseline. A few warm-up
  work-item waits could therefore enter a scheduler-wait histogram delta. Do
  not interpret that metric as a precise per-request wait population.
- GPU telemetry coverage counts successful query rows. Missing or `N/A`
  thermal-throttle reason fields do not independently invalidate a run and can
  be treated as no reported slowdown. Future sustained thermal claims should
  validate required telemetry fields or state where they are unavailable.
- `bench/analyze.py` is a convenience summary tool, not an independent
  raw-record forensic validator. Release comparisons use the fail-closed
  `bench/release_analysis.py`; neither produced a Prompt-3 treatment estimate.
