# Required release figures not generated

These figures require complete, valid, paired release trials. Both sustained
Prompt-3 comparisons reached the 85 C stop in their first treatment and have
zero completed trial-index rows. Generating policy curves from those partial
attempts would misrepresent the evidence.

| Required figure | Status | Reason |
|---|---|---|
| Throughput vs offered load (A/B/C/D where applicable) | NOT RUN | No completed Prompt-3 load-curve treatment set. |
| SLO goodput vs offered load | NOT RUN | No complete paired policy curve. |
| Interactive p99 TTFT vs offered load | NOT RUN | No valid paired p99 trial distribution. |
| Representative near-saturation TTFT CDF | NOT RUN | No complete valid near-knee sample. |
| Long-prefill interference, chunk 512 vs 2048 | NOT RUN | Frozen ablation did not run after thermal stops. |
| Fairness: class wait and service share | NOT RUN | No valid Prompt-3 mixed treatment set. |
| Resource-pressure timeline | NOT RUN | The bounded failure tests are not a comparative pressure campaign. |
| Adaptive controller timeline | NOT RUN | No complete adaptive mixed trial. |
| Cancellation stop/release latency | NOT RUN | No final release cancellation campaign; deterministic correctness tests remain. |
| Profiler summary | NOT REGENERATED | Reuse Prompt-2 profiling evidence in `docs/profiling/`; no runtime hot-path change justified a new trace. |

The small 4 RPS smoke analyzer output is retained with its run directory, but
it is not a release figure and is not used to claim a policy effect.
