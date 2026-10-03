# Reproducing release evidence

The release reproduction script requires a Linux host with C++20, CMake,
Ninja, CUDA 13, `nvcc`, `nvidia-smi`, the pinned Python dependencies, and the
exact external TinyLlama GGUF. It fails if the GPU, model checksum, backend pin,
or a build/test prerequisite is missing; it does not fall back to a CPU backend.

```sh
python3 -m pip install -r requirements-bench.txt
MODEL_PATH=/path/to/TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf \
  scripts/reproduce_release.sh
```

The script creates fresh CUDA and ASan/UBSan build directories under `/tmp`,
fetches llama.cpp at the exact commit in `cmake/Dependencies.cmake`, builds,
runs all CTest targets with the model-backed integration tests enabled, runs
the sanitizer CPU suite with LeakSanitizer disabled because this validation
host's ptrace setup causes LSan to abort during teardown, validates
workload/schema inputs, then runs a small
real-GPU open-loop smoke and generates its indexed summary/figures. It does
not use the repository's ignored `build/` outputs. Network access is required
for the first pinned llama.cpp fetch unless CMake's documented FetchContent
source override is supplied by the operator.

The Python packages used by the benchmark are pinned in
[`requirements-bench.txt`](../requirements-bench.txt). The C++ direct
dependencies and backend revision are exact CMake pins. The release campaign
manifest records compiler, CUDA, GPU/driver, model hash, backend hash, server
binary hash, workload hash, requested configuration and each run's backend
and runtime-resolved limits, trial seeds/order,
sender-lateness rules, and each command line.

For paired performance evidence, use `scripts/run_release_campaign.py`. Keep
the arrival seed paired across FIFO, static SA-WDS, and adaptive SA-WDS. Use
five or more independent trials, randomize policy order, and preserve every
run index and raw request/metrics file locally. Then run
`bench/release_analysis.py` on the exact run-index files. Confidence intervals
resample paired trials, not individual requests. The analysis also writes
per-trial p99 and request counts; a pooled request CDF is labeled descriptive.

CI runs CPU/FakeEngine correctness, bounded parser mutation, Python benchmark
and workload checks, ASan/UBSan, and a tracked-file secret scan. It does not
claim GPU coverage or simulate CUDA. Local GPU results and profiling traces
are release evidence for the recorded machine only.
