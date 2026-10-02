# InferScale — Definition of Done

InferScale is not complete because it serves tokens. It is complete when correctness, policy causality, overload behavior, statistical evidence, and reproducibility all survive explicit gates.

## Gate A — Architecture ownership

- [ ] InferScale drives a pinned direct `libllama` adapter rather than hiding behind `llama-server` internally.
- [ ] `llama-server` remains a separately built external reference.
- [ ] request/scheduler/KV policy ownership boundaries match the documentation.
- [ ] v1 makes no physical paged-KV claim.

## Gate B — Lifecycle and concurrency correctness

- [ ] runtime reactor is the only request-state writer;
- [ ] every asynchronous request reference carries generation identity;
- [ ] batch tickets fence resource reuse;
- [ ] terminal resources release exactly once;
- [ ] stale generation events cannot mutate current requests;
- [ ] cancellation race tests pass;
- [ ] graceful shutdown has a bounded deadline.

## Gate C — Memory/resource safety

- [ ] hard usable KV capacity is calibrated/documented;
- [ ] hard growth cannot be overridden by priority;
- [ ] soft reservations are accounted/reclaimed;
- [ ] backend state reconciliation exists where supported;
- [ ] queue and stream buffers are bounded;
- [ ] repeated cycles show no unexplained monotonic host/GPU growth;
- [ ] memory-pressure workloads produce controlled overload rather than avoidable OOM.

## Gate D — Scheduler implementation

- [ ] same-runtime FIFO control exists;
- [ ] exact `WorkItem` token ranges exist;
- [ ] prefill is chunked and bounded;
- [ ] SA-WDS policy is deterministic;
- [ ] decode protection is tested;
- [ ] aging/fairness is tested under sustained mixed traffic;
- [ ] starvation-threshold events are observable;
- [ ] deadlines are explicitly handled;
- [ ] adaptive prefill-budget controller stays within bounds, has hysteresis/cooldown tests, and can fall back to static mode.

## Gate E — API/backpressure robustness

- [ ] async SSE works under concurrency;
- [ ] request body is bounded;
- [ ] per-stream output is bounded;
- [ ] slow readers suppress additional work or time out according to policy;
- [ ] disconnect becomes cancellation;
- [ ] health/readiness remain defined during saturation;
- [ ] unsupported API behavior is explicit, not silently ignored.

## Gate F — Failure/recovery model

- [ ] request-recoverable path tested;
- [ ] engine-recoverable path tested;
- [ ] engine-restart-required path tested;
- [ ] fatal path tested in a subprocess/controlled environment;
- [ ] unknown errors map conservatively;
- [ ] metrics sink failure cannot crash healthy inference;
- [ ] engine restart clears stale sequence mappings.

## Gate G — Memory/thread/tooling safety

- [ ] ASan clean;
- [ ] UBSan clean;
- [ ] TSan-compatible control-plane/fake-engine suite clean or justified suppressions documented;
- [ ] custom CUDA, if retained, passes Compute Sanitizer/relevant checks;
- [ ] fuzz targets run without reproducible crash for the release budget;
- [ ] compiler warnings are clean under project policy.

## Gate H — Benchmark integrity

- [ ] external llama-server reference retained;
- [ ] same-runtime FIFO control retained;
- [ ] adaptive treatment measured with identical backend/model settings;
- [ ] open-loop generator avoids coordinated omission;
- [ ] client send lateness is validated;
- [ ] p99 sample population is documented and defensible;
- [ ] repeated trials use a predeclared method;
- [ ] uncertainty/effect size reported;
- [ ] raw runs preserved;
- [ ] thermal/power anomalies flagged.

## Gate I — Performance evidence

- [ ] throughput vs load curve;
- [ ] SLO goodput vs load curve;
- [ ] p99 TTFT vs load curve;
- [ ] prefill-interference experiment;
- [ ] fairness/starvation experiment;
- [ ] KV pressure/overload experiment;
- [ ] cancellation stop/release measurement;
- [ ] ablation for adaptive scheduler;
- [ ] ablation for chunked prefill;
- [ ] ablation for KV-aware admission;
- [ ] static-vs-feedback prefill-budget ablation;
- [ ] Phase-4 optimization ablation if retained.

## Gate J — Profiling discipline

- [ ] NVTX coverage exists for main control/execution stages;
- [ ] representative Nsight Systems trace captured;
- [ ] one real bottleneck is identified;
- [ ] optimization hypothesis is documented before final result selection;
- [ ] retained optimization improves a meaningful end-to-end metric, **or** evidence shows why no custom optimization was justified;
- [ ] no microbenchmark-only result is presented as an end-to-end speedup.

## Gate K — Repository/reproducibility quality

- [ ] one documented build flow;
- [ ] one documented test/sanitizer flow;
- [ ] one documented benchmark/release reproduction flow;
- [ ] exact dependency/backend commits pinned;
- [ ] resolved configs stored with results;
- [ ] benchmark/workload schemas validated;
- [ ] ADRs document key design decisions;
- [ ] `docs/LIMITATIONS.md` exists;
- [ ] no secrets/model weights accidentally committed;
- [ ] CI covers CPU correctness and static hygiene.

## Gate L — Resume/interview integrity

Before any quantitative claim:

- [ ] metric comes from a valid causal/practical comparison;
- [ ] exact workload/hardware/model context is stated;
- [ ] result is reproducible across trials;
- [ ] raw evidence exists;
- [ ] uncertainty is available in repository evidence;
- [ ] tradeoff/regression is understood;
- [ ] claim does not say physical paging unless the research extension is actually implemented;
- [ ] author can explain one hard failure and its regression test.

# Completion levels

## Prototype
Direct model execution plus basic queueing/streaming.

Not sufficient for a high-stakes resume claim.

## Resume-grade
Passes Gates A–L and demonstrates at least one reproducible user/resource-visible improvement over the same-runtime FIFO control.

## Exceptional
In addition to resume-grade:

- large, statistically rigorous workload study;
- strong ablations and overload characterization;
- deterministic/model-based race testing;
- useful profiling-driven optimization or well-supported decision not to add one;
- excellent README/ADRs/failure-model documentation;
- release bundle reproducible on documented hardware;
- ideally one upstream issue/PR/benchmark contribution or independent technical write-up.

## Research-extension grade
Only use this label if the optional paged-KV work actually changes physical block ownership and the attention/memory path, includes equivalence/correctness tests, and is benchmarked independently from the core scheduler.
