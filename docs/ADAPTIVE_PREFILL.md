# Adaptive prefill control

Adaptive mode changes one scheduling parameter: the maximum prefill tokens
allowed in the next scheduling epoch. It cannot change hard capacity, context
size, sequence count, request lifecycle, fairness rules, cancellation, or
resource authorization. Static prefill mode remains available and is the
default unless measurement supports enabling feedback.

## Signals and decision rule

The reactor observes interactive first-output latency, interactive decode
backlog, and executor idle fraction. The TTFT signal uses a 1/4 EWMA update;
the controller defaults use a 750 ms high threshold and 500 ms low threshold.
Executor idle fraction is a bounded proxy from backend batch time and the
interval between completions, not a GPU-utilization measurement.
Trace entries retain the batch ID, signals, batch prefill/decode token counts,
old/new budget, and reason so mixed-batch observations remain interpretable.

Three consecutive high-pressure observations reduce the budget by one 128-token
step. Three low-pressure observations with low decode backlog and sufficient
executor idle headroom increase it by one step. The initial bounds are 128 to
2048 tokens, starting at 1024, with five completed backend-batch observations
of cooldown after an adjustment. Every observation entering cooldown is held,
including the final configured cooldown observation. Every update clamps to configured bounds. The trace is bounded to
128 recent entries. An unavailable TTFT sample is never fabricated: queue age
and decode backlog remain usable signals. Increasing requires a valid executor
idle observation; an absent or invalid idle measurement cannot trigger growth.
When interactive TTFT, ready-queue, and decode-backlog signals are all absent,
the controller returns to the configured static prefill budget (clamped within
adaptive bounds), clears stale hysteresis state, and records
`static_fallback`.

These values are starting defaults, not a universal serving policy. The
predeclared Phase-3 comparison used a 512–2048 token range, 2048 initial
budget, 256-token steps, three observation windows, and five-window cooldown.
They were frozen before comparative treatment analysis. In the three-seed
matrix adaptive mode reduced mean interactive TTFT p99 by about 124 ms but
also reduced mean goodput by about 0.22 requests/s and raised rejections by
14; it remains disabled by default. A final-tree profiled C4 run held its
budget at 2048 throughout the retained trace window, recording hysteresis and
static-fallback reasons without changing the bound. The controller is kept as
an experimental mode because its behavior is bounded and testable, not because
the measurement established a universal benefit.

## Safety and fallback

The controller runs on the reactor after a completed backend batch and can
affect only a later plan. Controller traces are bounded. Static mode resets to
the configured fixed epoch budget. When no interactive control signal is
available, adaptive mode returns to that static budget; missing idle data can
never cause an increase. Hard resource settings remain fixed, and the scheduler
continues under the static hard-capacity and fairness rules. A trace reason
distinguishes static fallback, hysteresis hold, cooldown, pressure reduction,
and idle-capacity increase.

No controller decision is a hard-real-time guarantee. Adaptive behavior must
be compared against static mode using the same workload trace, resource
settings, model, backend, and scheduler.
