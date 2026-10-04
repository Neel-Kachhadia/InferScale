#!/usr/bin/env python3
"""Run bounded repeated CUDA traffic and sample host/GPU/logical resource state."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import httpx

from run_release_campaign import (EXPECTED_MODEL_SHA256, gpu_sample, request_json, sha256,
                                 terminate_process, wait_ready, warmup)

ROOT = Path(__file__).resolve().parents[1]
RSS_PLATEAU_ABSOLUTE_ALLOWANCE_KIB = 64 * 1024
RSS_PLATEAU_RELATIVE_ALLOWANCE = 0.10


def blocks_are_valid(blocks: list[dict[str, Any]]) -> bool:
    return bool(blocks) and all(
        block.get("valid") is True and block.get("thermal_valid") is True
        for block in blocks)


def rss_plateau(warm_median_kib: int | None, final_median_kib: int | None) -> tuple[bool, int | None]:
    if warm_median_kib is None or final_median_kib is None:
        return False, None
    allowance = max(RSS_PLATEAU_ABSOLUTE_ALLOWANCE_KIB,
                    int(warm_median_kib * RSS_PLATEAU_RELATIVE_ALLOWANCE))
    return final_median_kib <= warm_median_kib + allowance, allowance


def gpu_memory_plateau(samples: list[int | float]) -> tuple[bool, float | None, float | None]:
    if len(samples) < 6:
        return False, None, None
    count = max(1, len(samples) // 3)
    first = sorted(samples[:count])[count // 2]
    final = sorted(samples[-count:])[count // 2]
    allowance = max(128.0, float(first) * 0.10)
    return final <= first + allowance, float(first), allowance


def resource_snapshot_violations(kv: dict[str, Any], resources: dict[str, Any],
                                 maximum_sequences: int,
                                 event_queue_rejections: int | None) -> list[str]:
    violations: list[str] = []
    required = ("charged_tokens", "committed_tokens", "soft_reserved_tokens",
                "authorized_tokens", "hard_usable_tokens", "available_sequences")
    if not all(field in kv for field in required):
        return ["missing_kv_counter"]
    charged = int(kv["charged_tokens"])
    accounted = (int(kv["committed_tokens"]) + int(kv["soft_reserved_tokens"])
                 + int(kv["authorized_tokens"]))
    if charged != accounted:
        violations.append("kv_accounting_drift")
    hard_capacity = int(kv["hard_usable_tokens"])
    if charged > hard_capacity or int(kv["committed_tokens"]) > hard_capacity:
        violations.append("hard_capacity_exceeded")
    resource_fields = ("live_sequences", "live_requests", "queued_requests", "inflight_tickets",
                       "stream_buffered_bytes", "stream_reserved_bytes", "event_queue_depth",
                       "engine_command_queue_depth", "engine_command_queue_capacity")
    if not all(field in resources for field in resource_fields):
        return violations + ["missing_runtime_resource_counter"]
    live_sequences = int(resources["live_sequences"])
    live_requests = int(resources["live_requests"])
    if live_sequences < 0 or live_sequences > maximum_sequences:
        violations.append("sequence_capacity_exceeded_or_missing")
    if int(kv["available_sequences"]) + live_sequences != maximum_sequences:
        violations.append("sequence_accounting_drift")
    if live_requests < 0 or live_requests > maximum_sequences:
        violations.append("request_slot_capacity_exceeded_or_missing")
    if int(resources["queued_requests"]) > live_requests:
        violations.append("queued_request_accounting_drift")
    if int(resources["inflight_tickets"]) > maximum_sequences:
        violations.append("inflight_ticket_capacity_exceeded")
    if (int(resources["stream_buffered_bytes"]) +
            int(resources["stream_reserved_bytes"]) > maximum_sequences * 4096):
        violations.append("stream_buffer_capacity_exceeded")
    if int(resources["event_queue_depth"]) > 1024:
        violations.append("event_queue_capacity_exceeded")
    command_capacity = int(resources["engine_command_queue_capacity"])
    if command_capacity <= 0 or int(resources["engine_command_queue_depth"]) > command_capacity:
        violations.append("engine_command_queue_capacity_exceeded")
    if event_queue_rejections is None or event_queue_rejections < 0:
        violations.append("missing_event_queue_rejection_counter")
    elif event_queue_rejections > 0:
        violations.append("event_queue_rejections_observed")
    if kv.get("pressure") not in {"normal", "soft", "high", "hard"}:
        violations.append("invalid_pressure_state")
    return violations


def runtime_is_drained(snapshot: dict[str, Any], maximum_sequences: int) -> bool:
    kv = snapshot.get("kv", {})
    resources = snapshot.get("resources", {})
    return (all(kv.get(field) == 0 for field in (
                "charged_tokens", "committed_tokens", "soft_reserved_tokens",
                "authorized_tokens")) and
            kv.get("available_sequences") == maximum_sequences and
            all(resources.get(field) == 0 for field in (
                "live_requests", "live_sequences", "queued_requests", "inflight_tickets",
                "stream_buffered_bytes", "stream_reserved_bytes", "event_queue_depth",
                "engine_command_queue_depth", "outstanding_engine_commands")))


def rss_kib(pid: int) -> int | None:
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (OSError, ValueError):
        return None
    return None


class Sampler:
    def __init__(self, pid: int, metrics_url: str, output: Path, max_temperature_c: int):
        self.pid = pid
        self.metrics_url = metrics_url
        self.output = output
        self.max_temperature_c = max_temperature_c
        self.stop_event = threading.Event()
        self.thermal_violation = threading.Event()
        self.telemetry_invalid = threading.Event()
        self.thread: threading.Thread | None = None
        self.samples: list[dict[str, Any]] = []

    def start(self) -> None:
        self.thread = threading.Thread(target=self._sample, daemon=True)
        self.thread.start()

    def _sample(self) -> None:
        while not self.stop_event.is_set():
            captured: dict[str, Any] = {
                "sampled_at_utc": datetime.now(timezone.utc).isoformat(),
                "elapsed_s": time.monotonic() - self.started,
                "host_rss_kib": rss_kib(self.pid),
                "gpu": gpu_sample(),
            }
            gpu = captured["gpu"]
            temperature = gpu.get("temperature.gpu")
            memory_used = gpu.get("memory.used")
            thermal_reasons = ("clocks_throttle_reasons.sw_thermal_slowdown",
                               "clocks_throttle_reasons.hw_thermal_slowdown")
            active_thermal_reason = any(str(gpu.get(field, "N/A")) not in
                                        ("Not Active", "N/A", "None")
                                        for field in thermal_reasons)
            if (not isinstance(temperature, (int, float)) or
                    not isinstance(memory_used, (int, float)) or
                    any(field not in gpu for field in thermal_reasons)):
                self.telemetry_invalid.set()
            if ((isinstance(temperature, (int, float)) and
                 temperature >= self.max_temperature_c) or active_thermal_reason):
                self.thermal_violation.set()
            try:
                captured["runtime"] = request_json(self.metrics_url, timeout=2.0)
            except (OSError, urllib.error.URLError):
                captured["runtime"] = None
            runtime = captured["runtime"]
            kv = runtime.get("kv", {}) if isinstance(runtime, dict) else {}
            resources = runtime.get("resources", {}) if isinstance(runtime, dict) else {}
            if (runtime is None or not all(field in kv for field in (
                    "charged_tokens", "committed_tokens", "soft_reserved_tokens",
                    "authorized_tokens", "hard_usable_tokens", "available_sequences")) or
                    not all(field in resources for field in (
                        "live_requests", "live_sequences", "queued_requests", "inflight_tickets",
                        "stream_buffered_bytes", "stream_reserved_bytes", "event_queue_depth",
                        "engine_command_queue_depth", "engine_command_queue_capacity",
                        "outstanding_engine_commands"))):
                self.telemetry_invalid.set()
            if not isinstance(runtime, dict) or not isinstance(
                    runtime.get("event_queue_rejections"), int):
                self.telemetry_invalid.set()
            self.samples.append(captured)
            self.stop_event.wait(1.0)

    def close(self) -> None:
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=12)
        self.output.write_text("".join(json.dumps(row) + "\n" for row in self.samples),
                               encoding="utf-8")

    def begin(self, initial_gpu: dict[str, Any],
              initial_runtime: dict[str, Any] | None) -> None:
        self.started = time.monotonic()
        temperature = initial_gpu.get("temperature.gpu")
        thermal_reasons = ("clocks_throttle_reasons.sw_thermal_slowdown",
                           "clocks_throttle_reasons.hw_thermal_slowdown")
        if (not isinstance(temperature, (int, float)) or
                temperature >= self.max_temperature_c or any(
                    str(initial_gpu.get(field, "N/A")) not in
                    ("Not Active", "N/A", "None") for field in thermal_reasons)):
            self.thermal_violation.set()
        if (not isinstance(temperature, (int, float)) or
                not isinstance(initial_gpu.get("memory.used"), (int, float)) or
                any(field not in initial_gpu for field in thermal_reasons) or
                not isinstance(initial_runtime, dict)):
            self.telemetry_invalid.set()
        self.samples.append({"sampled_at_utc": initial_gpu.get(
                                 "sampled_at_utc", datetime.now(timezone.utc).isoformat()),
                             "elapsed_s": 0.0,
                             "host_rss_kib": rss_kib(self.pid),
                             "gpu": initial_gpu,
                             "runtime": initial_runtime})
        self.start()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/release-v1/soak"))
    parser.add_argument("--duration-minutes", type=float, default=20.0)
    parser.add_argument("--block-seconds", type=int, default=60)
    parser.add_argument("--rate", type=float, default=2.0)
    parser.add_argument("--max-temperature-c", type=int, default=85)
    parser.add_argument("--max-start-temperature-c", type=int, default=65)
    parser.add_argument("--cool-start-temperature-c", type=int, default=60)
    parser.add_argument("--seed", type=int, default=61103)
    args = parser.parse_args()
    args.server, args.model = args.server.resolve(), args.model.resolve()
    if not args.server.is_file() or not os.access(args.server, os.X_OK):
        parser.error("--server must be an executable release server")
    if not args.model.is_file() or sha256(args.model) != EXPECTED_MODEL_SHA256:
        parser.error("model missing or checksum differs from frozen validation model")
    if (args.duration_minutes < 1 or args.block_seconds < 10 or args.rate <= 0 or
            args.max_start_temperature_c <= 0 or args.cool_start_temperature_c <= 0 or
            args.cool_start_temperature_c > args.max_start_temperature_c or
            args.max_temperature_c <= args.max_start_temperature_c):
        parser.error("invalid duration, block length, rate, or GPU temperature bound")
    cool_start_samples: list[dict[str, Any]] = []
    consecutive_cool_samples = 0
    cool_start_deadline = time.monotonic() + 900
    while consecutive_cool_samples < 5 and time.monotonic() < cool_start_deadline:
        sample = gpu_sample()
        cool_start_samples.append(sample)
        temperature = sample.get("temperature.gpu")
        thermal_fields = ("clocks_throttle_reasons.sw_thermal_slowdown",
                          "clocks_throttle_reasons.hw_thermal_slowdown")
        cool = (isinstance(temperature, (int, float)) and
                temperature <= args.cool_start_temperature_c and
                isinstance(sample.get("memory.used"), (int, float)) and
                all(str(sample.get(field, "N/A")) in ("Not Active", "N/A", "None")
                    for field in thermal_fields))
        consecutive_cool_samples = consecutive_cool_samples + 1 if cool else 0
        if consecutive_cool_samples < 5:
            time.sleep(1.0)
    if consecutive_cool_samples < 5:
        parser.error("GPU did not provide five consecutive cool-start samples within 15 minutes")
    start_sample = cool_start_samples[-1]
    start_temperature = start_sample.get("temperature.gpu")
    if not isinstance(start_temperature, (int, float)):
        parser.error(f"cannot validate GPU start temperature: {start_sample.get('error', start_sample)}")
    if start_temperature > args.max_start_temperature_c:
        parser.error(f"GPU start temperature {start_temperature} C exceeds cool-start limit "
                     f"{args.max_start_temperature_c} C")
    out = args.output.resolve() / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    (out / "requests").mkdir(parents=True, exist_ok=False)
    port = 0
    import socket
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = int(listener.getsockname()[1])
    base = f"http://127.0.0.1:{port}"
    command = [str(args.server), "--model", str(args.model), "--host", "127.0.0.1",
               "--port", str(port), "--ctx-size-per-seq", "2048", "--parallel", "16",
               "--batch-size", "2048", "--ubatch-size", "2048", "--gpu-layers", "99",
               "--threads", "8", "--threads-batch", "8", "--threads-http", "4",
               "--scheduler", "fifo", "--prefill-epoch-tokens", "2048",
               "--scheduler-prefill-chunk", "512", "--generation-reserve-tokens", "64"]
    env = os.environ.copy()
    env["CUDA_VISIBLE_DEVICES"] = "0"
    (out / "server-command.json").write_text(json.dumps(command, indent=2) + "\n")
    manifest = {"server": str(args.server), "model": str(args.model),
                "model_sha256": sha256(args.model), "duration_target_minutes": args.duration_minutes,
                "block_seconds": args.block_seconds, "offered_rate_rps": args.rate,
                "policy": "FIFO; static 2048-token prefill epoch; 512-token scheduler chunks",
                "workload_mix": {"primary": "phase5_mixed at configured Poisson rate",
                                 "cancel_every_n_blocks": 4,
                                 "cancel_offers": 20,
                                 "cancel_probability": 0.25,
                                 "memory_pressure_every_n_blocks": 5,
                                 "memory_pressure_offers": 8},
                "configuration": {"context_per_sequence": 2048, "sequences": 16,
                                  "batch": 2048, "ubatch": 2048,
                                  "hard_logical_kv_tokens": 29491, "generation_reserve": 64},
                "validity": {"maximum_temperature_c": args.max_temperature_c,
                             "maximum_start_temperature_c": args.max_start_temperature_c,
                             "cool_start_temperature_c": args.cool_start_temperature_c,
                             "consecutive_cool_start_samples": 5,
                             "cool_start_samples": cool_start_samples,
                             "start_temperature_c": start_temperature,
                             "thermal_slowdown_reason": "must remain Not Active"}}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    log_path = out / "server.log"
    process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log_path.open("wb"),
                               stderr=subprocess.STDOUT)
    sampler = Sampler(process.pid, base + "/metrics", out / "resource-samples.jsonl",
                      args.max_temperature_c)
    blocks: list[dict[str, Any]] = []
    server_shutdown_cleanly = False
    started = time.monotonic()
    try:
        wait_ready(base + "/health", process)
        initial_gpu = gpu_sample()
        initial_runtime = request_json(base + "/metrics")
        sampler.begin(initial_gpu, initial_runtime)
        warmup(base + "/v1/completions")
        started = time.monotonic()
        block_index = 0
        target_s = args.duration_minutes * 60

        def run_workload(workload: str, label: str, offer_count: int, seed: int,
                         mode: str | None, rate: float | None,
                         concurrency: int) -> dict[str, Any]:
            command_run = [sys.executable, str(ROOT / "bench/run.py"),
                           "--workload", str(ROOT / "bench/workloads" / workload),
                           "--url", base + "/v1/completions", "--model", "local-model",
                           "--output", str(out / "requests"), "--requests", str(offer_count),
                           "--concurrency", str(concurrency), "--seed", str(seed),
                           "--metrics-sample-interval-ms", "250", "--require-server-metrics",
                           "--max-send-lateness-ms", "100"]
            if mode is not None:
                command_run.extend(["--mode", mode])
            if rate is not None:
                command_run.extend(["--rate", str(rate)])
            block_log = out / f"block-{block_index:03d}-{label}.log"
            existing = set((out / "requests").glob("*/summary.json"))
            with block_log.open("wb") as log:
                load_process = subprocess.Popen(command_run, cwd=ROOT, env=env, stdout=log,
                                                stderr=subprocess.STDOUT)
                block_deadline = time.monotonic() + 3600
                while load_process.poll() is None:
                    if sampler.thermal_violation.is_set():
                        terminate_process(load_process)
                        raise RuntimeError("GPU thermal validity limit reached during load block")
                    if process.poll() is not None:
                        terminate_process(load_process, grace_s=1.0)
                        raise RuntimeError(f"server exited during load block: {process.returncode}")
                    if time.monotonic() >= block_deadline:
                        terminate_process(load_process, grace_s=1.0)
                        raise TimeoutError(f"load block {block_index} exceeded 3600 seconds")
                    time.sleep(0.25)
            if load_process.returncode != 0:
                raise RuntimeError(f"load block {block_index} {label} failed; see {block_log}")
            new_summaries = [path for path in (out / "requests").glob("*/summary.json")
                             if path not in existing]
            if len(new_summaries) != 1:
                raise RuntimeError(f"expected one completed request summary for {label}, "
                                   f"found {len(new_summaries)}")
            summary = json.loads(new_summaries[0].read_text(encoding="utf-8"))
            drain_deadline = time.monotonic() + 10.0
            drained = None
            while time.monotonic() < drain_deadline:
                drained = request_json(base + "/metrics", timeout=2.0)
                if drained is not None and runtime_is_drained(drained, 16):
                    break
                if drained is None:
                    sampler.telemetry_invalid.set()
                time.sleep(0.1)
            if drained is None or not runtime_is_drained(drained, 16):
                raise RuntimeError(f"runtime resources failed to drain after {label} workload")
            summary["soak_post_drain_metrics"] = drained
            return summary

        while time.monotonic() - started < target_s:
            if sampler.thermal_violation.is_set():
                raise RuntimeError("GPU reached its continuous thermal validity limit")
            remaining_s = target_s - (time.monotonic() - started)
            block_index += 1
            requests = max(1, int(min(args.block_seconds, remaining_s) * args.rate))
            sample_start = len(sampler.samples)
            run_summaries = [run_workload("phase5_mixed.yaml", "mixed", requests,
                                          args.seed + block_index * 10, "open_poisson",
                                          args.rate, 32)]
            if block_index % 4 == 0:
                run_summaries.append(run_workload(
                    "cancel.yaml", "cancel", 20, args.seed + block_index * 10 + 1,
                    "open_fixed", args.rate, 16))
            if block_index % 5 == 0:
                run_summaries.append(run_workload(
                    "memory_pressure.yaml", "pressure", 8,
                    args.seed + block_index * 10 + 2, None, None, 8))
            summary = run_summaries[-1]
            drain_snapshot = summary.get("soak_post_drain_metrics", {})
            kv = drain_snapshot.get("kv", {})
            resources = drain_snapshot.get("resources", {})
            metric_deltas = [item.get("server_metric_deltas") or {} for item in run_summaries]
            accepted = sum(int(delta.get(service_class, {}).get("admitted", 0))
                           for delta in metric_deltas for service_class in ("interactive", "batch"))
            rejected = sum(int(delta.get(service_class, {}).get("rejected", 0))
                           for delta in metric_deltas for service_class in ("interactive", "batch"))
            block_gpu_samples = sampler.samples[sample_start:]
            block_thermal_valid = (not sampler.thermal_violation.is_set() and
                                   bool(block_gpu_samples) and all(
                isinstance(item.get("gpu", {}).get("temperature.gpu"), (int, float)) and
                item["gpu"]["temperature.gpu"] < args.max_temperature_c
                for item in block_gpu_samples))
            block = {"block": block_index,
                     "run_ids": [item.get("run_id") for item in run_summaries],
                     "elapsed_s": sum(float(item.get("elapsed_s", 0)) for item in run_summaries),
                     "offered": sum(int(item.get("requested_count", 0)) for item in run_summaries),
                     "accepted": accepted,
                     "rejected": rejected,
                     "cancelled": sum(int(item.get("cancelled_request_count", 0)) for item in run_summaries),
                     "successes": sum(int(item.get("successful_requests", 0)) for item in run_summaries),
                     "valid": all(item.get("valid") is True for item in run_summaries),
                     "run_summaries": [{"workload": item.get("workload"),
                                        "run_id": item.get("run_id"),
                                        "requested_count": item.get("requested_count"),
                                        "successful_requests": item.get("successful_requests"),
                                        "cancelled_request_count": item.get("cancelled_request_count"),
                                        "record_drops": item.get("record_drops"),
                                        "max_send_lateness_ms": item.get("max_send_lateness_ms"),
                                        "statuses": item.get("statuses"),
                                        "error_classes": item.get("error_classes"),
                                        "server_metric_deltas": item.get("server_metric_deltas"),
                                        "valid": item.get("valid")}
                                       for item in run_summaries],
                     "record_drops": sum(int(item.get("record_drops", 0)) for item in run_summaries),
                     "max_send_lateness_ms": max(
                         (float(item.get("max_send_lateness_ms", 0)) for item in run_summaries),
                         default=None),
                     "thermal_valid": block_thermal_valid,
                     "charged_tokens_after": kv.get("charged_tokens"),
                     "committed_tokens_after": kv.get("committed_tokens"),
                     "soft_reserved_tokens_after": kv.get("soft_reserved_tokens"),
                     "authorized_tokens_after": kv.get("authorized_tokens"),
                     "available_sequences_after": kv.get("available_sequences"),
                     "live_requests_after": resources.get("live_requests"),
                     "live_sequences_after": resources.get("live_sequences"),
                     "queued_requests_after": resources.get("queued_requests"),
                     "inflight_tickets_after": resources.get("inflight_tickets"),
                     "stream_buffered_bytes_after": resources.get("stream_buffered_bytes"),
                     "stream_reserved_bytes_after": resources.get("stream_reserved_bytes"),
                     "event_queue_depth_after": resources.get("event_queue_depth"),
                     "engine_command_queue_depth_after": resources.get("engine_command_queue_depth"),
                     "outstanding_engine_commands_after": resources.get("outstanding_engine_commands"),
                     "event_queue_rejections_after": drain_snapshot.get("event_queue_rejections"),
                     "max_gpu_temperature_c": max((sample["gpu"].get("temperature.gpu")
                                                    for sample in block_gpu_samples
                                                    if isinstance(sample.get("gpu", {}).get("temperature.gpu"), (int, float))),
                                                   default=None)}
            blocks.append(block)
            with (out / "block-summary.jsonl").open("a", encoding="utf-8") as summary_file:
                summary_file.write(json.dumps(block) + "\n")
            if not blocks[-1]["valid"] or not block_thermal_valid:
                raise RuntimeError(f"invalid request block {block_index}")
            if any(kv.get(field) != 0 for field in ("charged_tokens", "committed_tokens",
                                                    "soft_reserved_tokens", "authorized_tokens")):
                raise RuntimeError(f"resource counters did not return to zero after block {block_index}")
            if kv.get("available_sequences") != 16:
                raise RuntimeError(f"sequence allocator did not return all 16 sequences after block {block_index}")
            if any(resources.get(field) != 0 for field in (
                    "live_requests", "live_sequences", "queued_requests", "inflight_tickets",
                    "stream_buffered_bytes", "stream_reserved_bytes", "event_queue_depth",
                    "engine_command_queue_depth",
                    "outstanding_engine_commands")):
                raise RuntimeError(f"runtime request/queue resources did not drain after block {block_index}")
            print(f"block={block_index} offers={block['offered']} elapsed={block['elapsed_s']:.1f}s "
                  f"success={block['successes']} cancelled={block['cancelled']} rss_kib={rss_kib(process.pid)}",
                  flush=True)
        final_gpu = gpu_sample()
        final_temperature = final_gpu.get("temperature.gpu")
        final_runtime = request_json(base + "/metrics")
        if (not isinstance(final_temperature, (int, float)) or
                not isinstance(final_gpu.get("memory.used"), (int, float)) or
                final_runtime is None):
            sampler.telemetry_invalid.set()
        sampler.samples.append({"sampled_at_utc": final_gpu["sampled_at_utc"],
                                "elapsed_s": time.monotonic() - sampler.started,
                                "host_rss_kib": rss_kib(process.pid), "gpu": final_gpu,
                                "runtime": final_runtime})
        if (not isinstance(final_temperature, (int, float)) or
                final_temperature >= args.max_temperature_c or any(
                    str(final_gpu.get(field, "N/A")) not in ("Not Active", "N/A", "None")
                    for field in ("clocks_throttle_reasons.sw_thermal_slowdown",
                                  "clocks_throttle_reasons.hw_thermal_slowdown"))):
            sampler.thermal_violation.set()
            raise RuntimeError("final GPU thermal sample violates the soak validity limit")
    except Exception as error:
        (out / "failure.txt").write_text(f"{type(error).__name__}: {error}\n", encoding="utf-8")
        print(f"soak failed: {type(error).__name__}: {error}", file=sys.stderr)
        return_code = 1
    else:
        return_code = 0
    finally:
        sampler.close()
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=12)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
                return_code = 1
        if process.returncode not in (0, -signal.SIGTERM):
            return_code = 1
        server_shutdown_cleanly = process.returncode == 0
        if not server_shutdown_cleanly:
            return_code = 1
    resource_metrics = [sample.get("runtime") for sample in sampler.samples
                        if isinstance(sample.get("runtime"), dict)]
    rss_values = [sample["host_rss_kib"] for sample in sampler.samples
                  if isinstance(sample.get("host_rss_kib"), int)]
    warm_count = max(1, len(rss_values) // 3)
    warm_rss = rss_values[:warm_count]
    late_rss = rss_values[-warm_count:]
    warm_median = sorted(warm_rss)[len(warm_rss) // 2] if warm_rss else None
    late_median = sorted(late_rss)[len(late_rss) // 2] if late_rss else None
    rss_is_stable, rss_plateau_allowance_kib = rss_plateau(warm_median, late_median)
    temperature_samples = [sample["gpu"].get("temperature.gpu") for sample in sampler.samples
                           if isinstance(sample.get("gpu", {}).get("temperature.gpu"), (int, float))]
    gpu_memory_samples = [sample["gpu"].get("memory.used") for sample in sampler.samples
                          if isinstance(sample.get("gpu", {}).get("memory.used"), (int, float))]
    gpu_memory_stable, gpu_memory_warm_median_mib, gpu_memory_plateau_allowance_mib = (
        gpu_memory_plateau(gpu_memory_samples))
    thermal_valid = (not sampler.thermal_violation.is_set() and bool(temperature_samples) and
                     max(temperature_samples) < args.max_temperature_c and
                     start_temperature <= args.max_start_temperature_c)
    runtime_coverage = (len(resource_metrics) / len(sampler.samples)) if sampler.samples else 0.0
    telemetry_valid = not sampler.telemetry_invalid.is_set() and runtime_coverage >= 0.99
    if not thermal_valid or not telemetry_valid:
        return_code = 1
    kv_snapshots = [sample.get("runtime", {}).get("kv", {}) for sample in sampler.samples
                    if isinstance(sample.get("runtime"), dict)]
    resource_snapshots = [sample.get("runtime", {}).get("resources", {})
                          for sample in sampler.samples
                          if isinstance(sample.get("runtime"), dict)]
    resource_violations = [violation
                           for sample in sampler.samples
                           if isinstance(sample.get("runtime"), dict)
                           for violation in resource_snapshot_violations(
                               sample["runtime"].get("kv", {}),
                               sample["runtime"].get("resources", {}), 16,
                               sample["runtime"].get("event_queue_rejections")
                               if isinstance(sample["runtime"].get("event_queue_rejections"), int)
                               else None)]
    pressure_states = [snapshot.get("pressure") for snapshot in kv_snapshots]
    pressure_transitions = [state for index, state in enumerate(pressure_states)
                            if index == 0 or state != pressure_states[index - 1]]
    max_values = lambda snapshots, key: max(
        (int(snapshot.get(key, 0)) for snapshot in snapshots), default=None)
    start_kv = kv_snapshots[0] if kv_snapshots else {}
    end_kv = kv_snapshots[-1] if kv_snapshots else {}
    start_resources = resource_snapshots[0] if resource_snapshots else {}
    end_resources = resource_snapshots[-1] if resource_snapshots else {}
    post_drain = runtime_is_drained(resource_metrics[-1], 16) if resource_metrics else False
    actual_duration_seconds = time.monotonic() - started
    duration_complete = actual_duration_seconds >= args.duration_minutes * 60
    server_log_text = log_path.read_text(encoding="utf-8", errors="replace").lower()
    oom_markers = re.findall(r"(?:cuda|gpu).{0,80}(?:out of memory|failed to allocate)|"
                             r"(?:out of memory|failed to allocate).{0,80}(?:cuda|gpu)",
                             server_log_text)
    if (resource_violations or oom_markers or not post_drain or not rss_is_stable or
            not gpu_memory_stable or not duration_complete or not server_shutdown_cleanly):
        return_code = 1
    result = {"actual_duration_minutes": actual_duration_seconds / 60,
              "duration_complete": duration_complete,
              "sample_count": len(sampler.samples), "blocks": blocks,
              "all_blocks_valid": blocks_are_valid(blocks),
              "all_request_records_valid": bool(blocks) and all(block.get("valid") is True
                                                                  for block in blocks),
              "zero_leases_after_each_block": bool(blocks) and all(
                  block["charged_tokens_after"] == 0 and block["committed_tokens_after"] == 0 and
                  block["soft_reserved_tokens_after"] == 0 and
                  block["available_sequences_after"] == 16 for block in blocks),
              "runtime_resources_drain_after_each_block": bool(blocks) and all(
                  all(block.get(field) == 0 for field in (
                      "live_requests_after", "live_sequences_after", "queued_requests_after",
                      "inflight_tickets_after",
                      "stream_buffered_bytes_after", "stream_reserved_bytes_after",
                      "event_queue_depth_after", "engine_command_queue_depth_after",
                      "outstanding_engine_commands_after")) for block in blocks),
              "hard_capacity_violations": resource_violations.count("hard_capacity_exceeded"),
              "resource_invariant_violations": resource_violations,
              "pressure_states_observed": pressure_states,
              "pressure_transitions": pressure_transitions,
              "avoidable_oom_markers": oom_markers,
              "offered_requests_total": sum(int(block.get("offered", 0)) for block in blocks),
              "accepted_requests_total": sum(int(block.get("accepted", 0)) for block in blocks),
              "rejected_requests_total": sum(int(block.get("rejected", 0)) for block in blocks),
              "cancelled_requests_total": sum(int(block.get("cancelled", 0)) for block in blocks),
              "successful_requests_total": sum(int(block.get("successes", 0)) for block in blocks),
              "record_drops_total": sum(int(block.get("record_drops", 0)) for block in blocks),
              "maximum_sender_lateness_ms": max(
                  (float(block.get("max_send_lateness_ms", 0) or 0) for block in blocks),
                  default=None),
              "post_drain_counters_zero": post_drain,
              "server_shutdown_cleanly": server_shutdown_cleanly,
              "first_rss_kib": rss_values[0] if rss_values else None,
              "last_rss_kib": rss_values[-1] if rss_values else None,
              "max_rss_kib": max(rss_values, default=None),
              "rss_stable_plateau": rss_is_stable,
              "rss_plateau_allowance_kib": rss_plateau_allowance_kib,
              "warmup_third_median_rss_kib": warm_median,
              "final_third_median_rss_kib": late_median,
              "gpu_memory_stable_plateau": gpu_memory_stable,
              "gpu_memory_warmup_median_mib": gpu_memory_warm_median_mib,
              "gpu_memory_plateau_allowance_mib": gpu_memory_plateau_allowance_mib,
              "final_minus_warmup_median_rss_kib": (
                  late_median - warm_median if warm_median is not None and
                  late_median is not None else None),
              "final_runtime_snapshot": resource_metrics[-1] if resource_metrics else None,
              "runtime_metrics_coverage": runtime_coverage,
              "telemetry_valid": telemetry_valid,
              "kv_start": {field: start_kv.get(field) for field in
                           ("charged_tokens", "committed_tokens", "soft_reserved_tokens",
                            "authorized_tokens", "available_sequences")},
              "kv_peak": {"charged_tokens": max_values(kv_snapshots, "peak_charged_tokens"),
                          "committed_tokens": max_values(kv_snapshots, "committed_tokens"),
                          "soft_reserved_tokens": max_values(kv_snapshots, "soft_reserved_tokens"),
                          "authorized_tokens": max_values(kv_snapshots, "authorized_tokens")},
              "kv_end": {field: end_kv.get(field) for field in
                         ("charged_tokens", "committed_tokens", "soft_reserved_tokens",
                          "authorized_tokens", "available_sequences")},
              "requests_start_peak_end": {
                  "start": start_resources.get("live_requests"),
                  "peak": max_values(resource_snapshots, "live_requests"),
                  "end": end_resources.get("live_requests")},
              "sequences_start_peak_end": {
                  "start": start_resources.get("live_sequences"),
                  "peak": max_values(resource_snapshots, "live_sequences"),
                  "end": end_resources.get("live_sequences")},
              "queued_requests_peak": max_values(resource_snapshots, "queued_requests"),
              "inflight_tickets_peak": max_values(resource_snapshots, "inflight_tickets"),
              "event_queue_depth_peak": max_values(resource_snapshots, "event_queue_depth"),
              "runtime_event_queue_rejections_peak": max(
                  (int(sample.get("event_queue_rejections", 0)) for sample in resource_metrics),
                  default=None),
              "engine_command_queue_depth_peak": max_values(
                  resource_snapshots, "engine_command_queue_depth"),
              "gpu_memory_start_peak_end_mib": {
                  "start": gpu_memory_samples[0] if gpu_memory_samples else None,
                  "peak": max(gpu_memory_samples, default=None),
                  "end": gpu_memory_samples[-1] if gpu_memory_samples else None},
              "gpu_start_temperature_c": start_temperature,
              "gpu_max_temperature_c": max(temperature_samples, default=None),
              "gpu_end_temperature_c": temperature_samples[-1] if temperature_samples else None,
              "thermal_valid": thermal_valid,
              "result": "PASS" if return_code == 0 and thermal_valid and
                        telemetry_valid and rss_is_stable and gpu_memory_stable and
                        post_drain and duration_complete and server_shutdown_cleanly else "FAIL"}
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(out)
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
