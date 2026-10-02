# Phase 3 — KV Budgeting, Chunked Prefill, and SLO-Aware Adaptive Scheduling

## Goal
Turn the safe FIFO runtime into the project-defining scheduler: hard/soft KV control, bounded prefill chunks, decode protection, weighted service fairness, aging, SLO measurements, and overload-aware admission.

## Duration target
**8–12 focused days**.

## Part A — Calibrate usable backend capacity

Do not invent a physical page pool.

Determine a conservative `hard_usable_capacity` for the pinned model/context/backend configuration using:

- configured context and parallel-sequence constraints;
- backend capabilities;
- observed memory behavior;
- a safety reserve.

Store the calibration method and resulting value in the benchmark/config manifest.

## Part B — `KvBudgetManager`

Implement per-request leases:

```text
soft reserved
committed
peak
released
```

### Admission
Reserve a configurable amount based on:

```text
remaining prompt
+ generation reserve
+ safety margin
```

### Runtime growth
Before every work item that grows sequence state:

```text
if committed_global + expected_growth > hard_usable_capacity:
    do not schedule
```

Priority never bypasses this.

### Reservation adaptation
As requests progress:

- unused soft reserve can be reclaimed;
- actual committed growth updates the lease;
- high pressure can defer new admissions;
- the runtime can reconcile accounting with backend-observed sequence positions where supported.

### Pressure policy

```text
NORMAL        -> admit normally
SOFT_PRESSURE -> conservative admission
HIGH_PRESSURE -> prefer progress/completion; defer new large work
HARD_LIMIT    -> no additional growth until capacity frees
```

These are policy states, not promises that physical memory is paged.

## Part C — Chunked prefill

Introduce:

```text
max_prefill_chunk_tokens
max_prefill_tokens_per_epoch
```

A long prompt is represented as multiple exact `WorkItem` ranges.

Example:

```text
prompt: 6000 tokens
chunk: 512

[0,512)
[512,1024)
...
[5632,6000)
```

Decode-ready sequences can run between these chunks.

### Required invariant
`prefill_cursor` advances only after a corresponding batch ticket completes successfully and the runtime commits the work.

On a partial/recoverable backend result, reconcile before rescheduling rather than assuming the full chunk committed.

## Part D — SA-WDS scheduler

Implement the named adaptive policy from component design.

### Class service credits
At each epoch:

```text
credit[class] += weight[class]
credit[class] = min(credit[class], cap[class])
```

Selected work spends cost proportional to scheduled tokens or an empirically justified service unit.

### Urgency ordering
Within a class use deterministic lexicographic ordering:

```text
starvation breached?
deadline slack bucket
age bucket
arrival order
request key
```

### Decode protection
When decode-ready work exists, reserve a minimum epoch share for it before filling with prefill chunks.

### Memory awareness
A request whose next work item cannot pass hard growth authorization is temporarily ineligible. Do not boost it into unsafe execution.

### Stable determinism
Given the same snapshot/config/clock, the policy must produce the same `WorkPlan`.

## Part E — Adaptive prefill-budget feedback

Add a bounded controller around the static-safe scheduler. The controller observes a coarse control window and may adjust only `max_prefill_tokens_per_epoch` between configured limits.

Inputs may include:

```text
interactive wait/TTFT EWMA
decode backlog
executor idle fraction
oldest batch wait
```

Policy requirements:

```text
high/low hysteresis
minimum N windows before change
fixed step size or bounded proportional step
cooldown after change
full trace of decisions
static fallback mode
```

It must never alter hard KV capacity or bypass fairness/admission invariants. Benchmark it against an otherwise identical **static prefill-budget** variant. If feedback does not improve a declared metric without harmful oscillation/regression, keep static mode as the v1 default and document the result.

## Part F — Admission under overload

Add bounded behavior for:

- queue full;
- impossible context request;
- request whose minimum viable work cannot fit;
- soft memory pressure;
- missed deadline before service;
- request predicted to miss its class SLO under a documented optional admission heuristic.

Keep predictive SLO rejection optional until validated; deterministic safety rules come first.

## Part G — Goodput/SLO definitions

Example benchmark declarations:

```text
interactive:
  TTFT <= 750 ms
  p99 scheduler wait <= 500 ms

batch:
  starvation threshold <= 5 s
```

Exact thresholds depend on model/hardware and must be fixed **before** comparative runs, not chosen after seeing results.

Goodput:

```text
goodput_rps = completed requests meeting declared SLO / wall-clock second
```

Also report raw throughput so latency improvements are not achieved by silently starving work.

## Part H — Fairness tests

Test continuous mixed load where interactive work never stops arriving.

The batch class must still make progress according to the configured service weights/aging policy.

Measure:

- maximum wait;
- p99 wait;
- service share;
- starvation events;
- interactive slowdown;
- batch slowdown.

## Property/invariant tests

Randomized operations over thousands of deterministic seeds:

```text
admit
schedule prefill chunk
schedule decode
complete ticket
cancel
expire deadline
inject recoverable failure
release
reuse request/sequence slot
change pressure state
```

After every operation assert:

```text
committed <= hard capacity
no released lease is committed/grown again
no stale generation mutates current state
no terminal request is newly scheduled
no sequence slot reused while old ticket exists
prefill cursor never skips uncommitted input
queue/accounting totals match request table
```

## Benchmark matrix

Run at minimum:

```text
B. InferScale FIFO
C1. adaptive scheduling, no chunking
C2. adaptive + chunked prefill
C3. adaptive + chunked prefill + KV-aware admission
C4. C3 + bounded adaptive prefill-budget controller
```

Across:

- low load;
- saturation region;
- burst overload;
- short+long mixed workload;
- memory-pressure workload;
- cancellation workload.

This makes the value of each feature visible.

## Required evidence

At least one final chart should show:

```text
interactive p99 TTFT vs offered load
+
batch starvation/goodput on same workload
```

Another should show:

```text
KV committed/reserved pressure over time
+
admission/rejection events
```

## Optional feature: prefix reuse
Do **not** implement in the core Phase-3 gate. If later added, it must have an explicit ownership model, correctness tests, and an ablation separate from scheduling.

## Exit gate
Phase 3 is complete only when:

- long prompts are truly chunked by token range;
- hard KV growth cannot be overridden by priority;
- adaptive policy is deterministic;
- batch work makes progress under sustained interactive traffic;
- memory-pressure tests produce controlled defer/reject behavior rather than avoidable OOM;
- cancellation still passes all ticket/generation tests;
- goodput, fairness, throughput, and p99 effects are all measured;
- ablations isolate chunking and adaptive scheduling from unrelated runtime changes.
