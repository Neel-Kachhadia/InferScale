# Scheduling

InferScale keeps FIFO as the runtime default and scientific control, and offers
SA-WDS (SLO-Aware Weighted Deficit Scheduling) as an explicit policy through the same reactor,
`WorkPlan`, `BatchTicket`, executor, and backend path. Scheduler selection
changes ordering and chunk budgets; it does not change request ownership or
backend execution.

## Eligibility

The reactor constructs candidates from authoritative request records. A
request is ineligible when it is terminal, cancelled, expired, already has a
ticket in flight, has no remaining prompt/output work, is blocked by its
bounded output stream, cannot grow within its own soft reservation or global
hard headroom, or cannot satisfy the backend sequence/token limits. Eligibility
is checked again when a plan is issued. A stale plan cannot change lifecycle,
resource accounting, or service credit.

Decode work costs one scheduled token. Prefill work costs the number of prompt
tokens in its exact range. This is token-service fairness, not equal GPU-time
fairness: token positions do not have uniform compute cost across prompt and
decode work.

## Chunked prefill

Prefill work is represented by half-open prompt-token ranges. With a 6000-token
prompt and a 512-token chunk ceiling, the scheduler emits
`[0,512)`, `[512,1024)`, and so on, ending with the exact final partial range.
The epoch prefill budget, backend batch limit, chunk ceiling, class credit, and
resource authorization can shorten a range further.

Submitting a range does not advance `prefill_cursor`. The cursor advances only
after the matching generation-fenced ticket reports successful backend
execution and the reactor commits its exact growth. A failed or ambiguous
backend operation cannot skip or repeat a committed range; ambiguous backend
mutation remains charged until sequence clear succeeds.

## SA-WDS service policy

The two supported classes are `INTERACTIVE` and `BATCH`. Their configured
service quanta default to 1024 and 256 scheduled tokens, a 4:1 ratio. Credits
are class-level so submitting more requests in one class does not multiply
that class's share. A weighted round opens only after every currently active
class has spent its available credit. The quanta are then granted once, capped
by `credit_cap_tokens`; they are not granted on every scheduling epoch. This
lets a 128-token epoch carry service across multiple plans without saturating
both classes and repeatedly breaking ties in favor of Interactive. Empty
classes discard stale credit, so an idle class cannot bank a future monopoly.
Selected token cost is charged only when the reactor successfully issues the
matching plan. If hard-resource checks trim a plan, only retained work spends
credit. A failed or stale issue cannot spend credit.

Within a class, deterministic urgency orders candidates by starvation-bound
breach, deadline-slack bucket, bounded age bucket, arrival order, then
`RequestKey`. A candidate beyond the starvation bound may receive its next
eligible work item even when its class credit is exhausted; resource, sequence,
token, and backpressure checks still apply. Deadline buckets affect preference,
not hard-real-time guarantees. Aging uses reactor clock time; tests use a
manual clock and do not sleep. The same candidate snapshot, configuration,
credits, and time produce the same plan.

When decode-ready work exists, SA-WDS schedules a bounded decode share first.
The remaining epoch budget admits prefill chunks; unused decode protection is
available to prefill, and leftover capacity can serve more decode. Credits
still apply to both classes. Thus decode has protection without a permanent
monopoly over long prompts. Once a prefill candidate crosses its starvation
bound, its next eligible chunk reserves one token and one sequence opportunity
ahead of decode; normal decode protection resumes on the following plan.

Hard KV eligibility is applied independently of urgency and class credit.
Neither an interactive label, a deadline, nor an aged request can authorize
growth above the hard logical-token capacity. When every ready item is blocked
at the hard limit and no backend ticket or clear is outstanding, the reactor's
documented resource-stall policy retires one deterministic victim and waits for
the matching clear acknowledgement before making another such decision.

## FIFO control

FIFO walks eligible candidates in arrival order and applies the same exact
chunk, epoch-token, sequence, backpressure, and hard-resource bounds. It is the
same-runtime control for scheduler ablations. The external `llama-server`
comparison changes more than scheduling and is reported only as a system
reference.

The three-seed mixed-load Phase-3 comparison did not show SA-WDS improving
declared SLO goodput over FIFO at the tested 8 RPS offer rate. FIFO therefore
remains the default; SA-WDS is available for workloads whose class-level token
service policy is desired and must be evaluated against that workload's SLOs.
This result is specific to the TinyLlama/RTX 4060 test setup and is not a
universal ranking.

## Metrics and limits

Runtime snapshots expose per-class service tokens, admission/rejection,
resource defer/unblock, maximum ready wait, starvation events, selected and
eligible requests, batch token/sequence counts, prefill/decode tokens, epoch
timing, and pressure state. Ready wait is measured from the time work becomes
ready to issue; TTFT remains a separate request-level measure. SA-WDS makes no
formal real-time or equal-GPU-time fairness guarantee. A request that is
permanently stream-backpressured or cannot satisfy resource growth is not
continuously eligible for a scheduling fairness claim.

`/healthz` reports process/runtime liveness. `/ready` reports admission
readiness: it becomes unavailable when the sequence limit is full or logical
KV pressure reaches the soft-admission watermark, and becomes ready again once
capacity returns to the normal state. This is based on real admission
constraints rather than transient queue depth. `/metrics` distinguishes
invalid requests, KV-capacity rejects, sequence-capacity rejects, runtime
state rejects, and event-queue-full outcomes by traffic class. The current
HTTP rejection body remains generic (`runtime admission rejected or timed
out`) for reactor-level rejects; exact cause is observable in the aggregate
metrics. A future API revision may add a request-scoped rejection code, but
that refinement is not used to interpret the Phase-3 benchmark because its
per-class cause counters are retained alongside each run.
