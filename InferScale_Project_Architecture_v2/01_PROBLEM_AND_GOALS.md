# InferScale — Problem Definition and Engineering Goals

## 1. Problem statement

A real inference server receives heterogeneous work:

- short interactive prompts with strict first-token expectations;
- long prompts that create expensive prefills;
- short and long generations;
- requests with different priority/SLO classes;
- requests that disconnect, expire, or cancel;
- bursts that temporarily exceed service or memory capacity;
- concurrent sequences whose KV growth is not known exactly at admission time.

The hard problem is not merely producing tokens. It is deciding **what work runs next, what work waits, what can be safely admitted, what must be cancelled, and what evidence proves the policy is better**.

### Failure mode A — prefill interference
A multi-thousand-token prompt can monopolize an execution step and inflate interactive TTFT unless prefill work is chunked and budgeted.

### Failure mode B — queue collapse
A server can look fast below saturation and then accumulate unbounded queue delay. InferScale must expose the saturation knee and apply bounded overload behavior before memory or latency collapses.

### Failure mode C — memory overcommit
Initial prompt length is known, future generation length is not. Admission must use conservative soft reservations while every subsequent KV growth remains subject to a non-negotiable hard capacity check.

### Failure mode D — cancellation waste and lifetime races
An HTTP disconnect can occur while GPU work still references the sequence. Reusing request/sequence resources before the in-flight batch completes risks corruption; continuing to schedule the cancelled request wastes compute.

### Failure mode E — unfair scheduling
Pure priority can starve batch traffic. Pure FIFO can make interactive work wait behind expensive prefills. Pure shortest-job heuristics can punish long requests indefinitely.

### Failure mode F — misleading performance evidence
Closed-loop clients can hide overload through coordinated omission, p99 from a few hundred requests is statistically weak, and a custom kernel can improve a microbenchmark while regressing end-to-end latency.

## 2. Engineering objective

Build a direct-`libllama` single-GPU runtime that makes the following decisions explicitly and reproducibly:

1. whether a request is admitted, deferred, or rejected;
2. how much soft KV capacity is reserved and how hard growth is enforced;
3. which prefill token range or decode token is scheduled in the next epoch;
4. how interactive and batch classes share service while guaranteeing aging/fairness;
5. how long prefills are split into bounded chunks and how the prefill budget adapts to measured SLO pressure/headroom;
6. how cancellation/deadline events interact with in-flight GPU execution;
7. when backend errors are request-scoped, engine-recoverable, restart-required, or fatal;
8. when the server applies backpressure or overload rejection;
9. which optimization deserves implementation based on profiling;
10. whether the resulting policy increases **SLO goodput**, not just raw throughput.

## 3. Primary success metrics

### User-visible latency

- queue delay;
- time to first token (TTFT);
- inter-token latency (ITL);
- end-to-end latency;
- p50 / p90 / p95 / p99;
- SLO attainment rate by traffic class.

### Throughput and goodput

- output tokens/s;
- total processed tokens/s;
- completed requests/s;
- **goodput**: requests/s or tokens/s completed while satisfying the workload's declared SLO.

### Resource efficiency

- GPU active/utilization timeline;
- GPU memory model-load baseline and peak;
- logical KV committed/reserved/free capacity;
- batch size/token occupancy per epoch;
- prefill/decode service share;
- executor idle gaps;
- host scheduling/batch-build time;
- transfer/synchronization time when applicable.

### Fairness

- maximum queue/scheduler wait by class;
- p99 scheduler wait by class;
- class slowdown relative to isolated service;
- starvation-threshold violations;
- service-share error relative to configured weights.

### Reliability

- avoidable OOM count;
- rejected/deferred count by reason;
- cancellation-to-stop latency;
- cancellation-to-resource-release latency;
- tokens computed after cancellation;
- double release / stale generation / invariant violation count;
- engine restarts and fatal errors;
- memory growth across soak cycles.

## 4. Explicit non-goals for resume-grade v1

- model training or fine-tuning;
- multi-node serving;
- tensor/pipeline parallelism;
- custom tokenizer;
- new transformer implementation;
- physical PagedAttention/paged-KV implementation;
- speculative decoding as a required feature;
- prefix-block sharing as a required feature;
- Kubernetes/platform engineering;
- custom kernels without a measured bottleneck.

These are possible future projects, not prerequisites for a strong v1.

## 5. Falsifiable hypotheses

### H1 — chunked prefill reduces interference
At the same model, backend, and offered load, bounding prefill work per epoch should reduce interactive p95/p99 TTFT and interactive slowdown under mixed short/long traffic compared with unchunked prefill.

### H2 — SLO-aware fair scheduling improves useful service
Compared with InferScale FIFO, the adaptive scheduler should increase SLO goodput for interactive work while keeping batch starvation violations at zero (or under a declared bound) and retaining an explicitly measured fraction of aggregate throughput.

### H3 — hard/soft KV control prevents avoidable memory failure
A hard capacity invariant plus conservative soft reservation and runtime growth checks should reject/defer overload before backend KV allocation failure in stress cases that would otherwise overcommit.

### H4 — generation-fenced cancellation prevents lifetime corruption
Cancellation during in-flight execution should stop future scheduling within a bounded number of epochs, release resources exactly once after the final referencing batch retires, and produce zero stale-generation writes under deterministic race tests and sanitizer runs.

### H5 — profiling-guided optimization improves end-to-end behavior
At least one retained optimization must be justified by Nsight/perf evidence and improve an end-to-end metric or reduce CPU/GPU idle overhead under a controlled benchmark. A rejected optimization is acceptable if evidence explains why it did not help.

### H6 — bounded feedback adaptation responds without instability
A prefill-budget feedback controller should reduce interactive SLO pressure when queue/TTFT stress persists and recover throughput when headroom returns, while remaining inside fixed safety bounds and avoiding rapid oscillation through hysteresis/cooldown.

### H7 — policy effect survives ablation
The measured benefit of adaptive scheduling must remain visible when unrelated variables such as HTTP stack, model, tokenizer, sampler, backend commit, and memory configuration are held constant.

## 6. Scientific controls

The final study separates:

- **External reference:** pinned `llama-server` configuration.
- **Internal control:** InferScale runtime + FIFO policy.
- **Treatment:** InferScale runtime + adaptive scheduler.
- **Ablations:** remove chunking, aging, memory-aware admission, or measured optimization one at a time.

This prevents the project from attributing backend or server implementation differences to the scheduler.

## 7. Interview questions the repository must answer with evidence

- What does InferScale own that llama.cpp does not own in this experiment?
- Why use direct `libllama` instead of wrapping `llama-server`?
- What is the exact scheduling algorithm and what fairness property does it provide?
- How is an 8k-token prompt split when the prefill epoch budget is 512 tokens?
- What prevents a high-priority request from overriding physical memory safety?
- What happens when a generation exceeds its soft KV reservation?
- What prevents request-ID reuse from corrupting an old in-flight GPU batch?
- How quickly does cancellation stop additional compute and release resources?
- What happens after a recoverable backend failure versus a fatal CUDA failure?
- How does the load generator avoid coordinated omission?
- How many observations support the reported p99 and how are confidence intervals produced?
- Which optimization was selected from profiling evidence, and what tradeoff did it introduce?

If the implementation cannot answer these questions in code, tests, traces, and raw benchmark artifacts, the project is not exceptional yet.
