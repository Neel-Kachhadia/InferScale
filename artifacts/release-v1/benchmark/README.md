# Local raw benchmark evidence

Raw run directories are intentionally kept local and ignored by Git. Their
campaign manifests, run indexes, failure markers, load-generator summaries,
request records, GPU samples, and log checksums are indexed in
[`raw-index.json`](raw-index.json). The 16 RPS headline and 8 RPS mixed
campaigns reached the predeclared 85 C stop before a trial completed; they are
invalid and excluded. The eight-request 4 RPS smokes validate the runner and
clean reproduction only.

`trial-summary.csv` contains only explicitly labeled harness smokes and invalid
thermal attempts; smoke throughput cells are blank. It is not an inferential
comparison. There are no Prompt-3 confidence intervals; see
[`figures/NOT_RUN.md`](figures/NOT_RUN.md).
