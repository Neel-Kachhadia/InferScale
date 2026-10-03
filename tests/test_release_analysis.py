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
                "validity": {"max_start_temperature_c": 65},
            }
            (campaign / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            rows = []
            for trial in range(1, 6):
                for treatment, change in (("fifo", 0.0), ("sawds_static", -10.0),
                                          ("sawds_static_chunk2048", -15.0)):
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
                                 "gpu_samples": None, "treatment_order": ["fifo", "sawds_static"],
                                 "valid": True, "thermal_valid": True,
                                 "gpu_start_temperature_c": 50})
            index_path = campaign / "run-index.jsonl"
            index_path.write_text("".join(json.dumps(row) + "\n" for row in rows),
                                  encoding="utf-8")
            trials, manifests = release_analysis.load_trials([index_path])
            _, intervals = release_analysis.summary_and_intervals(trials, root / "analysis")
            effect = next(row for row in intervals
                          if row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(effect["independent_paired_trials"], 5)
            self.assertEqual(effect["median_paired_effect"], -10.0)
            chunk_effect = next(row for row in intervals
                                if row["comparison"] ==
                                "sawds_static_chunk2048 - sawds_static" and
                                row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(chunk_effect["independent_paired_trials"], 5)
            self.assertEqual(chunk_effect["median_paired_effect"], -5.0)
            release_analysis.generate_figures(trials, manifests, root / "analysis", root)
            self.assertTrue((root / "analysis/figures/p99-ttft-vs-load.svg").is_file())
            self.assertTrue((root / "analysis/figures/ttft-cdf.svg").is_file())

            four_trial_index = campaign / "run-index-four.jsonl"
            four_trial_index.write_text("".join(json.dumps(row) + "\n" for row in rows[:8]),
                                        encoding="utf-8")
            four_trials, _ = release_analysis.load_trials([four_trial_index])
            _, short_intervals = release_analysis.summary_and_intervals(
                four_trials, root / "short-analysis")
            short_effect = next(row for row in short_intervals
                                if row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(short_effect["status"], "insufficient_pairs")
            self.assertIsNone(short_effect["bootstrap_95pct_low"])
            self.assertIsNone(short_effect["bootstrap_95pct_high"])

            (campaign / "campaign-error.txt").write_text("thermal abort\n", encoding="utf-8")
            failed_trials, _ = release_analysis.load_trials([index_path])
            self.assertTrue(all(not row["valid"] for row in failed_trials))
            failed_summary, failed_intervals = release_analysis.summary_and_intervals(
                failed_trials, root / "failed-analysis")
            self.assertEqual(failed_summary, [])
            self.assertEqual(failed_intervals, [])

            empty_campaign = root / "empty-failed-campaign"
            empty_campaign.mkdir()
            (empty_campaign / "manifest.json").write_text(json.dumps({
                "campaign_id": "empty-failed-campaign"}), encoding="utf-8")
            (empty_campaign / "run-index.jsonl").write_text("", encoding="utf-8")
            (empty_campaign / "campaign-error.txt").write_text("failed before first run\n",
                                                                 encoding="utf-8")
            empty_trials, empty_manifests = release_analysis.load_trials(
                [empty_campaign / "run-index.jsonl"])
            self.assertEqual(empty_trials, [])
            self.assertEqual(empty_manifests["empty-failed-campaign"]["campaign_error"],
                             "failed before first run")

            incomplete_campaign = root / "incomplete-campaign"
            incomplete_campaign.mkdir()
            (incomplete_campaign / "manifest.json").write_text(json.dumps({
                "campaign_id": "incomplete-campaign",
                "load": {"trial_count": 1, "offered_rates_rps": [4.0]},
                "policies": {"fifo": ["--scheduler", "fifo"]},
            }), encoding="utf-8")
            (incomplete_campaign / "run-index.jsonl").write_text("", encoding="utf-8")
            incomplete_trials, incomplete_manifests = release_analysis.load_trials(
                [incomplete_campaign / "run-index.jsonl"])
            self.assertEqual(incomplete_trials, [])
            self.assertIn("incomplete or duplicate run index",
                          incomplete_manifests["incomplete-campaign"]["campaign_error"])

    def test_whiskers_use_exact_minimum_and_maximum(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            figure = Path(temporary) / "whisker.svg"
            release_analysis.line_figure(
                figure, "test", "min-to-max", "load", "latency",
                {"policy": [(1.0, 6.0, (1.0, 9.0))]}, ["run-1"])
            svg = figure.read_text(encoding="utf-8")
            # 1 and 9 ms map independently after both bounds expand the y axis.
            self.assertIn('y1="434.0"', svg)
            self.assertIn('y2="105.6"', svg)


if __name__ == "__main__":
    unittest.main()
