from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
import release_analysis


class ReleaseAnalysisTests(unittest.TestCase):
    def test_bootstrap_resamples_trial_units_deterministically(self) -> None:
        values = [-3.0, -1.0, 0.0, 2.0, 4.0]
        first = release_analysis.bootstrap_median_interval(values, seed=17, repetitions=250)
        second = release_analysis.bootstrap_median_interval(values, seed=17, repetitions=250)
        self.assertEqual(first, second)
        self.assertLessEqual(first[0], 0.0)
        self.assertGreaterEqual(first[1], 0.0)
        self.assertEqual(release_analysis.percentile(values, 0.5), 0.0)

    def test_paired_effect_uses_trials_and_outputs_regenerable_figures(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            campaign = root / "campaign"
            run_root = campaign / "runs"
            run_root.mkdir(parents=True)
            manifest = {
                "campaign_id": "test-campaign",
                "resolved_configuration": {"chunk_tokens": 512,
                                           "generation_reserve_tokens": 64},
            }
            (campaign / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            rows = []
            for trial in range(1, 6):
                for treatment, change in (("fifo", 0.0), ("sawds_static", -10.0)):
                    run_id = f"{treatment}-{trial}"
                    run_dir = run_root / run_id
                    run_dir.mkdir()
                    summary = {
                        "run_id": run_id, "workload": "phase5_interactive",
                        "offered_rate_rps": 4.0, "requested_count": 100,
                        "successful_requests": 100, "requests_per_s": 4.0,
                        "output_tokens_per_s": 96.0, "goodput_requests_per_s": 4.0,
                        "ttft_ms": {"p99": 200.0}, "e2e_ms": {"p99": 800.0},
                        "valid": True, "record_drops": 0,
                        "server_metrics_sampling_coverage": 1.0,
                        "server_metrics_fresh_for_client_results": True,
                        "server_metric_deltas": {"interactive": {
                            "scheduler_wait_p99_upper_bound_ms": 31,
                            "scheduler_wait_observations": 200,
                            "maximum_ready_wait_ms_after": 30,
                            "starvation_events": 0, "rejected": 0,
                            "resource_deferred": 0, "service_tokens": 1000,
                        }},
                        "by_class": {"interactive": {
                            "successful_requests": 100,
                            "sample_count": 100,
                            "ttft_ms": {"p50": 50, "p90": 100, "p95": 150,
                                        "p99": 200.0 + change},
                            "e2e_ms": {"p99": 800},
                            "piece_interarrival_ms": {"p99": 20},
                            "goodput_requests_per_s": 4.0,
                        }},
                    }
                    summary_path = run_dir / "summary.json"
                    summary_path.write_text(json.dumps(summary), encoding="utf-8")
                    records_path = run_dir / "requests.jsonl"
                    records_path.write_text("\n".join(json.dumps({
                        "sample_id": index, "priority_class": "interactive", "status": "ok",
                        "first_token_ns": 200_000_000 + index * 1_000_000,
                        "intended_arrival_ns": index * 1_000_000,
                    }) for index in range(5)) + "\n", encoding="utf-8")
                    metrics_path = run_dir / "server-metrics.jsonl"
                    metrics_path.write_text(json.dumps({"offset_ms": 0, "kv": {
                        "pressure": "normal", "committed_tokens": 0,
                        "soft_reserved_tokens": 0}}) + "\n", encoding="utf-8")
                    rows.append({"trial": trial, "treatment": treatment, "seed": 800 + trial,
                                 "run_id": run_id, "summary": str(summary_path),
                                 "records": str(records_path), "server_metrics": str(metrics_path),
                                 "gpu_samples": None, "treatment_order": ["fifo", "sawds_static"]})
            index_path = campaign / "run-index.jsonl"
            index_path.write_text("".join(json.dumps(row) + "\n" for row in rows),
                                  encoding="utf-8")
            trials, manifests = release_analysis.load_trials([index_path])
            _, intervals = release_analysis.summary_and_intervals(trials, root / "analysis")
            effect = next(row for row in intervals
                          if row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(effect["independent_paired_trials"], 5)
            self.assertEqual(effect["median_paired_effect"], -10.0)
            release_analysis.generate_figures(trials, manifests, root / "analysis", root)
            self.assertTrue((root / "analysis/figures/p99-ttft-vs-load.svg").is_file())
            self.assertTrue((root / "analysis/figures/ttft-cdf.svg").is_file())


if __name__ == "__main__":
    unittest.main()
