from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import run_release_campaign


class ReleaseCampaignTests(unittest.TestCase):
    def test_resolved_configuration_comes_from_backend_and_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            server_log = Path(temporary) / "server.log"
            server_log.write_text(
                "context_per_sequence_resolved=2048 total_context_resolved=32768\n"
                "parallel_resolved=16 batch_size_resolved=2048 ubatch_size_resolved=1024 "
                "cuda_backend_available=true\n", encoding="utf-8")
            metrics = {
                "kv": {"hard_usable_tokens": 29491},
                "scheduler": {"mode": "sawds"},
                "adaptive_prefill": {"enabled": True, "epoch_budget_tokens": 2048},
            }
            actual = run_release_campaign.parse_resolved_configuration(server_log, metrics)
            self.assertEqual(actual["context_per_sequence"], 2048)
            self.assertEqual(actual["total_context"], 32768)
            self.assertEqual(actual["parallel_sequences"], 16)
            self.assertEqual(actual["ubatch_tokens"], 1024)
            self.assertEqual(actual["hard_logical_tokens"], 29491)
            self.assertEqual(actual["scheduler"], "sawds")
            self.assertTrue(actual["adaptive_prefill_enabled"])

    def test_resolved_configuration_fails_when_cuda_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            server_log = Path(temporary) / "server.log"
            server_log.write_text(
                "context_per_sequence_resolved=2048 total_context_resolved=32768\n"
                "parallel_resolved=16 batch_size_resolved=2048 ubatch_size_resolved=1024 "
                "cuda_backend_available=false\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "CUDA is unavailable"):
                run_release_campaign.parse_resolved_configuration(
                    server_log, {"kv": {"hard_usable_tokens": 29491}})

    def test_power_summary_uses_current_limit_and_rejects_impossible_readings(self) -> None:
        samples = [
            {"power.draw.average": 30.0, "power.draw.instant": 34.0,
             "enforced.power.limit": 100.0, "power.default_limit": 100.0,
             "power.max_limit": 140.0},
            {"power.draw.average": 32.0, "power.draw.instant": 35.0,
             "enforced.power.limit": 100.0, "power.default_limit": 100.0,
             "power.max_limit": 140.0},
        ]
        summary = run_release_campaign.gpu_sample_summary(samples)
        self.assertTrue(summary["gpu_power_telemetry_valid"])
        self.assertEqual(summary["gpu_power_limit_w"], 100.0)
        self.assertEqual(summary["gpu_mean_power_w"], 31.0)

        samples[1]["power.draw.instant"] = 590.0
        summary = run_release_campaign.gpu_sample_summary(samples)
        self.assertTrue(summary["gpu_power_telemetry_valid"])
        self.assertFalse(summary["gpu_instant_power_telemetry_valid"])
        self.assertEqual(summary["gpu_power_impossible_instant_sample_count"], 1)
        self.assertEqual(summary["gpu_mean_power_w"], 31.0)
        self.assertIsNone(summary["gpu_mean_instantaneous_power_w"])


if __name__ == "__main__":
    unittest.main()
