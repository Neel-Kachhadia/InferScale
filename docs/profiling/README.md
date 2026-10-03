# Profiling evidence

This directory records the profiler manifest, compact trace interpretation,
optimization experiment note, and outcome. Large Nsight captures and temporary
CPU profiles remain under ignored `build/` and are referenced by path and
checksum rather than committed.

## Manifest fields

For every representative capture record:

```text
run_id:
commit:
model_sha256:
llama_cpp_commit:
runtime_config:
workload:
seed:
offered_rate_rps:
host_kernel:
gpu:
driver:
cuda_toolkit:
gpu_temperature_before_after_c:
gpu_memory_before_peak_after_mib:
nsys_version:
nsys_trace_path:
nsys_trace_sha256:
cpu_profiler:
cpu_profile_path:
cpu_profile_sha256:
```

## Required interpretation

The profile note identifies CPU scheduling/batch construction, synchronization,
GPU launch cadence and idle gaps, backend execution, stream pressure, and
resource stalls. It distinguishes project-owned cost from llama.cpp/model
compute. If CPU sampling is blocked by host policy, record the exact limitation
and use bounded runtime/NVTX durations; do not alter kernel settings or
permissions to enable profiling.

Before an optimization is retained, record the measured bottleneck, profile
run ID, hypothesis, proposed change, expected end-to-end metric, correctness
risk, and rollback condition. The before/after ablation changes only that
optimization. A measured negative result is valid evidence to keep the simpler
implementation.

Phase-4 measured results and trace locations are recorded in
[PROFILE_PHASE4.md](PROFILE_PHASE4.md). The measured optimization decision is
in [OPTIMIZATION_EXPERIMENT.md](OPTIMIZATION_EXPERIMENT.md). The actual trace is
kept locally under ignored `build/phase3-evidence/` and identified by SHA-256;
the compact source-controlled notes are the canonical interpretation.
