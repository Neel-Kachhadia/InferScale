# Phase 1 — External Baseline, Benchmark Harness, and Embedded Engine Proof

## Goal
Establish trustworthy measurement before optimization and prove that InferScale can drive the pinned backend directly without inheriting `llama-server` scheduling behavior.

## Duration target
**3–5 focused days** for the core gate; additional time is acceptable for benchmark-harness hardening.

## 1. Pin the experimental environment

Record and commit a machine-readable environment manifest containing:

```text
OS + kernel
CPU model
RAM
GPU model + VRAM
GPU driver
CUDA toolkit/runtime
llama.cpp commit
compiler + version
CMake/Ninja versions
Python lockfile
model identifier + checksum + quantization
```

Do not use a floating backend revision after baseline collection begins.

## 2. Build external reference `llama-server`

Build the pinned backend unmodified and preserve the exact CLI used for every baseline run.

Capture at minimum:

```text
context size
parallel slots
batch/ubatch settings
continuous batching setting
flash-attention setting
GPU offload
sampling config
thread counts
```

The external reference answers: **how does InferScale compare with a mature server?** It is not the scientific control for scheduler causality.

## 3. Build the benchmark/load generator first

Implement a Python async client that supports:

### Arrival modes

- fixed-concurrency closed loop;
- timestamp-driven open loop;
- Poisson-like open-loop arrivals;
- deterministic bursts.

### Workload dimensions

- short/medium/long/very-long prompts;
- short/long generation limits;
- interactive/batch priority classes;
- deadline/SLO labels;
- cancellation/disconnect probability;
- deterministic seeds.

### Required workload files

```text
interactive_short.yaml
long_context.yaml
mixed_slo.yaml
burst.yaml
cancel.yaml
slow_reader.yaml
```

Prefix-heavy workload is optional until prefix reuse becomes an implemented feature.

## 4. Avoid coordinated omission

The open-loop generator must precompute or independently schedule intended arrival timestamps.

For every request record:

```text
intended_arrival
actual_send
accepted
first_token
last_token
status
```

Report both server response latency and arrival lateness in the load generator. A run with an overloaded client host is invalid.

## 5. Raw event schema

Every benchmark request should produce a record with at least:

```text
run_id
sample_id
seed
priority_class
prompt_tokens
requested_output_tokens
actual_output_tokens
intended_arrival_ns
send_ns
accepted_ns
first_token_ns
completion_ns
status
cancelled
error_class
```

InferScale-specific runs later append scheduler/KV fields.

## 6. Baseline metrics

Collect:

- queue/accept latency where observable;
- TTFT p50/p90/p95/p99;
- E2E p50/p90/p95/p99;
- ITL distribution;
- output tokens/s;
- requests/s;
- GPU memory peak;
- GPU utilization/timeline;
- CPU utilization;
- SLO attainment/goodput for declared SLO workloads.

## 7. Statistical baseline protocol

For final-quality baseline data:

- warm model and run a documented warm-up;
- use enough measured requests that p99 is not based on only a handful of samples;
- target **>=10,000 requests** for the principal p99 workload when runtime permits, or document why fewer are statistically acceptable;
- run at least 5 repeated trials for headline comparisons, preferably 7+;
- report median across trials plus bootstrap confidence intervals or another documented uncertainty method;
- preserve every run, including regressions.

Development smoke runs can be much smaller but may not support resume claims.

## 8. Embedded-engine proof

Create the smallest direct-`libllama` executable that:

1. loads the same pinned model;
2. tokenizes a prompt;
3. creates a backend sequence;
4. builds a sequence-aware batch;
5. performs prefill;
6. executes several decode steps;
7. samples/returns tokens;
8. clears the sequence;
9. shuts down cleanly.

This is not InferScale yet. It proves the project can own scheduling above `libllama` instead of delegating to `llama-server`.

## 9. Baseline saturation sweep

For at least one representative mixed workload, sweep offered load until:

```text
underutilized -> efficient region -> saturation knee -> overload
```

The benchmark harness must show throughput, p99 TTFT, goodput, and rejection/error behavior across the sweep.

## 10. Thermal and power validity

For laptop/thermally constrained GPUs, record:

- GPU temperature;
- GPU clock range;
- power state/limit where available;
- whether a run throttled.

Invalidate or flag trials whose thermal/power behavior differs materially between baseline and treatment.

## Tests

- deterministic workload generation for a fixed seed;
- open-loop arrivals remain independent of response latency;
- malformed SSE response handling;
- client cancellation timer accuracy;
- raw record completeness;
- manifest schema validation;
- statistics code verified against synthetic known distributions.

## Deliverables

```text
bench/
  run.py
  analyze.py
  client.py
  schema/
  workloads/
  releases/baseline/

docs/
  BASELINE.md
  BENCHMARK_METHODOLOGY.md

src/engine/
  llama_engine_smoke.cc (or equivalent proof target)
```

## Exit gate
Phase 1 is complete only when:

- the pinned external reference can be reproduced from one documented command sequence;
- raw request-level data is preserved;
- open-loop generation is coordinated-omission-safe;
- a saturation curve can be generated;
- the direct `libllama` proof runs on the target GPU;
- the model/backend/hardware manifest is complete;
- no performance claim has yet been attributed to InferScale.
