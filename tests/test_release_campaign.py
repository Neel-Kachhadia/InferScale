from __future__ import annotations

import argparse
import json
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import run_release_campaign


class ReleaseCampaignTests(unittest.TestCase):
    def test_abort_cleanup_kills_a_load_generator_that_ignores_sigterm(self) -> None:
        process = subprocess.Popen(
            [sys.executable, "-c",
             "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); "
             "print('ready', flush=True); time.sleep(30)"],
            stdout=subprocess.PIPE, text=True)
        self.addCleanup(run_release_campaign.terminate_process, process, grace_s=0.1)
        assert process.stdout is not None
        self.assertEqual(process.stdout.readline().strip(), "ready")
        process.stdout.close()
        run_release_campaign.terminate_process(process, grace_s=0.1)
        self.assertIsNotNone(process.poll())
        self.assertEqual(process.returncode, -signal.SIGKILL)

    def test_thermal_abort_produces_a_complete_invalid_index_row(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            campaign = Path(temporary)
            run_dir = campaign / "runs" / "partial-run"
            run_dir.mkdir(parents=True)
            label = "trial-01-r16-fifo"
            (campaign / f"{label}.context.json").write_text(json.dumps({
                "started_at_utc": "2026-10-04T00:00:00+00:00",
                "gpu_start_temperature_c": 60,
                "server_command": ["server", "--port", "1234"],
                "loadgen_command": ["python", "run.py"],
                "server_log": "server.log",
                "server_stopped_cleanly": True,
            }), encoding="utf-8")
            (campaign / f"{label}.gpu.jsonl").write_text("\n".join([
                json.dumps({"temperature.gpu": 60,
                            "clocks_throttle_reasons.sw_thermal_slowdown": "Not Active",
                            "clocks_throttle_reasons.hw_thermal_slowdown": "Not Active"}),
                json.dumps({"temperature.gpu": 85,
                            "clocks_throttle_reasons.sw_thermal_slowdown": "Active",
                            "clocks_throttle_reasons.hw_thermal_slowdown": "Not Active"}),
            ]) + "\n", encoding="utf-8")
            (run_dir / "requests.jsonl").write_text(json.dumps({
                "run_id": "partial-id", "send_ns": 300, "intended_arrival_ns": 100,
                "status": "ok",
            }) + "\n", encoding="utf-8")
            args = argparse.Namespace(max_gpu_temperature_c=85, requests=700)
            row = run_release_campaign.invalid_run_record(
                args, 1, "fifo", 817, 16.0, campaign,
                RuntimeError("GPU reached a thermal validity limit during the trial"),
                set(), ["fifo", "sawds_static", "sawds_adaptive"])
            self.assertFalse(row["valid"])
            self.assertFalse(row["thermal_valid"])
            self.assertEqual(row["invalid_classification"], "INVALID_THERMAL")
            self.assertEqual(row["run_id"], "partial-id")
            self.assertEqual(row["partial_record_count"], 1)
            self.assertEqual(row["max_send_lateness_ms"], 0.0002)
            self.assertIsNone(row["summary"])

    def test_resolved_configuration_comes_from_backend_and_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            server_log = Path(temporary) / "server.log"
            server_log.write_text(
                "context_per_sequence_resolved=2048 total_context_resolved=32768\n"
                "parallel_resolved=16 batch_size_resolved=2048 ubatch_size_resolved=1024 "
                "cuda_backend_available=true scheduler=sawds adaptive_prefill=true "
                "generation_reserve_tokens=64 prefill_epoch_tokens=2048 "
                "scheduler_prefill_chunk_tokens=512\n", encoding="utf-8")
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
            self.assertEqual(actual["generation_reserve_tokens"], 64)
            self.assertEqual(actual["prefill_epoch_tokens"], 2048)
            self.assertEqual(actual["chunk_tokens"], 512)

            expected = {"context_per_sequence": 2048, "total_context": 32768,
                        "parallel_sequences": 16, "batch_tokens": 2048,
                        "ubatch_tokens": 1024, "hard_logical_tokens": 29491,
                        "chunk_tokens": 512, "generation_reserve_tokens": 64,
                        "prefill_epoch_tokens": 2048}
            run_release_campaign.validate_resolved_configuration(actual, "sawds_adaptive", expected)
            with self.assertRaisesRegex(RuntimeError, "scheduler"):
                run_release_campaign.validate_resolved_configuration(
                    {**actual, "scheduler": "fifo"}, "sawds_adaptive", expected)
            with self.assertRaisesRegex(RuntimeError, "ubatch_tokens"):
                run_release_campaign.validate_resolved_configuration(
                    {**actual, "ubatch_tokens": 2048}, "sawds_adaptive", expected)

    def test_resolved_configuration_fails_when_cuda_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            server_log = Path(temporary) / "server.log"
            server_log.write_text(
                "context_per_sequence_resolved=2048 total_context_resolved=32768\n"
                "parallel_resolved=16 batch_size_resolved=2048 ubatch_size_resolved=1024 "
                "cuda_backend_available=false scheduler=fifo adaptive_prefill=false "
                "generation_reserve_tokens=64 prefill_epoch_tokens=2048 "
                "scheduler_prefill_chunk_tokens=512\n", encoding="utf-8")
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
        self.assertEqual(summary["gpu_thermal_slowdown_samples"], 0)
        self.assertEqual(summary["gpu_power_limit_w"], 100.0)
        self.assertEqual(summary["gpu_mean_power_w"], 31.0)

        samples[1]["power.draw.instant"] = 590.0
        summary = run_release_campaign.gpu_sample_summary(samples)
        self.assertTrue(summary["gpu_power_telemetry_valid"])
        self.assertFalse(summary["gpu_instant_power_telemetry_valid"])
        self.assertEqual(summary["gpu_power_impossible_instant_sample_count"], 1)
        self.assertEqual(summary["gpu_mean_power_w"], 31.0)
        self.assertIsNone(summary["gpu_mean_instantaneous_power_w"])

        samples[0]["clocks_throttle_reasons.sw_thermal_slowdown"] = "Active"
        summary = run_release_campaign.gpu_sample_summary(samples)
        self.assertEqual(summary["gpu_thermal_slowdown_samples"], 1)


if __name__ == "__main__":
    unittest.main()
