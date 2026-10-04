from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
import release_analysis


REQUESTED = {
    "context_per_sequence": 2048,
    "parallel_sequences": 16,
    "batch_tokens": 2048,
    "ubatch_tokens": 2048,
    "hard_logical_tokens": 29491,
    "chunk_tokens": 512,
    "generation_reserve_tokens": 64,
}


def trial(trial_number: int, treatment: str, ttft: float, *,
          observations: int = 1500, rejections: int = 0,
          purpose: str = "release_comparison") -> dict:
    return {
        "run_id": f"{treatment}-{trial_number}",
        "workload": "phase5_interactive",
        "trial": trial_number,
        "seed": 800 + trial_number,
        "treatment": treatment,
        "campaign_id": f"campaign-{treatment}",
        "campaign_purpose": purpose,
        "offered_rate_rps": 16.0,
        "requests_offered": observations,
        "requests_succeeded": observations - rejections,
        "interactive_ttft_observations": observations - rejections,
        "requests_per_s": 4.0,
        "output_tokens_per_s": 96.0,
        "goodput_requests_per_s": 3.5,
        "interactive_ttft_p99_ms": ttft,
        "interactive_e2e_p99_ms": 800.0,
        "interactive_ready_wait_p99_upper_bound_ms": 31.0,
        "interactive_service_share": 0.5,
        "interactive_rejections": rejections,
        "batch_rejections": 0,
        "rejected_requests": rejections,
        "rejection_fraction": rejections / observations,
        "completed_success_fraction": (observations - rejections) / observations,
        "max_send_lateness_ms": 1.0,
        "valid": True,
        "comparison_eligible": True,
    }


def paired_trials(observations: int = 1500) -> list[dict]:
    rows = []
    for index in range(1, 8):
        rows.extend([
            trial(index, "fifo", 200.0, observations=observations),
            trial(index, "sawds_static", 190.0, observations=observations, rejections=10),
            trial(index, "sawds_adaptive", 180.0, observations=observations, rejections=20),
            trial(index, "sawds_static_chunk2048", 185.0, observations=observations),
        ])
    return rows


