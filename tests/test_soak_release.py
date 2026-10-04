from __future__ import annotations

import sys
import unittest
import os
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import soak_release


class SoakReleaseTests(unittest.TestCase):
    def test_missing_block_thermal_flag_fails_closed_without_crashing(self) -> None:
        self.assertFalse(soak_release.blocks_are_valid([{"valid": True}]))

    def test_all_blocks_require_request_and_thermal_validity(self) -> None:
        self.assertTrue(soak_release.blocks_are_valid([
            {"valid": True, "thermal_valid": True},
            {"valid": True, "thermal_valid": True},
        ]))
        self.assertFalse(soak_release.blocks_are_valid([
            {"valid": True, "thermal_valid": True},
            {"valid": True, "thermal_valid": False},
        ]))

    def test_rss_plateau_has_a_frozen_absolute_and_relative_allowance(self) -> None:
        stable, allowance = soak_release.rss_plateau(500_000, 550_000)
        self.assertTrue(stable)
        self.assertEqual(allowance, 64 * 1024)
        self.assertFalse(soak_release.rss_plateau(500_000, 600_000)[0])
        self.assertFalse(soak_release.rss_plateau(None, 100)[0])

    def test_gpu_memory_plateau_uses_first_and_final_thirds(self) -> None:
        stable, warm, allowance = soak_release.gpu_memory_plateau(
            [2000, 2010, 1990, 2010, 2020, 2010, 2020, 2040, 2030])
        self.assertTrue(stable)
        self.assertEqual(warm, 2000.0)
        self.assertEqual(allowance, 200.0)
        self.assertFalse(soak_release.gpu_memory_plateau(
            [2000, 2010, 1990, 2500, 2520, 2510])[0])

    def test_resource_counter_invariants_cover_hard_cap_sequences_and_pressure(self) -> None:
        kv = {"charged_tokens": 10, "committed_tokens": 6,
              "soft_reserved_tokens": 3, "authorized_tokens": 1,
              "hard_usable_tokens": 20, "available_sequences": 15,
              "pressure": "normal"}
        resources = {"live_sequences": 1, "live_requests": 1, "queued_requests": 0,
                     "inflight_tickets": 0, "stream_buffered_bytes": 0,
                     "stream_reserved_bytes": 0, "event_queue_depth": 0,
                     "engine_command_queue_depth": 0,
                     "engine_command_queue_capacity": 128}
        self.assertEqual(soak_release.resource_snapshot_violations(kv, resources, 16, 0), [])
        broken = {**kv, "charged_tokens": 21, "pressure": "unknown"}
        violations = soak_release.resource_snapshot_violations(broken, resources, 16, 0)
        self.assertIn("kv_accounting_drift", violations)
        self.assertIn("hard_capacity_exceeded", violations)
        self.assertIn("invalid_pressure_state", violations)
        sequence_drift = {**kv, "available_sequences": 14}
        self.assertIn("sequence_accounting_drift",
                      soak_release.resource_snapshot_violations(sequence_drift, resources, 16, 0))
        queue_overflow = {**resources, "engine_command_queue_depth": 129}
        self.assertIn("engine_command_queue_capacity_exceeded",
                      soak_release.resource_snapshot_violations(kv, queue_overflow, 16, 0))
        self.assertIn("event_queue_rejections_observed",
                      soak_release.resource_snapshot_violations(kv, resources, 16, 1))

    def test_event_queue_rejections_use_scheduler_metrics_schema(self) -> None:
        runtime = {"scheduler": {"event_queue_rejections": 0}}
        self.assertEqual(soak_release.event_queue_rejections(runtime), 0)
        self.assertIsNone(soak_release.event_queue_rejections(
            {"event_queue_rejections": 0}))
        self.assertIsNone(soak_release.event_queue_rejections(
            {"scheduler": {"event_queue_rejections": True}}))

        kv = {"charged_tokens": 0, "committed_tokens": 0,
              "soft_reserved_tokens": 0, "authorized_tokens": 0,
              "hard_usable_tokens": 20, "available_sequences": 16,
              "pressure": "normal"}
        resources = {"live_sequences": 0, "live_requests": 0, "queued_requests": 0,
                     "inflight_tickets": 0, "stream_buffered_bytes": 0,
                     "stream_reserved_bytes": 0, "event_queue_depth": 0,
                     "engine_command_queue_depth": 0,
                     "engine_command_queue_capacity": 128}
        self.assertEqual(soak_release.resource_snapshot_violations(
            kv, resources, 16, soak_release.event_queue_rejections(runtime)), [])
        self.assertIn("missing_event_queue_rejection_counter",
                      soak_release.resource_snapshot_violations(
                          kv, resources, 16,
                          soak_release.event_queue_rejections({"scheduler": {}})))
        self.assertIn("event_queue_rejections_observed",
                      soak_release.resource_snapshot_violations(
                          kv, resources, 16,
                          soak_release.event_queue_rejections(
                              {"scheduler": {"event_queue_rejections": 1}})))

    def test_drain_requires_leases_sequences_requests_and_queues_to_be_empty(self) -> None:
        drained = {
            "kv": {"charged_tokens": 0, "committed_tokens": 0,
                   "soft_reserved_tokens": 0, "authorized_tokens": 0,
                   "available_sequences": 16},
            "resources": {"live_requests": 0, "live_sequences": 0,
                          "queued_requests": 0, "inflight_tickets": 0,
                          "stream_buffered_bytes": 0, "stream_reserved_bytes": 0,
                          "event_queue_depth": 0, "engine_command_queue_depth": 0,
                          "outstanding_engine_commands": 0},
        }
        self.assertTrue(soak_release.runtime_is_drained(drained, 16))
        drained["resources"]["live_requests"] = 1
        self.assertFalse(soak_release.runtime_is_drained(drained, 16))

    def test_sampler_records_initial_live_snapshot_before_starting_thread(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sampler = soak_release.Sampler(os.getpid(), "", Path(temporary) / "samples.jsonl", 85)
            sampler.start = lambda: None
            gpu = {"sampled_at_utc": "2026-10-04T00:00:00+00:00",
                   "temperature.gpu": 55, "memory.used": 1024,
                   "clocks_throttle_reasons.sw_thermal_slowdown": "Not Active",
                   "clocks_throttle_reasons.hw_thermal_slowdown": "Not Active"}
            sampler.begin(gpu, {"kv": {}, "resources": {},
                                 "scheduler": {"event_queue_rejections": 0}})
            self.assertEqual(len(sampler.samples), 1)
            self.assertEqual(sampler.samples[0]["elapsed_s"], 0.0)
            self.assertEqual(sampler.samples[0]["gpu"], gpu)
            self.assertFalse(sampler.thermal_violation.is_set())
            self.assertFalse(sampler.telemetry_invalid.is_set())


if __name__ == "__main__":
    unittest.main()
