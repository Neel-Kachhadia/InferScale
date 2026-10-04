# Prompt-3 resource soak

## Result

Gate C resource safety is PASS WITH LIMITATION on the tested host, model, backend, and moderate FIFO workload. The valid run is preserved under artifacts/release-v1/soak/20261004T094228Z and ran the retained server binary identified in [source-binding.json](../artifacts/release-v1/soak/source-binding.json). A first harness attempt at 20261004T091817Z is retained as INVALID_HARNESS_INTERRUPTED because the sampler read the event queue counter from the wrong metrics path; it is not part of acceptance.

The successful run lasted 20.2555 minutes and collected 1,190 one-second samples with 100% runtime telemetry coverage. It ran twenty 60-second open-loop mixed-workload blocks at 2 RPS, with periodic cancellation work and eight-request memory-pressure offers. It offered 2,462 requests, accepted 2,462 at the harness boundary, completed 2,438 successfully, cancelled 24, rejected none, and recorded zero drops. Maximum sender lateness was 2.652 ms. GPU temperature started at 57 C, peaked at 82 C, and ended at 69 C under the unchanged 85 C stop.

## Resource bounds and drain

| Resource | Start | Peak | Post-drain / end | Result |
|---|---:|---:|---:|---|
| Host RSS | 364,628 KiB | 658,448 KiB | 658,448 KiB | RSS increased 293,820 KiB from the first sample to the end. The final-third median was 641,684 KiB, 46,728 KiB above the warm-up-third median (about 3.4 MiB/min over those median windows), within the predeclared 65,536 KiB allowance. Its final five-minute slope was about 4.8 MiB/min. RSS rose in steps and no plateau or absence of slow host growth is established. |
| GPU memory used | 2,021 MiB | 2,313 MiB | 2,313 MiB | Final-third median 2,313 MiB was within the predeclared 10% warm-up allowance; samples remained at 2,313 MiB for roughly the final 10 minutes. |
| KV charged tokens | 0 | 15,269 | 0 | Drained |
| KV committed tokens | 0 | 15,090 | 0 | Drained |
| KV soft reservation | 0 | 11,154 | 0 | Drained |
| KV authorized growth | 0 | 2,048 | 0 | Drained |
| Live requests | 0 | 8 | 0 | Drained after every block |
| Live backend sequences | 0 | 8 | 0 | Drained; all 16 slots available |
| Queued requests / in-flight tickets | 0 / 0 | 8 / 8 | 0 / 0 | Drained |
| Stream buffered / reserved bytes | 0 / 0 | See compact timeline CSV | 0 / 0 | Drained after every block |
| Event queue depth / rejected events | 0 / 0 | 1 / 0 | 0 / 0 | No event queue loss |

There were zero hard-capacity violations, resource invariant violations, avoidable OOM markers, or lease/request/sequence leaks. The maximum charged value remained below the 29,491-token hard bound. Runtime pressure remained normal during this workload; the separate admission and pressure boundary tests establish soft/high-pressure and hard-denial behavior. This soak does not claim that the workload itself forced a pressure transition.

The compact 1,190-row time series is artifacts/release-v1/resource-soak-timeline.csv. The summary, compact series, and figure are derived from the retained raw JSONL samples. File sizes and SHA-256 values for the raw soak logs, requests, metrics, manifest, and samples are listed in artifacts/release-v1/resource-soak-index.json. The time-series figure is artifacts/release-v1/benchmark/figures/resource-soak-timeline.svg.

The result supports hard logical-capacity enforcement, zero lease/counter drift, and clean service drain for this FIFO workload. The observed RSS increase passed the frozen 20-minute drift allowance, but remained upward trending; the soak does not rule out slow host-RSS growth over longer service periods. This run covered FIFO only, and runtime pressure stayed normal, so it does not show endurance for SA-WDS/adaptive or force a pressure transition. It is not evidence of physical KV paging, arbitrary-load safety, or a scheduler performance effect. ASan/UBSan/LSan status is reported in artifacts/release-v1/sanitizer-summary.md; the supported host-capable LSan suite passed with leak detection enabled.

The soak ran with the retained server executable under `/tmp/inferscale-release-7f27cf33b6c4.6lIYLx`. Its SHA-256 was recovered from that executable after the run and recorded in [source-binding.json](../artifacts/release-v1/soak/source-binding.json). The path suffix and the release builder at commit `7f27cf33b6c452015f922d8a8a3d8deb8e0a7f79` identify the source revision. The raw soak manifest did not record a dirty-tree flag, so source cleanliness at build time is not independently attested. The current release source adds full-request hard-KV admission, fatal-state issue prevention, and zero-hint generation retention after that soak. These changes have targeted regression and current-source sanitizer coverage. A bounded eight-request CUDA smoke later passed on runtime source `850db0c` (8/8, 62 C start, 68 C peak), but this is not a sustained soak; a later `nvidia-smi` query could not communicate with the driver. Gate C is PASS WITH LIMITATION: the retained soak applies directly to the recorded `7f27cf33` binary, while the later changes have tests, sanitizers, and a short smoke but no new sustained GPU workload evidence.
