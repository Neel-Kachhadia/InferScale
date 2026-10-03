# Phase-3 scheduler and resource benchmarks

## Frozen SLO contract

The comparative contract is frozen before the final Phase-3 treatment runs on
2026-10-03:

- **Interactive:** TTFT at most 750 ms, scheduler-ready wait p99 at most
  500 ms, and E2E at most 8 seconds for the mixed-SLO manifest.
- **Batch:** a continuously eligible admitted request should receive service
  before the 5-second starvation threshold; this is an operational alarm and
  policy target, not a mathematical deadline guarantee.

An SLO goodput request is a successfully completed request that meets every
SLO declared for its class. SLO goodput is the count of those requests divided
by measured wall-clock seconds. Report it beside offered and successful
requests/s, output tokens/s, admission rejections, cancellations, and other
errors so rejection cannot masquerade as a latency improvement.

## Comparison taxonomy

| Variant | Purpose | Causal interpretation |
|---|---|---|
| A: external `llama-server` | Mature system reference | Not a scheduler control; multiple implementation details differ. |
| B: InferScale FIFO | Same-runtime FIFO policy control | Main scheduler baseline. |
| C1: SA-WDS, batch-sized prefill chunks | Chunking-off ablation where technically safe | Isolates bounded chunking effect under SA-WDS. |
| C2: SA-WDS with bounded chunked prefill | Scheduler plus chunking | Main adaptive-policy treatment with static prefill budget. |
| C3: C2 with generation soft reservation | Resource-aware admission treatment | Compare with the same hard cap and no speculative generation reserve; hard growth checks remain enabled in both variants. |
| C4: C3 plus adaptive prefill feedback | Controller ablation | Compare against identical static C3 configuration. |
| E: best verified Phase-3 configuration plus one measured optimization | Optimization-only ablation | Change only the retained optimization where practical. |

Disabling chunking means using a batch-sized scheduler chunk while keeping
model, backend, `n_ubatch`, HTTP path, hard cap, and work trace fixed; the
llama.cpp backend may still internally microbatch that submitted work. If the
final backend/runtime path cannot support this safely, omit C1 and state why.
The resource admission comparison never disables hard growth authorization.
Its control uses a zero speculative generation reserve; its treatment uses
the predeclared bounded reserve. Both enforce the same hard logical capacity.

## Workloads and reporting

The current compact workload set covers interactive-short, long-context,
mixed-SLO, burst, cancellation, and slow-reader behavior. The mixed-SLO trace
labels requests as interactive or batch and carries TTFT/E2E SLO metadata.
Open-loop intended arrivals remain precomputed and bounded; sender lateness
and recorder drops determine run validity. Exact prompt/output usage is
reported only when the endpoint provides it. SSE content-piece interarrival
is not exact token ITL and is labeled separately.

Report run IDs, seed, offered rate, model checksum, exact backend commit,
resolved context/batch/ubatch, GPU/driver/CUDA, scheduler/controller/resource
settings, TTFT/E2E and piece-interarrival percentiles by class, success/error
counts, SLO goodput, service-token share, maximum/p99 wait where measured,
starvation events, admission/resource deferrals, and send-lateness validity.
Prompt-1 one-seed saturation results remain practical baseline observations,
not final statistical release evidence.

Same-runtime B-to-C comparisons support scheduler attribution. A-to-C is
reported only as a system-level reference. Each treatment must use paired
arrival traces and unchanged model, backend, sampling, HTTP path, host, and
resource limits except for its declared ablation variable. Prompt 3 owns the
release-grade repeated-trial confidence intervals and final headline plots.

## Three-seed mixed-load comparison

The compact development-grade causal matrix used 80 open-loop Poisson arrivals
at 8 offered requests/s, concurrency 32, seeds 31, 32, and 33, greedy sampling,
TinyLlama Q4_K_M, the same CUDA backend and HTTP/SSE path, 2048 context tokens
per sequence, 16 sequences, 2048 batch/ubatch tokens, and a hard cap of 29,491
logical tokens. Every run kept the 10% aggregate logical-context margin. All
15 load-generator runs were valid; each had 80 records and zero recorder
drops. Errors below are explicit HTTP admission rejections, not load-generator
or stream failures. Output-token throughput counts exact completed-response
usage tokens divided by measured run wall time; rejected requests remain in
the offered sample and are not counted as successful output.

The table reports arithmetic means of per-seed runs. Percentiles are the mean
of each seed's per-run p99, not a pooled p99. `max batch wait` is the largest
ready wait observed across the three runs. `rejects` is total over all 240
offered requests. Class SLO goodput and overall SLO goodput both include
rejections in the offered denominator and count only completed requests that
meet their declared TTFT/E2E conditions. Interactive scheduler-wait p99 was
below 500 ms in all three runs for every treatment. No run crossed the 5 s
batch starvation alarm or reported a starvation event or hard-limit abort.

