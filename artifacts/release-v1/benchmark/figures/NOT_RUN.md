# Release figures not generated

The final 16 RPS primary, 12 RPS sole fallback, and 8 RPS mixed SLO campaigns produced four analyzer-valid standalone treatment arms, 151 `INVALID_THERMAL` arms, and one `INVALID_CONFIG_MISMATCH` arm, but no comparison-eligible paired FIFO/static/adaptive trial. The analysis output contains no policy-effect estimates or confidence intervals, so these comparative figures were not generated:

- p99 TTFT comparison or CDF
- SLO goodput comparison
- output-throughput comparison
- rejection comparison
- paired treatment-effect and confidence-interval plot
- fairness or starvation comparison
- long-prefill, controller, cancellation, or chunk-ablation comparison

The four analyzer-valid standalone arm values remain in artifacts/release-v1/valid-arm-details.csv and the raw campaign records. They are not plotted as treatment effects. All invalid thermal and configuration-mismatched runs remain indexed and excluded. The missing comparison is optional publication-grade evidence, not a software-release blocker. No release-grade scheduler speedup is claimed.

Two figures use valid, non-comparative evidence: resource-soak-timeline.svg and thermal-validity-summary.svg. The first plots the valid 20.26-minute soak; the second counts valid and invalid treatment arms and shows that no complete paired treatment trial was obtained.
