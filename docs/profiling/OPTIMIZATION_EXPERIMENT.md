# Phase-4 optimization experiment

## Experiment note frozen from the representative trace

- **Observed bottleneck:** llama.cpp CUDA model execution and synchronization;
  project-owned scheduler and batch planning account for very little time.
- **Profile/run ID:** C4 seed 32, `616a0f33-0473-467a-a9e8-55372b03380e`;
  manifest and trace details are in [PROFILE_PHASE4.md](PROFILE_PHASE4.md).
- **Hypothesis:** reusing `LlamaBatchBuilder` planning storage or avoiding its
  per-plan allocation could reduce batch-build time.
- **Measured bound:** batch-plan construction consumed 4.284 ms over 1000
  backend batches, versus 11.878 s in the corresponding backend execution
  ranges. Even eliminating the entire measured region could save at most
  0.036% of that backend-range time, before accounting for implementation
  overhead or profiling variance.
- **Expected metric:** at most a sub-0.04% change in backend-range time; no
  credible end-to-end gain is expected at this workload scale.
- **Correctness risk:** persistent scratch storage would add state and lifetime
  coupling to plan construction and can invalidate ownership assumptions if
  buffers escape the engine call.
- **Rollback condition:** any chunk-index, logits-index, batch ownership,
  cancellation, or sequence-lifetime regression would reject the candidate.

## Outcome and ablation

The measured upper bound is below a useful end-to-end signal. The candidate was
rejected before adding persistent buffers or changing the batch path. No
optimization patch was retained, no CUDA kernel was added, and no speedup is
claimed. The bounded experiment was to profile the proposed hot path in a
representative mixed workload and calculate its maximum possible contribution
to backend-range time. This avoids adding allocator/lifetime complexity for a
region that is already negligible.

Because no source optimization was retained, there is no meaningful
optimization-only before/after treatment. The Phase-3 implementation remains
the control and the retained-optimization treatment is intentionally absent;
the required E comparison is therefore **PASS WITH LIMITATION** rather than a
claim of improvement. Final-tree C3 and profiled C4 runs are reported
separately in the profile note and are not treated as a causal optimization
pair. A future profile that shows materially higher builder cost can reopen
the experiment with a single-variable ablation.