| Variant | Completed / 80 mean | Rejects / 240 | Requests/s | SLO goodput/s | Output tokens/s | Overall TTFT p99 ms | Overall E2E p99 ms | Interactive TTFT p99 ms | Interactive / batch ready-wait p99 ms | Max batch wait ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| B FIFO, 512-token chunks, static | 71.7 | 25 | 6.47 | 6.48 | 258.23 | 632 | 3,547 | 471 | 74 / 18 | 152 |
| C1 SA-WDS, batch-sized chunks | 64.3 | 47 | 5.36 | 5.36 | 217.51 | 3,992 | 8,947 | 398 | 52 / 1,023 | 2,407 |
| C2 SA-WDS, 512-token chunks, static | 64.0 | 48 | 5.25 | 5.24 | 215.01 | 3,757 | 9,275 | 421 | 52 / 1,706 | 2,652 |
| C3 C2 plus 64-token generation reserve | 65.7 | 43 | 5.36 | 5.35 | 218.07 | 3,944 | 9,305 | 442 | 52 / 1,364 | 2,883 |
| C4 C3 plus adaptive controller | 61.0 | 57 | 5.10 | 5.13 | 210.43 | 3,420 | 9,188 | 318 | 42 / 1,706 | 1,999 |

B completed 215/240 requests versus C2's 192/240 and produced higher mean
goodput and output-token throughput. C2's interactive TTFT p99 was about 50 ms
lower than B's, while its overall tails, completion count, and goodput were
worse. The tested scheduler therefore does not replace FIFO as the default.
The measured class service-token share was about 23% interactive / 77% batch
for both B and C2 because the batch class contributes much longer prompt
ranges; the configured 4:1 weights govern bounded service credits, not a
promise that raw token share equals 80:20 for unlike work costs.

C1-to-C2 isolates bounded prefill chunking under SA-WDS: the mean goodput
change was -0.12 requests/s, with no consistent three-seed tail improvement.
Chunking remains useful to create interleaving points and enforce a bounded
work item, but this measurement does not establish a universal performance
win. C2-to-C3 changed only the 64-token generation reserve: it recovered five
admissions over 240 offers and improved mean goodput by about 0.11 requests/s,
an effect too small and seed-sensitive to claim as a reliable gain. C3-to-C4
reduced the mean interactive TTFT p99 by roughly 124 ms but had lower mean
goodput by about 0.22 requests/s and 14 more rejections; the controller
remains disabled by default. The matrix's greatest logical charge was 20,598
tokens, below the 29,491 hard cap, and the greatest once-per-second sampled
GPU memory was 2,145 MiB of 8,188 MiB. Rejections were due to explicit
sequence/KV admission boundaries; none were hidden or omitted from goodput.

The configured service weights for all treatments were Interactive 1024 and
Batch 256 token credits (4:1), credit cap 4096, 5,000 ms starvation alarm,
1,000 ms age buckets capped at 60, 100 ms deadline-slack buckets capped at
600, 25% decode share, 512-token scheduler chunk for B/C2/C3/C4, and 2,048
static prefill tokens per epoch. C4 used a 512–2,048 range, 2,048 initial
budget, 256-token step, three observation windows, and five-window cooldown.
The paired matrix was captured before a later narrow fix that ensures a
starvation-aged prefill can reserve a sequence under a one-token decode-only
plan. No matrix run reached the 5 s threshold (the largest batch ready wait
was 2.883 s), so this branch was not active in the measured treatments; the
current source was rebuilt and its exact behavior is covered by a deterministic
regression test. A final-tree real GPU mixed-load check is recorded separately
from this causal matrix.

### Run IDs

| Variant | Seed 31 | Seed 32 | Seed 33 |
|---|---|---|---|
| B | `b6ffdebf-bd37-4f6d-94b7-1c9e6e77d33d` | `f1134972-c5db-4a2f-bbfb-9a17279697e9` | `9c661714-20a2-4ece-aa6a-3fa67c5d08a9` |
| C1 | `477803a1-1e16-4b0a-b2a4-464d7580aaea` | `48f8e6b7-08cf-48a6-85f5-32c7af360976` | `bbebf825-1a75-4593-a606-9320a33f4603` |
| C2 | `91a8c27a-1802-4e1b-8602-029d6f425a00` | `2d5f9fe0-2cb4-4b93-a86b-2b9fceb1d4de` | `1841091e-868c-4f90-8349-17b9fb345239` |
| C3 | `027151cd-c4b3-4bf8-b7f8-183bf8ec6e4f` | `14977139-8761-4abc-a827-3aec09083e8a` | `f89910a6-ca9a-4f31-924b-de56d055314b` |
| C4 | `94ed9887-5e5d-4d7b-93fd-b9fef626bf1c` | `2e7a4f25-ed04-4cda-aca6-c4924d78863d` | `2b4f3bff-e5c3-4904-8f26-a29fa9219500` |

Compact records, server metrics, GPU samples, and raw request JSONL are kept
locally under ignored `build/phase3-evidence/`; Prompt 3 owns archival release
statistics, confidence intervals, and publication plots.

The runtime publishes `/metrics` from an atomic snapshot refreshed every
100 ms. The load generator now polls the final snapshot for up to 250 ms until
per-class admitted-plus-rejected counters cover all client HTTP responses and
completed counters cover all streams that ended with `[DONE]`; it records the
wait duration and a freshness flag. Historical matrix
runs predate this bounded poll. Their request counts and SLO results come from
raw client records, while server completion/resource counters in the table
come from separate after-drain scrapes. One immediate embedded scrape lagged by
two or three completions (C4: 8/11 batch, C3: 9/11 batch); the corresponding
after-drain snapshots and raw records both show 11/11. This snapshot lag did
not change any request outcome or reported table value.
