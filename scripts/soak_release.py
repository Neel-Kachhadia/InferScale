#!/usr/bin/env python3
"""Run bounded repeated CUDA traffic and sample host/GPU/logical resource state."""

from __future__ import annotations

import argparse
import json
import os
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

from run_release_campaign import EXPECTED_MODEL_SHA256, gpu_sample, request_json, sha256, wait_ready, warmup

ROOT = Path(__file__).resolve().parents[1]


def rss_kib(pid: int) -> int | None:
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (OSError, ValueError):
        return None
    return None


class Sampler:
    def __init__(self, pid: int, metrics_url: str, output: Path):
        self.pid = pid
        self.metrics_url = metrics_url
        self.output = output
        self.stop_event = threading.Event()
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
            try:
                captured["runtime"] = request_json(self.metrics_url, timeout=2.0)
            except (OSError, urllib.error.URLError):
                captured["runtime"] = None
            self.samples.append(captured)
            self.stop_event.wait(5.0)

    def close(self) -> None:
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=12)
        self.output.write_text("".join(json.dumps(row) + "\n" for row in self.samples),
                               encoding="utf-8")

    def begin(self) -> None:
        self.started = time.monotonic()
        self.start()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/release-v1/soak"))
    parser.add_argument("--duration-minutes", type=float, default=15.0)
    parser.add_argument("--block-seconds", type=int, default=60)
    parser.add_argument("--rate", type=float, default=2.0)
    parser.add_argument("--max-temperature-c", type=int, default=85)
    parser.add_argument("--seed", type=int, default=61103)
    args = parser.parse_args()
    args.server, args.model = args.server.resolve(), args.model.resolve()
    if not args.server.is_file() or not os.access(args.server, os.X_OK):
        parser.error("--server must be an executable release server")
    if not args.model.is_file() or sha256(args.model) != EXPECTED_MODEL_SHA256:
        parser.error("model missing or checksum differs from frozen validation model")
    if args.duration_minutes < 1 or args.block_seconds < 10 or args.rate <= 0:
        parser.error("duration, block length, and offered rate must be positive")
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
                "configuration": {"context_per_sequence": 2048, "sequences": 16,
                                  "batch": 2048, "ubatch": 2048,
                                  "hard_logical_kv_tokens": 29491, "generation_reserve": 64},
                "validity": {"maximum_temperature_c": args.max_temperature_c,
                             "thermal_slowdown_reason": "must remain Not Active"}}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    log_path = out / "server.log"
    process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log_path.open("wb"),
                               stderr=subprocess.STDOUT)
    sampler = Sampler(process.pid, base + "/metrics", out / "resource-samples.jsonl")
    blocks: list[dict[str, Any]] = []
    started = time.monotonic()
    try:
        wait_ready(base + "/health", process)
        warmup(base + "/v1/completions")
        sampler.begin()
        block_index = 0
        target_s = args.duration_minutes * 60
        while time.monotonic() - started < target_s:
            latest = sampler.samples[-1]["gpu"] if sampler.samples else gpu_sample()
            temperature = latest.get("temperature.gpu")
            throttle = any(str(latest.get(field, "Not Active")) not in
                           ("Not Active", "N/A", "None") for field in
                           ("clocks_throttle_reasons.sw_thermal_slowdown",
                            "clocks_throttle_reasons.hw_thermal_slowdown"))
            if temperature is not None and float(temperature) >= args.max_temperature_c:
                raise RuntimeError(f"temperature limit reached: {temperature} C")
            if throttle:
                raise RuntimeError("GPU thermal slowdown became active")
            remaining_s = target_s - (time.monotonic() - started)
            requests = max(1, int(min(args.block_seconds, remaining_s) * args.rate))
            block_index += 1
            command_run = [sys.executable, str(ROOT / "bench/run.py"),
                           "--workload", str(ROOT / "bench/workloads/phase5_interactive.yaml"),
                           "--url", base + "/v1/completions", "--model", "local-model",
                           "--output", str(out / "requests"), "--requests", str(requests),
                           "--mode", "open_poisson", "--rate", str(args.rate),
                           "--concurrency", "32", "--seed", str(args.seed + block_index),
                           "--metrics-sample-interval-ms", "250", "--require-server-metrics",
                           "--max-send-lateness-ms", "100"]
            block_log = out / f"block-{block_index:03d}.log"
            with block_log.open("wb") as log:
                result = subprocess.run(command_run, cwd=ROOT, env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=3600, check=False)
            if result.returncode != 0:
                raise RuntimeError(f"load block {block_index} failed; see {block_log}")
            dirs = sorted((out / "requests").glob("*/summary.json"),
                          key=lambda path: path.stat().st_mtime_ns)
            summary = json.loads(dirs[-1].read_text(encoding="utf-8"))
            kv = (summary.get("server_metrics_after") or {}).get("kv", {})
            block = {"block": block_index, "run_id": summary.get("run_id"),
                     "elapsed_s": summary.get("elapsed_s"),
                     "successes": summary.get("successful_requests"),
                     "valid": summary.get("valid"), "record_drops": summary.get("record_drops"),
                     "max_send_lateness_ms": summary.get("max_send_lateness_ms"),
                     "charged_tokens_after": kv.get("charged_tokens"),
                     "committed_tokens_after": kv.get("committed_tokens"),
                     "soft_reserved_tokens_after": kv.get("soft_reserved_tokens"),
                     "available_sequences_after": kv.get("available_sequences"),
                     "active_sequences_after": 16 - kv.get("available_sequences", 16),
                     "max_gpu_temperature_c": max((sample["gpu"].get("temperature.gpu")
                                                    for sample in sampler.samples
                                                    if isinstance(sample["gpu"].get("temperature.gpu"), (int, float))),
                                                   default=None)}
            blocks.append(block)
            with (out / "block-summary.jsonl").open("a", encoding="utf-8") as summary_file:
                summary_file.write(json.dumps(block) + "\n")
            if not summary.get("valid"):
                raise RuntimeError(f"invalid request block {block_index}")
            if any(kv.get(field) != 0 for field in ("charged_tokens", "committed_tokens",
                                                    "soft_reserved_tokens", "authorized_tokens")):
                raise RuntimeError(f"resource counters did not return to zero after block {block_index}")
            if kv.get("available_sequences") != 16:
                raise RuntimeError(f"sequence allocator did not return all 16 sequences after block {block_index}")
            print(f"block={block_index} requests={requests} elapsed={summary.get('elapsed_s'):.1f}s "
                  f"success={summary.get('successful_requests')} rss_kib={rss_kib(process.pid)}",
                  flush=True)
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
    resource_metrics = [sample.get("runtime") for sample in sampler.samples
                        if isinstance(sample.get("runtime"), dict)]
    rss_values = [sample["host_rss_kib"] for sample in sampler.samples
                  if isinstance(sample.get("host_rss_kib"), int)]
    warm_count = max(1, len(rss_values) // 3)
    warm_rss = rss_values[:warm_count]
    late_rss = rss_values[-warm_count:]
    warm_median = sorted(warm_rss)[len(warm_rss) // 2] if warm_rss else None
    late_median = sorted(late_rss)[len(late_rss) // 2] if late_rss else None
    result = {"actual_duration_minutes": (time.monotonic() - started) / 60,
              "sample_count": len(sampler.samples), "blocks": blocks,
              "all_blocks_valid": bool(blocks) and all(block["valid"] for block in blocks),
              "zero_leases_after_each_block": bool(blocks) and all(
                  block["charged_tokens_after"] == 0 and block["committed_tokens_after"] == 0 and
                  block["soft_reserved_tokens_after"] == 0 and
                  block["available_sequences_after"] == 16 for block in blocks),
              "first_rss_kib": rss_values[0] if rss_values else None,
              "last_rss_kib": rss_values[-1] if rss_values else None,
              "max_rss_kib": max(rss_values, default=None),
              "warmup_third_median_rss_kib": warm_median,
              "final_third_median_rss_kib": late_median,
              "final_minus_warmup_median_rss_kib": (
                  late_median - warm_median if warm_median is not None and
                  late_median is not None else None),
              "final_runtime_snapshot": resource_metrics[-1] if resource_metrics else None,
              "result": "PASS" if return_code == 0 else "FAIL"}
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(out)
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