class ReleaseAnalysisTests(unittest.TestCase):
    def test_evidence_paths_inside_repository_are_portable(self) -> None:
        self.assertEqual(release_analysis.evidence_path(
            release_analysis.REPO_ROOT / "README.md"), "README.md")
        self.assertIsNone(release_analysis.evidence_path(None))

    def test_indexed_thermal_abort_without_summary_stays_invalid_and_auditable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            campaign = Path(temporary) / "campaign"
            campaign.mkdir()
            manifest = {
                "campaign_id": "thermal-campaign",
                "campaign_purpose": "release_comparison",
                "load": {"offered_rates_rps": [16.0], "trial_count": 1},
                "policies": {"fifo": ["--scheduler", "fifo"]},
                "requested_configuration": REQUESTED,
                "validity": {"max_start_temperature_c": 65},
            }
            (campaign / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            row = {
                "trial": 1, "treatment": "fifo", "seed": 817,
                "offered_rate_rps": 16.0, "requested": 700,
                "summary": None, "records": None,
                "gpu_samples": "trial.gpu.jsonl",
                "gpu_start_temperature_c": 60, "gpu_max_temperature_c": 85,
                "gpu_thermal_slowdown_samples": 0, "thermal_valid": False,
                "valid": False, "invalid_classification": "INVALID_THERMAL",
                "failure_reason": "GPU reached thermal stop",
            }
            index = campaign / "run-index.jsonl"
            index.write_text(json.dumps(row) + "\n", encoding="utf-8")
            trials, _ = release_analysis.load_trials([index])
            self.assertEqual(len(trials), 1)
            self.assertFalse(trials[0]["summary_valid"])
            self.assertFalse(trials[0]["valid"])
            self.assertFalse(trials[0]["comparison_eligible"])
            self.assertEqual(trials[0]["run_row"]["invalid_classification"],
                             "INVALID_THERMAL")

    def test_missing_absolute_summary_uses_local_run_id_then_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            campaign = Path(temporary) / "campaign"
            run_dir = campaign / "runs" / "portable-run"
            run_dir.mkdir(parents=True)
            (campaign / "manifest.json").write_text(json.dumps({
                "campaign_id": "portable-campaign",
                "campaign_purpose": "reproduction_smoke",
                "workload": {"path": "workloads/smoke.yaml"},
            }), encoding="utf-8")
            summary_path = run_dir / "summary.json"
            summary_path.write_text(json.dumps({
                "run_id": "portable-run", "workload": "smoke", "valid": True,
                "requested_count": 8, "successful_requests": 8,
                "by_class": {"interactive": {"successful_requests": 8}},
            }), encoding="utf-8")
            row = {
                "run_id": "portable-run", "trial": 1, "seed": 10,
                "treatment": "fifo", "offered_rate_rps": 4.0,
                "summary": "/old/checkout/artifacts/runs/portable-run/summary.json",
                "valid": True, "thermal_valid": True,
                "gpu_start_temperature_c": 50,
                "gpu_thermal_slowdown_samples": 0,
            }
            index = campaign / "run-index.jsonl"
            index.write_text(json.dumps(row) + "\n", encoding="utf-8")

            trials, _ = release_analysis.load_trials([index])
            self.assertTrue(trials[0]["valid"])
            self.assertIsNone(trials[0]["summary_path_error"])
            self.assertEqual(trials[0]["summary_path"], str(summary_path))
            # The recorded path is still absent, so resolving it to the local
            # run-id convention rather than the original path is intentional.
            self.assertEqual(trials[0]["run_id"], "portable-run")

            row["run_id"] = "missing-run"
            index.write_text(json.dumps(row) + "\n", encoding="utf-8")
            missing, _ = release_analysis.load_trials([index])
            self.assertFalse(missing[0]["valid"])
            self.assertFalse(missing[0]["index_valid"])
            self.assertIn("indexed summary artifact not found",
                          missing[0]["summary_path_error"])

    def test_summaryless_thermal_aborts_stay_in_their_planned_pair(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            campaign = Path(temporary) / "campaign"
            campaign.mkdir()
            manifest = {
                "campaign_id": "full-pair-campaign",
                "campaign_purpose": "release_comparison",
                "workload": {"path": "/evidence/workloads/phase5_interactive.yaml"},
                "load": {"offered_rates_rps": [16.0], "trial_count": 1},
                "policies": {"fifo": [], "sawds_static": [], "sawds_adaptive": []},
                "requested_configuration": REQUESTED,
                "validity": {"max_start_temperature_c": 65,
                             "max_paired_start_temperature_delta_c": 5},
            }
            (campaign / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            summary_path = campaign / "fifo-summary.json"
            summary_path.write_text(json.dumps({
                "run_id": "fifo", "workload": "phase5_interactive",
                "offered_rate_rps": 16.0, "requested_count": 100,
                "successful_requests": 100, "valid": True,
                "by_class": {"interactive": {"successful_requests": 100}},
            }), encoding="utf-8")
            base_resolved = {
                "context_per_sequence": 2048, "total_context": 32768,
                "parallel_sequences": 16, "batch_tokens": 2048,
                "ubatch_tokens": 2048, "hard_logical_tokens": 29491,
                "chunk_tokens": 512, "generation_reserve_tokens": 64,
                "scheduler": "fifo", "adaptive_prefill_enabled": False,
            }
            rows = []
            for treatment, start, is_valid, summary, scheduler, adaptive in (
                    ("fifo", 50, True, str(summary_path), "fifo", False),
                    ("sawds_static", 85, False, None, "sawds", False),
                    ("sawds_adaptive", 85, False, None, "sawds", True)):
                resolved = dict(base_resolved, scheduler=scheduler,
                                adaptive_prefill_enabled=adaptive)
                rows.append({
                    "trial": 1, "seed": 51020, "treatment": treatment,
                    "offered_rate_rps": 16.0, "summary": summary,
                    "valid": is_valid, "thermal_valid": is_valid,
                    "invalid_classification": None if is_valid else "INVALID_THERMAL",
                    "gpu_start_temperature_c": start,
                    "gpu_thermal_slowdown_samples": 0,
                    "resolved_configuration": resolved,
                })
            index = campaign / "run-index.jsonl"
            index.write_text("".join(json.dumps(row) + "\n" for row in rows),
                             encoding="utf-8")

            trials, _ = release_analysis.load_trials([index])
            self.assertEqual(len(trials), 3)
            self.assertTrue(all(row["workload"] == "phase5_interactive" for row in trials))
            self.assertTrue(all(row["offered_rate_rps"] == 16.0 for row in trials))
            self.assertTrue(all(row["comparison_eligible"] is False for row in trials))
            self.assertTrue(all("run-invalid: sawds_adaptive, sawds_static" in
                                row["comparison_exclusion"] for row in trials))
            self.assertTrue(all("spread 35 C exceeds 5 C" in
                                row["comparison_exclusion"] for row in trials))
            aborted = [row for row in trials if not row["index_valid"]]
            self.assertEqual(len(aborted), 2)
            self.assertTrue(all(row["run_row"]["invalid_classification"] ==
                                "INVALID_THERMAL" for row in aborted))

            with tempfile.TemporaryDirectory() as output_dir:
                summary, intervals = release_analysis.summary_and_intervals(
                    trials, Path(output_dir))
            self.assertEqual(intervals, [])
            self.assertTrue(all(not row["policy_comparison_eligible"] for row in summary))

    def test_headline_ttft_population_counts_observed_successes_not_offers(self) -> None:
        summary = {
            "requested_count": 10000,
            "successful_requests": 9200,
            "by_class": {
                "interactive": {
                    "sample_count": 10000,
                    "successful_requests": 9200,
                    "ttft_observations": 8995,
                }
            },
        }
        row = {"trial": 1, "treatment": "fifo", "valid": True,
               "run_id": "indexed-run", "thermal_valid": True,
               "gpu_thermal_slowdown_samples": 0,
               "gpu_power_limit_w": 100, "gpu_default_power_limit_w": 100}
        result = release_analysis.metric_fields(summary, row)
        self.assertEqual(result["interactive_ttft_observations"], 8995)
        self.assertEqual(result["interactive_successful"], 9200)
        self.assertEqual(result["requests_offered"], 10000)
        self.assertEqual(result["run_id"], "indexed-run")
        self.assertEqual(result["gpu_power_limit_w"], 100)

    def test_bootstrap_resamples_trial_units_deterministically(self) -> None:
        values = [-3.0, -1.0, 0.0, 2.0, 4.0, 6.0, 7.0]
        first = release_analysis.bootstrap_median_interval(values, seed=17, repetitions=250)
        second = release_analysis.bootstrap_median_interval(values, seed=17, repetitions=250)
        self.assertEqual(first, second)
        self.assertLessEqual(first[0], 2.0)
        self.assertGreaterEqual(first[1], 2.0)
        self.assertEqual(release_analysis.percentile(values, 0.5), 2.0)

    def test_analysis_preserves_rejections_and_gates_headline_p99(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rows, intervals = release_analysis.summary_and_intervals(
                paired_trials(), root / "analysis")
            effect = next(row for row in intervals
                          if row["comparison"] == "sawds_static - fifo" and
                          row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(effect["independent_paired_trials"], 7)
            self.assertEqual(effect["median_paired_effect"], -10.0)
            self.assertEqual(effect["status"], "estimated")
            self.assertEqual(effect["baseline_interactive_ttft_observations"], 10500)
            self.assertEqual(effect["treatment_interactive_ttft_observations"], 10430)

            reject_effect = next(row for row in intervals
                                 if row["comparison"] == "sawds_static - fifo" and
                                 row["metric"] == "rejected_requests")
            self.assertEqual(reject_effect["median_paired_effect"], 10.0)
            adaptive_static_effect = next(row for row in intervals
                                          if row["comparison"] ==
                                          "sawds_adaptive - sawds_static" and
                                          row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(adaptive_static_effect["median_paired_effect"], -10.0)
            self.assertEqual(adaptive_static_effect["status"], "estimated")
            self.assertEqual(adaptive_static_effect["baseline_interactive_ttft_observations"], 10430)
            self.assertEqual(adaptive_static_effect["treatment_interactive_ttft_observations"], 10360)
            adaptive_reject_effect = next(row for row in intervals
                                          if row["comparison"] ==
                                          "sawds_adaptive - sawds_static" and
                                          row["metric"] == "rejected_requests")
            self.assertEqual(adaptive_reject_effect["median_paired_effect"], 10.0)
            summary = next(row for row in rows if row["treatment"] == "sawds_static")
            self.assertEqual(summary["median_rejected_requests"], 10.0)
            self.assertTrue(summary["policy_comparison_eligible"])

            chunk_effect = next(row for row in intervals
                                if row["comparison"] ==
                                "sawds_static_chunk2048 - sawds_static" and
                                row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(chunk_effect["median_paired_effect"], -5.0)

            small_population = paired_trials(observations=1000)
            _, insufficient = release_analysis.summary_and_intervals(
                small_population, root / "small-analysis")
            small_p99 = next(row for row in insufficient
                             if row["comparison"] == "sawds_static - fifo" and
                             row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(small_p99["status"], "secondary_only_insufficient_headline_n")
            self.assertIsNone(small_p99["bootstrap_95pct_low"])

            five_pairs = paired_trials()[:15]
            _, few_pairs = release_analysis.summary_and_intervals(
                five_pairs, root / "few-pairs")
            five_p99 = next(row for row in few_pairs
                            if row["comparison"] == "sawds_static - fifo" and
                            row["metric"] == "interactive_ttft_p99_ms")
            self.assertEqual(five_p99["status"], "insufficient_ci_pairs")
            self.assertIsNone(five_p99["bootstrap_95pct_low"])

    def test_load_trials_fails_closed_on_treatment_mismatch_and_temperature_spread(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            campaign = root / "campaign"
            campaign.mkdir()
            manifest = {
                "campaign_id": "test-campaign",
                "campaign_purpose": "release_comparison",
                "policies": {"fifo": [], "sawds_static": []},
                "requested_configuration": REQUESTED,
                "validity": {"max_start_temperature_c": 65,
                             "max_paired_start_temperature_delta_c": 5},
            }
            (campaign / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            index_rows = []
            for treatment_name, scheduler, start_temp, chunk in (
                    ("fifo", "fifo", 50, 512),
                    ("sawds_static", "sawds", 60, 512)):
                run_dir = campaign / treatment_name
                run_dir.mkdir()
                summary = {
                    "run_id": treatment_name, "workload": "phase5_interactive",
                    "offered_rate_rps": 16.0, "requested_count": 100,
                    "successful_requests": 100, "requests_per_s": 4.0,
                    "output_tokens_per_s": 96.0, "goodput_requests_per_s": 4.0,
                    "ttft_ms": {"p99": 200}, "e2e_ms": {"p99": 800},
                    "valid": True, "by_class": {"interactive": {
                        "successful_requests": 100, "sample_count": 100,
                        "ttft_ms": {"p99": 200}, "goodput_requests_per_s": 4.0}},
                    "server_metric_deltas": {"interactive": {"rejected": 0,
                        "service_tokens": 1000, "scheduler_wait_p99_upper_bound_ms": 31}},
                }
                summary_path = run_dir / "summary.json"
                summary_path.write_text(json.dumps(summary), encoding="utf-8")
                resolved = {"context_per_sequence": 2048, "total_context": 32768,
                            "parallel_sequences": 16, "batch_tokens": 2048,
                            "ubatch_tokens": 2048, "hard_logical_tokens": 29491,
                            "chunk_tokens": chunk, "generation_reserve_tokens": 64,
                            "scheduler": scheduler, "adaptive_prefill_enabled": False}
                index_rows.append({"trial": 1, "seed": 817, "treatment": treatment_name,
                                   "offered_rate_rps": 16.0, "summary": str(summary_path),
                                   "valid": True, "thermal_valid": True,
                                   "gpu_start_temperature_c": start_temp,
                                   "gpu_thermal_slowdown_samples": 0,
                                   "resolved_configuration": resolved})
            index = campaign / "run-index.jsonl"
            index.write_text("".join(json.dumps(row) + "\n" for row in index_rows),
                             encoding="utf-8")
            trials, _ = release_analysis.load_trials([index])
            self.assertTrue(all(row["valid"] for row in trials))
            self.assertTrue(all(row["resolved_configuration_valid"] for row in trials))
            self.assertTrue(all(row["paired_environment_valid"] is False for row in trials))
            self.assertTrue(all(row["comparison_eligible"] is False for row in trials))
            self.assertTrue(all(row["paired_start_temperature_delta_c"] == 10 for row in trials))
            self.assertTrue(all("spread" in row["comparison_exclusion"] for row in trials))

            # A valid survivor cannot form a zero-spread comparison by itself.
            index.write_text(json.dumps(index_rows[0]) + "\n", encoding="utf-8")
            singleton, _ = release_analysis.load_trials([index])
            self.assertTrue(singleton[0]["valid"])
            self.assertFalse(singleton[0]["comparison_eligible"])
            self.assertIsNone(singleton[0]["paired_start_temperature_delta_c"])
            self.assertIn("missing sawds_static", singleton[0]["comparison_exclusion"])

            # A complete valid set with matching starts is eligible.
            index_rows[1]["gpu_start_temperature_c"] = 52
            index.write_text("".join(json.dumps(row) + "\n" for row in index_rows),
                             encoding="utf-8")
            paired, _ = release_analysis.load_trials([index])
            self.assertTrue(all(row["comparison_eligible"] for row in paired))
            self.assertTrue(all(row["paired_start_temperature_delta_c"] == 2
                                for row in paired))

            index_rows[1]["gpu_start_temperature_c"] = 52
            index_rows[1]["resolved_configuration"]["chunk_tokens"] = 2048
            index.write_text("".join(json.dumps(row) + "\n" for row in index_rows),
                             encoding="utf-8")
            trials, _ = release_analysis.load_trials([index])
            static = next(row for row in trials if row["source_treatment"] == "sawds_static")
            self.assertFalse(static["valid"])
            self.assertFalse(static["resolved_configuration_valid"])
            self.assertIn("chunk_tokens", static["resolved_configuration_error"])

    def test_smoke_is_not_policy_comparison_and_output_cannot_overwrite_curated_data(self) -> None:
        smoke = trial(1, "fifo", 250.0, observations=8, purpose="reproduction_smoke")
        smoke["comparison_eligible"] = False
        with tempfile.TemporaryDirectory() as temporary:
            summary, intervals = release_analysis.summary_and_intervals([smoke], Path(temporary))
            self.assertEqual(len(summary), 1)
            self.assertFalse(summary[0]["policy_comparison_eligible"])
            self.assertEqual(intervals, [])
            interval_file = Path(temporary) / "confidence-intervals.csv"
            self.assertTrue(interval_file.read_text(encoding="utf-8").startswith(
                "workload,offered_rate_rps,comparison,metric,independent_paired_trials"))
        with self.assertRaisesRegex(ValueError, "cannot overwrite curated"):
            release_analysis.validate_output_path(
                release_analysis.REPO_ROOT / "artifacts/release-v1/benchmark")

    def test_bootstrap_requires_seven_paired_trials_for_a_95_percent_interval(self) -> None:
        rows = paired_trials()[:15]
        with tempfile.TemporaryDirectory() as temporary:
            _, intervals = release_analysis.summary_and_intervals(rows, Path(temporary))
        effect = next(row for row in intervals
                      if row["comparison"] == "sawds_static - fifo" and
                      row["metric"] == "goodput_requests_per_s")
        self.assertEqual(effect["status"], "insufficient_ci_pairs")
        self.assertIsNone(effect["bootstrap_95pct_low"])
        self.assertEqual(effect["minimum_pairs_for_95pct_interval"], 7)


if __name__ == "__main__":
    unittest.main()
