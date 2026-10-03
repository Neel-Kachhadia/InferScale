# ADR-005: Bounded adaptive prefill control

**Status:** Accepted

Adaptive feedback may change only the next epoch's maximum prefill-token
budget. A smoothed interactive latency signal, decode backlog, hysteresis,
bounded step, configured min/max, observation windows, and cooldown make each
decision traceable. The controller never mutates hard capacity, sequence or
context limits, lifecycle state, cancellation, or resource authorization.

Static prefill mode remains independently selectable and is the current
default. In the paired three-seed mixed-load comparison, adaptive mode reduced
TTFT p99 in two seeds but did not improve mean goodput and rejected more
requests overall; it remains an experimental option. This is specific to the
validation profile, and its bounded trace remains available for later workload
evaluation.
