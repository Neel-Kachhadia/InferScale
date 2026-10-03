# ADR-004: SLO-aware weighted deficit scheduling

**Status:** Accepted

InferScale keeps deterministic FIFO as a same-runtime control and adds SA-WDS
for two traffic classes: interactive and batch. Class-level bounded token
credits express relative service weights without allowing request fan-out to
multiply a class's weight. Urgency is discrete and deterministic: starvation
bound, deadline-slack bucket, bounded age bucket, arrival order, and
generation-fenced request key.

FIFO remains the runtime default for the current validation profile. In the
paired three-seed TinyLlama/RTX 4060 mixed-load comparison, SA-WDS did not
improve SLO goodput over FIFO; the scheduler remains an explicit policy for
workloads where its class-level token-service contract is useful. This is a
workload-specific result, not a universal ranking.

The scheduler applies lifecycle, stream, backend, token-budget, and hard
resource eligibility before ordering. It protects a bounded share for decode
while leaving remaining capacity for exact prefill chunks. The work cost is
scheduled token count; this is an explicit service accounting unit, not a
claim of equal GPU time. Hard resource safety cannot be overridden by class,
age, or deadline urgency.
