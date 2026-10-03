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
runs the CPU/FakeEngine runtime, policy, and HTTP CTest targets, runs the
sanitizer CPU suite with LeakSanitizer disabled because this validation host's
ptrace setup causes LSan to abort during teardown, validates workload/schema
inputs, then runs a bounded `reproduction_smoke` model-backed open-loop GPU
trial through the campaign runner's 65 C start and 85 C stop gates and
generates an indexed, smoke-labeled summary. It does not generate policy
figures from the smoke. The full model-backed CUDA CTest results are tracked as a
separate validation run; the clean reproduction avoids repeating longer GPU
integration scenarios. It does not use the repository's ignored `build/`
outputs. Network access is required
for the first pinned llama.cpp fetch unless CMake's documented FetchContent
source override is supplied by the operator.

The Python packages used by the benchmark are pinned in
[`requirements-bench.txt`](../requirements-bench.txt). The C++ direct
dependencies and backend revision are exact CMake pins. The release campaign
manifest records compiler, CUDA, GPU/driver, model hash, backend hash, server
binary hash, workload hash, requested configuration and each run's backend
and runtime-resolved limits, trial seeds/order,
sender-lateness rules, and each command line.

For paired performance evidence, use `scripts/run_release_campaign.py` with
`--purpose release_comparison`. Keep the arrival seed paired across FIFO,
static SA-WDS, and adaptive SA-WDS. Randomize policy order, require resolved
configuration to match each treatment, keep paired starts within 5 C, and
preserve every run index and raw request/metrics file locally. Then run
`bench/release_analysis.py` on the exact run-index files. A 95% interval needs
at least seven independent paired trials; headline interactive p99 also needs
at least 10,000 successful TTFT observations in each arm. Rejection and
completion effects accompany successful-response p99. Confidence intervals
resample paired trials, not individual requests. The analysis writes per-trial
p99 and request counts; a pooled request CDF is labeled descriptive. The
reproduction script marks its small run `reproduction_smoke`, which is never a
policy comparison.

The latest clean reproduction used detached source commit
`887bb0b70c56aaa05326526c1b12c2836d40eab2`. It passed the clean CUDA configure
and build, CPU CTest 3/3, ASan/UBSan CTest 3/3, Python tests 26/26, schema and
workload checks, and an eight-request model-backed smoke. That run began at
57 C and peaked at 59 C, with 2.424 ms maximum sender lateness. Analysis
classified it as one valid `reproduction_smoke` and zero policy-comparison
runs. The request records, server metrics, GPU samples, manifests, and smoke
analysis are committed under
`artifacts/release-v1/reproduction/clean-smoke/20261003T162240Z/`; earlier
clean smokes are also preserved there. None is a performance estimate. The
analyzer's paired regression test also covers the direct adaptive-versus-static
comparison and its rejection count. Captured console logs remain local and are
indexed by path and checksum; they are excluded from Git as raw run logs.

CI runs CPU/FakeEngine correctness, bounded parser mutation, Python benchmark
and workload checks, ASan/UBSan, and a tracked-file secret scan. It does not
claim GPU coverage or simulate CUDA. Local GPU results and profiling traces
are release evidence for the recorded machine only.
