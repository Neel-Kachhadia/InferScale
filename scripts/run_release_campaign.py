#!/usr/bin/env python3
"""Reproducibly run paired InferScale CUDA policy trials with GPU telemetry."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.metadata
import json
import os
import platform
import random
import re
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_MODEL_SHA256 = "9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0"
POLICIES: dict[str, list[str]] = {
    "fifo": ["--scheduler", "fifo"],
    "sawds_static": ["--scheduler", "sawds", "--static-prefill"],
    "sawds_adaptive": ["--scheduler", "sawds", "--adaptive-prefill",
                       "--adaptive-min", "512", "--adaptive-max", "2048",
                       "--adaptive-initial", "2048", "--adaptive-step", "256",
                       "--adaptive-high-wait-ms", "750",
                       "--adaptive-low-wait-ms", "500"],
}
def common_server_args(chunk_tokens: int, generation_reserve_tokens: int) -> list[str]:
    return [
    "--host", "127.0.0.1", "--ctx-size-per-seq", "2048", "--parallel", "16",
    "--batch-size", "2048", "--ubatch-size", "2048", "--gpu-layers", "99",
    "--threads", "8", "--threads-batch", "8", "--threads-http", "4",
    "--prefill-epoch-tokens", "2048", "--scheduler-prefill-chunk", str(chunk_tokens),
    "--generation-reserve-tokens", str(generation_reserve_tokens),
]
GPU_QUERY = (
    "timestamp,index,temperature.gpu,clocks.gr,clocks.mem,power.draw.average,"
    "power.draw.instant,enforced.power.limit,power.default_limit,power.max_limit,"
    "utilization.gpu,memory.used,memory.total,"
    "clocks_throttle_reasons.sw_thermal_slowdown,"
    "clocks_throttle_reasons.hw_thermal_slowdown,"
    "clocks_throttle_reasons.hw_slowdown,clocks_throttle_reasons.sw_power_cap"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def display_path(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path.resolve())


def command_version(command: list[str]) -> str:
    result = subprocess.run(command, check=False, capture_output=True, text=True, timeout=15)
    return (result.stdout + result.stderr).strip()


def compiler_manifest(server: Path) -> dict[str, Any]:
    cache_path = server.parent / "CMakeCache.txt"
    llama_source = server.parent / "_deps/llama_cpp-src"
    if not cache_path.is_file() or not (llama_source / ".git").exists():
        raise RuntimeError("server is not accompanied by a CMake cache and pinned llama.cpp source")
    cache: dict[str, str] = {}
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line and not line.startswith("#") and ":" in line and "=" in line:
            key, _, value = line.partition("=")
            cache[key.partition(":")[0]] = value
    source_directory_value = cache.get("CMAKE_HOME_DIRECTORY")
    if not source_directory_value:
        raise RuntimeError("server build cache does not identify its InferScale source directory")
    source_directory = Path(source_directory_value).resolve()
    if source_directory != ROOT.resolve():
        raise RuntimeError(
            f"server was configured from {source_directory}, expected campaign source {ROOT.resolve()}")
    backend_commit = subprocess.check_output(
        ["git", "-C", str(llama_source), "rev-parse", "HEAD"], text=True).strip()
    if backend_commit != "b29c606e28a01b1bc8c1351026a0fa6e616bf6c4":
        raise RuntimeError(f"server backend source is not the frozen llama.cpp pin: {backend_commit}")
    try:
        cpu_model = next(line.split(":", 1)[1].strip() for line in
                         Path("/proc/cpuinfo").read_text().splitlines()
                         if line.lower().startswith("model name"))
    except (OSError, StopIteration):
        cpu_model = platform.processor()
    ram_total = None
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemTotal:"):
                ram_total = line.split()[1] + " kB"
                break
    except OSError:
        pass
    return {
        "kernel": platform.release(), "platform": platform.platform(),
        "cpu": cpu_model, "ram_total": ram_total,
        "python": sys.version.splitlines()[0],
        "compiler": {"path": cache.get("CMAKE_CXX_COMPILER"),
                     "version": command_version([cache.get("CMAKE_CXX_COMPILER", "c++"), "--version"])},
        "cmake": command_version(["cmake", "--version"]).splitlines()[0],
        "ninja": command_version(["ninja", "--version"]).splitlines()[0],
        "cuda_compiler": command_version(["nvcc", "--version"]).splitlines()[-1],
        "cuda_build": {key: cache.get(key) for key in
                       ("CMAKE_BUILD_TYPE", "GGML_CUDA", "INFERSCALE_WITH_LLAMA",
                        "INFERSCALE_ENABLE_NVTX", "CMAKE_CUDA_ARCHITECTURES",
                        "CMAKE_CXX_FLAGS", "CMAKE_CUDA_FLAGS")},
        "cmake_preset_equivalent": "cuda-release; fresh out-of-tree Ninja Release build",
        "cmake_source_directory": str(source_directory),
        "cmake_source_commit": git("rev-parse", "HEAD"),
        "cmake_source_tracked_diff_sha256": hashlib.sha256(
            git("diff", "HEAD", "--binary").encode()).hexdigest(),
        "llama_cpp_source": str(llama_source), "llama_cpp_commit": backend_commit,
    }


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def request_json(url: str, timeout: float = 2.0) -> dict[str, Any] | None:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            value = json.loads(response.read())
            return value if isinstance(value, dict) else None
    except (OSError, urllib.error.URLError, json.JSONDecodeError):
        return None


def wait_ready(url: str, process: subprocess.Popen[bytes], timeout_s: float = 180) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited during startup with status {process.returncode}")
        health = request_json(url)
        ready = request_json(url.rsplit("/", 1)[0] + "/ready")
        if health and health.get("status") == "ok" and ready and ready.get("ready"):
            return
        time.sleep(0.25)
    raise TimeoutError(f"server was not ready within {timeout_s:.0f}s: {url}")


def warmup(url: str) -> None:
    import httpx

    payload = {"model": "local-model", "prompt": "word word word", "max_tokens": 2,
               "stream": True, "stream_options": {"include_usage": True}}
    with httpx.Client(timeout=60) as client:
        with client.stream("POST", url, json=payload) as response:
            if response.status_code != 200:
                raise RuntimeError(f"warmup request returned HTTP {response.status_code}")
            if not any(line.startswith("data: [DONE]") for line in response.iter_lines()):
                raise RuntimeError("warmup SSE stream had no [DONE] marker")


def gpu_sample() -> dict[str, Any]:
    fields = GPU_QUERY.split(",")
    process = subprocess.run(["nvidia-smi", f"--query-gpu={GPU_QUERY}",
                              "--format=csv,noheader,nounits"],
                             check=False, capture_output=True, text=True, timeout=8)
    if process.returncode != 0:
        return {"sampled_at_utc": datetime.now(timezone.utc).isoformat(),
                "error": process.stderr.strip() or f"nvidia-smi exit {process.returncode}"}
    rows = list(csv.reader(process.stdout.splitlines(), skipinitialspace=True))
    if not rows or len(rows[0]) != len(fields):
        return {"sampled_at_utc": datetime.now(timezone.utc).isoformat(),
                "error": f"unexpected nvidia-smi CSV: {process.stdout.strip()}"}
    row = dict(zip(fields, rows[0]))
    sample: dict[str, Any] = {"sampled_at_utc": datetime.now(timezone.utc).isoformat()}
    for key, value in row.items():
        value = value.strip()
        try:
            sample[key] = float(value) if "." in value else int(value)
        except ValueError:
            sample[key] = value
    return sample


class GpuSampler:
    def __init__(self, output: Path, interval_s: float = 1.0, max_temperature_c: int = 85):
        self.output = output
        self.interval_s = interval_s
        self.max_temperature_c = max_temperature_c
        self.stop_event = threading.Event()
        self.thermal_violation = threading.Event()
        self.thread: threading.Thread | None = None
        self.samples: list[dict[str, Any]] = []
        self.started_monotonic = 0.0

    def start(self, initial_sample: dict[str, Any] | None = None) -> None:
        self.started_monotonic = time.monotonic()
        if initial_sample is not None:
            self.samples.append(initial_sample)
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        while not self.stop_event.is_set():
            started = time.monotonic()
            sample = gpu_sample()
            self.samples.append(sample)
            temperature = sample.get("temperature.gpu")
            thermal_reasons = ("clocks_throttle_reasons.sw_thermal_slowdown",
                               "clocks_throttle_reasons.hw_thermal_slowdown")
            active_thermal_reason = any(str(sample.get(field, "N/A")) not in
                                        ("Not Active", "N/A", "None")
                                        for field in thermal_reasons)
            if (isinstance(temperature, (int, float)) and
                    temperature >= self.max_temperature_c) or active_thermal_reason:
                self.thermal_violation.set()
            self.stop_event.wait(max(0.0, self.interval_s - (time.monotonic() - started)))

    def close(self) -> None:
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=10)
        self.output.write_text("".join(json.dumps(s) + "\n" for s in self.samples),
                               encoding="utf-8")


def gpu_sample_summary(samples: list[dict[str, Any]]) -> dict[str, Any]:
    def numbers(field: str) -> list[float]:
        return [float(sample[field]) for sample in samples
                if isinstance(sample.get(field), (int, float))]

    temperatures = numbers("temperature.gpu")
    clocks = numbers("clocks.gr")
    average_powers = numbers("power.draw.average")
    instantaneous_powers = numbers("power.draw.instant")
    utilizations = numbers("utilization.gpu")
    memories = numbers("memory.used")
    current_limits = [sample.get("enforced.power.limit") for sample in samples
                      if isinstance(sample.get("enforced.power.limit"), (int, float))]
    default_limits = [sample.get("power.default_limit") for sample in samples
                      if isinstance(sample.get("power.default_limit"), (int, float))]
    max_limits = [sample.get("power.max_limit") for sample in samples
                  if isinstance(sample.get("power.max_limit"), (int, float))]
    thermal_reasons = ("clocks_throttle_reasons.sw_thermal_slowdown",
                       "clocks_throttle_reasons.hw_thermal_slowdown")
    thermal_slowdown_samples = sum(
        any(str(sample.get(field, "N/A")) not in ("Not Active", "N/A", "None")
            for field in thermal_reasons)
        for sample in samples)
    impossible_average_samples = 0
    impossible_instant_samples = 0
    for sample in samples:
        max_limit = sample.get("power.max_limit")
        if not isinstance(max_limit, (int, float)):
            max_limit = sample.get("enforced.power.limit")
        if isinstance(max_limit, (int, float)):
            average = sample.get("power.draw.average")
            instant = sample.get("power.draw.instant")
            if isinstance(average, (int, float)) and (
                    average < 0 or average > max_limit * 1.25):
                impossible_average_samples += 1
            if isinstance(instant, (int, float)) and (
                    instant < 0 or instant > max_limit * 1.25):
                impossible_instant_samples += 1
    average_power_valid = bool(average_powers) and impossible_average_samples == 0
    instant_power_valid = bool(instantaneous_powers) and impossible_instant_samples == 0
    return {
        "gpu_sample_count": len(samples),
        "gpu_mean_power_w": (sum(average_powers) / len(average_powers)
                              if average_power_valid else None),
        "gpu_mean_instantaneous_power_w": (
            sum(instantaneous_powers) / len(instantaneous_powers)
            if instant_power_valid else None),
        "gpu_power_telemetry_valid": average_power_valid,
        "gpu_instant_power_telemetry_valid": instant_power_valid,
        "gpu_power_impossible_average_sample_count": impossible_average_samples,
        "gpu_power_impossible_instant_sample_count": impossible_instant_samples,
        "gpu_power_average_sample_count": len(average_powers),
        "gpu_power_instant_sample_count": len(instantaneous_powers),
        "gpu_mean_utilization_percent": sum(utilizations) / len(utilizations)
            if utilizations else None,
        "gpu_peak_memory_used_mib": max(memories) if memories else None,
        "gpu_power_limit_w": current_limits[0] if current_limits else None,
        "gpu_default_power_limit_w": default_limits[0] if default_limits else None,
        "gpu_max_power_limit_w": max_limits[0] if max_limits else None,
        "gpu_max_temperature_c": max(temperatures) if temperatures else None,
        "gpu_thermal_slowdown_samples": thermal_slowdown_samples,
        "gpu_mean_graphics_clock_mhz": sum(clocks) / len(clocks) if clocks else None,
        "gpu_min_graphics_clock_mhz": min(clocks) if clocks else None,
        "gpu_max_graphics_clock_mhz": max(clocks) if clocks else None,
    }


def parse_resolved_configuration(server_log: Path, metrics: dict[str, Any] | None) -> dict[str, Any]:
    text = server_log.read_text(encoding="utf-8", errors="replace")
    patterns = {
        "context_per_sequence": r"context_per_sequence_resolved=(\d+)",
        "total_context": r"total_context_resolved=(\d+)",
        "parallel_sequences": r"parallel_resolved=(\d+)",
        "batch_tokens": r"batch_size_resolved=(\d+)",
        "ubatch_tokens": r"ubatch_size_resolved=(\d+)",
    }
    resolved: dict[str, Any] = {}
    for key, pattern in patterns.items():
        match = re.search(pattern, text)
        if not match:
            raise RuntimeError(f"could not read resolved {key} from server startup log")
        resolved[key] = int(match.group(1))
    cuda = re.search(r"cuda_backend_available=(true|false)", text)
    if not cuda:
        raise RuntimeError("could not read CUDA backend availability from server startup log")
    resolved["cuda_backend_available"] = cuda.group(1) == "true"
    if not resolved["cuda_backend_available"]:
        raise RuntimeError("server startup log reports that CUDA is unavailable")
    if not metrics or not isinstance(metrics.get("kv"), dict):
        raise RuntimeError("could not read runtime configuration from /metrics")
    resolved["hard_logical_tokens"] = metrics["kv"].get("hard_usable_tokens")
    log_values = {
        "scheduler": r"scheduler=(fifo|sawds)",
        "adaptive_prefill_enabled": r"adaptive_prefill=(true|false)",
        "generation_reserve_tokens": r"generation_reserve_tokens=(\d+)",
        "prefill_epoch_tokens": r"prefill_epoch_tokens=(\d+)",
        "chunk_tokens": r"scheduler_prefill_chunk_tokens=(\d+)",
    }
    for key, pattern in log_values.items():
        match = re.search(pattern, text)
        if not match:
            raise RuntimeError(f"could not read resolved {key} from server startup log")
        value = match.group(1)
        resolved[key] = (value == "true" if key == "adaptive_prefill_enabled"
                         else int(value) if value.isdigit() else value)
    metrics_scheduler = metrics.get("scheduler", {}).get("mode")
    metrics_adaptive = metrics.get("adaptive_prefill", {}).get("enabled")
    if resolved["scheduler"] != metrics_scheduler:
        raise RuntimeError("startup log and /metrics disagree on resolved scheduler")
    if resolved["adaptive_prefill_enabled"] is not metrics_adaptive:
        raise RuntimeError("startup log and /metrics disagree on adaptive prefill state")
    metrics_epoch_tokens = metrics.get("adaptive_prefill", {}).get("epoch_budget_tokens")
    if not isinstance(metrics_epoch_tokens, int):
        raise RuntimeError("runtime did not report a resolved prefill epoch budget")
    if resolved["prefill_epoch_tokens"] != metrics_epoch_tokens:
        raise RuntimeError("startup log and /metrics disagree on resolved prefill epoch budget")
    resolved["prefill_epoch_tokens"] = metrics_epoch_tokens
    if not isinstance(resolved["hard_logical_tokens"], int):
        raise RuntimeError("runtime did not report a resolved logical hard capacity")
    return resolved


def validate_resolved_configuration(resolved: dict[str, Any], treatment: str,
                                    expected: dict[str, Any]) -> None:
    """Fail closed if the running server does not match the requested treatment."""
    expected_scheduler = "fifo" if treatment == "fifo" else "sawds"
    expected_adaptive = treatment == "sawds_adaptive"
    checks = {
        **expected,
        "scheduler": expected_scheduler,
        "adaptive_prefill_enabled": expected_adaptive,
    }
    for key, value in checks.items():
        if resolved.get(key) != value:
            raise RuntimeError(
                f"resolved configuration mismatch for {treatment}: {key} "
                f"requested/expected {value!r}, observed {resolved.get(key)!r}")


def stop_server(process: subprocess.Popen[bytes]) -> bool:
    if process.poll() is not None:
        return True
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=12)
        return process.returncode == 0
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
        return False


def terminate_process(process: subprocess.Popen[bytes], grace_s: float = 5.0) -> None:
    """Stop a child before continuing after an invalid load or thermal abort."""
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=grace_s)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=grace_s)


def run_one(args: argparse.Namespace, trial: int, treatment: str, seed: int, rate: float,
            campaign_dir: Path) -> dict[str, Any]:
    run_root = campaign_dir / "runs"
    run_root.mkdir(parents=True, exist_ok=True)
    port = free_port()
    base_url = f"http://127.0.0.1:{port}"
    completions_url = base_url + "/v1/completions"
    run_label = f"trial-{trial:02d}-r{rate:g}-{treatment}"
    server_log = campaign_dir / f"{run_label}.server.log"
    gpu_path = campaign_dir / f"{run_label}.gpu.jsonl"
    command = [str(args.server), "--model", str(args.model), "--port", str(port),
               *common_server_args(args.chunk_tokens, args.generation_reserve_tokens),
               *POLICIES[treatment]]
    context_path = campaign_dir / f"{run_label}.context.json"
    run_context: dict[str, Any] = {
        "trial": trial, "treatment": treatment, "seed": seed,
        "offered_rate_rps": rate, "requested": args.requests,
        "workload": str(args.workload), "campaign_purpose": args.purpose,
        "server_command": command, "gpu_samples": display_path(gpu_path),
        "server_log": display_path(server_log),
    }
    env = os.environ.copy()
    env["CUDA_VISIBLE_DEVICES"] = args.cuda_visible_devices
    cooldown_started = time.monotonic()
    cooldown_deadline = cooldown_started + args.cooldown_timeout_s
    while True:
        start_sample = gpu_sample()
        start_temperature = start_sample.get("temperature.gpu")
        thermal_reasons = ("clocks_throttle_reasons.sw_thermal_slowdown",
                           "clocks_throttle_reasons.hw_thermal_slowdown")
        active_thermal_reason = any(str(start_sample.get(field, "N/A")) not in
                                    ("Not Active", "N/A", "None")
                                    for field in thermal_reasons)
        if not isinstance(start_temperature, (int, float)):
            raise RuntimeError(f"cannot validate GPU start temperature: {start_sample.get('error', start_sample)}")
        if start_temperature <= args.max_start_temperature_c and not active_thermal_reason:
            break
        if time.monotonic() >= cooldown_deadline:
            raise TimeoutError(f"GPU did not cool to {args.max_start_temperature_c} C within "
                               f"{args.cooldown_timeout_s} seconds")
        print(f"cooldown waiting: gpu={start_temperature}C thermal_throttle={active_thermal_reason}",
              flush=True)
        time.sleep(min(5.0, cooldown_deadline - time.monotonic()))
    cooldown_seconds = time.monotonic() - cooldown_started
    started = datetime.now(timezone.utc).isoformat()
    run_context.update({"started_at_utc": started,
                        "gpu_start_temperature_c": start_temperature,
                        "gpu_start_sample": start_sample})
    context_path.write_text(json.dumps(run_context, indent=2) + "\n", encoding="utf-8")
    with server_log.open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log,
                                   stderr=subprocess.STDOUT)
        sampler = GpuSampler(gpu_path, max_temperature_c=args.max_gpu_temperature_c)
        sampler.start(start_sample)
        try:
            wait_ready(base_url + "/health", process)
            startup_metrics = request_json(base_url + "/metrics")
            resolved_configuration = parse_resolved_configuration(server_log, startup_metrics)
            validate_resolved_configuration(resolved_configuration, treatment, {
                "context_per_sequence": 2048,
                "total_context": 32768,
                "parallel_sequences": 16,
                "batch_tokens": 2048,
                "ubatch_tokens": 2048,
                "hard_logical_tokens": 29491,
                "chunk_tokens": args.chunk_tokens,
                "generation_reserve_tokens": args.generation_reserve_tokens,
                "prefill_epoch_tokens": 2048,
            })
            warmup(completions_url)
            generator_command = [
                sys.executable, str(ROOT / "bench/run.py"), "--workload", str(args.workload),
                "--url", completions_url, "--model", "local-model", "--output", str(run_root),
                "--requests", str(args.requests), "--mode", args.mode,
                "--rate", str(rate), "--concurrency", str(args.concurrency),
                "--seed", str(seed), "--max-send-lateness-ms", str(args.max_sender_lateness_ms),
                "--metrics-sample-interval-ms", "250", "--require-server-metrics",
            ]
            run_context["loadgen_command"] = generator_command
            context_path.write_text(json.dumps(run_context, indent=2) + "\n",
                                    encoding="utf-8")
            log_path = campaign_dir / f"{run_label}.loadgen.log"
            run_dirs_before = {path for path in run_root.iterdir() if path.is_dir()}
            with log_path.open("wb") as load_log:
                load_process = subprocess.Popen(generator_command, cwd=ROOT, env=env,
                                                stdout=load_log, stderr=subprocess.STDOUT)
                load_deadline = time.monotonic() + args.run_timeout_s
                while load_process.poll() is None:
                    if sampler.thermal_violation.is_set():
                        terminate_process(load_process)
                        raise RuntimeError("GPU reached a thermal validity limit during the trial")
                    if time.monotonic() >= load_deadline:
                        terminate_process(load_process, grace_s=1.0)
                        raise TimeoutError(f"load generator exceeded {args.run_timeout_s}s")
                    time.sleep(0.2)
                if load_process.returncode != 0:
                    raise RuntimeError(f"load generator exited {load_process.returncode}; see {log_path}")
            run_dirs_after = {path for path in run_root.iterdir() if path.is_dir()}
            new_runs = sorted(run_dirs_after - run_dirs_before)
            if len(new_runs) != 1:
                raise RuntimeError(f"expected one loadgen output, found {len(new_runs)}")
            run_dir = new_runs[0]
            summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
            server_stopped_cleanly = stop_server(process)
            sampler.close()
            elapsed = time.monotonic() - sampler.started_monotonic
            gpu_errors = sum("error" in sample for sample in sampler.samples)
            expected_gpu_samples = max(1, int(elapsed / sampler.interval_s))
            gpu_coverage = min(1.0, len(sampler.samples) / expected_gpu_samples)
            gpu_summary = gpu_sample_summary(sampler.samples)
            thermal_valid = (gpu_summary["gpu_max_temperature_c"] is not None and
                             gpu_summary["gpu_max_temperature_c"] < args.max_gpu_temperature_c
                             and gpu_summary["gpu_thermal_slowdown_samples"] == 0
                             and not sampler.thermal_violation.is_set())
            return {
                "trial": trial, "treatment": treatment, "seed": seed,
                "started_at_utc": started, "finished_at_utc": datetime.now(timezone.utc).isoformat(),
                "offered_rate_rps": rate, "requested": args.requests,
                "run_id": summary["run_id"], "run_dir": display_path(run_dir),
                "summary": display_path(run_dir / "summary.json"),
                "records": display_path(run_dir / "requests.jsonl"),
                "server_metrics": display_path(run_dir / "server-metrics.jsonl"),
                "server_log": display_path(server_log),
                "loadgen_log": display_path(log_path),
                "gpu_samples": display_path(gpu_path),
                "server_command": command, "loadgen_command": generator_command,
                "requested_configuration": {
                    "context_per_sequence": 2048, "parallel_sequences": 16,
                    "batch_tokens": 2048, "ubatch_tokens": 2048, "gpu_layers": 99,
                    "threads": 8, "batch_threads": 8, "http_threads": 4,
                    "hard_logical_tokens": "resolved by RuntimeReactor",
                    "soft_pressure_percent": 70, "high_pressure_percent": 85,
                    "safety_margin_tokens": 16,
                    "generation_reserve_tokens": args.generation_reserve_tokens,
                    "chunk_tokens": args.chunk_tokens, "static_epoch_prefill_tokens": 2048,
                    "sawds_weights_interactive_batch": [1024, 256],
                    "credit_cap_tokens": 4096, "starvation_alarm_ms": 5000,
                    "sampling": "greedy", "http_body_limit_bytes": 65536,
                    "prompt_limit_bytes": 32768, "output_token_limit": 128,
                    "stream_buffer_bytes": 4096,
                },
                "resolved_configuration": resolved_configuration,
                "gpu_start_temperature_c": start_temperature,
                "gpu_cooldown_seconds": cooldown_seconds,
                "server_stopped_cleanly": server_stopped_cleanly,
                "gpu_telemetry_coverage": gpu_coverage,
                "gpu_telemetry_errors": gpu_errors,
                **gpu_summary,
                "thermal_valid": thermal_valid,
                "valid": bool(summary.get("valid")) and server_stopped_cleanly and
                         thermal_valid and gpu_coverage >= 0.9 and gpu_errors == 0,
                "status_counts": summary.get("statuses"),
                "send_lateness_p99_ms": summary.get("arrival_lateness_ms", {}).get("p99"),
                "max_send_lateness_ms": summary.get("max_send_lateness_ms"),
                "successful_requests": summary.get("successful_requests"),
            }
        finally:
            stopped_cleanly = True
            if process.poll() is None:
                stopped_cleanly = stop_server(process)
            sampler.close()
            run_context["server_stopped_cleanly"] = stopped_cleanly
            run_context["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
            context_path.write_text(json.dumps(run_context, indent=2) + "\n",
                                    encoding="utf-8")


def invalid_run_record(args: argparse.Namespace, trial: int, treatment: str, seed: int,
                       rate: float, campaign_dir: Path, error: Exception,
                       runs_before: set[Path], order: list[str]) -> dict[str, Any]:
    label = f"trial-{trial:02d}-r{rate:g}-{treatment}"
    context_path = campaign_dir / f"{label}.context.json"
    context = json.loads(context_path.read_text(encoding="utf-8")) if context_path.exists() else {}
    gpu_path = campaign_dir / f"{label}.gpu.jsonl"
    gpu_samples: list[dict[str, Any]] = []
    if gpu_path.exists():
        for line in gpu_path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                sample = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(sample, dict):
                gpu_samples.append(sample)
    temperatures = [sample.get("temperature.gpu") for sample in gpu_samples
                    if isinstance(sample.get("temperature.gpu"), (int, float))]
    thermal_fields = ("clocks_throttle_reasons.sw_thermal_slowdown",
                      "clocks_throttle_reasons.hw_thermal_slowdown")
    thermal_slowdown = sum(any(
        str(sample.get(field, "N/A")) not in ("Not Active", "N/A", "None")
        for field in thermal_fields) for sample in gpu_samples)
    thermal_abort = ("thermal" in str(error).lower() or
                     any(value >= args.max_gpu_temperature_c for value in temperatures) or
                     thermal_slowdown > 0)
    new_runs = sorted(set((campaign_dir / "runs").glob("*")) - runs_before)
    run_dir = new_runs[-1] if new_runs else None
    summary_path = run_dir / "summary.json" if run_dir is not None else None
    records_path = run_dir / "requests.jsonl" if run_dir is not None else None
    metrics_path = run_dir / "server-metrics.jsonl" if run_dir is not None else None
    summary = {}
    if summary_path is not None and summary_path.is_file():
        try:
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            summary = {}
    records: list[dict[str, Any]] = []
    if records_path is not None and records_path.is_file():
        for line in records_path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(record, dict):
                records.append(record)
    lateness = [max(0.0, (record["send_ns"] - record["intended_arrival_ns"]) / 1e6)
                for record in records
                if isinstance(record.get("send_ns"), int) and
                isinstance(record.get("intended_arrival_ns"), int)]
    gpu_errors = sum("error" in sample for sample in gpu_samples)
    status = "INVALID_THERMAL" if thermal_abort else "INVALID_RUN"
    return {
        "trial": trial, "treatment": treatment, "seed": seed,
        "started_at_utc": context.get("started_at_utc"),
        "finished_at_utc": datetime.now(timezone.utc).isoformat(),
        "offered_rate_rps": rate, "requested": args.requests,
        "run_id": summary.get("run_id") or
                  (records[0].get("run_id") if records else None),
        "run_dir": display_path(run_dir) if run_dir is not None else None,
        "summary": display_path(summary_path) if summary_path is not None and
                   summary_path.exists() else None,
        "records": display_path(records_path) if records_path is not None and
                   records_path.exists() else None,
        "server_metrics": display_path(metrics_path) if metrics_path is not None and
                          metrics_path.exists() else None,
        "server_log": context.get("server_log"),
        "loadgen_log": display_path(campaign_dir / f"{label}.loadgen.log"),
        "gpu_samples": display_path(gpu_path),
        "context": display_path(context_path) if context_path.exists() else None,
        "server_command": context.get("server_command"),
        "loadgen_command": context.get("loadgen_command"),
        "gpu_telemetry_errors": gpu_errors,
        "gpu_sample_count": len(gpu_samples),
        "gpu_start_temperature_c": context.get("gpu_start_temperature_c"),
        "gpu_max_temperature_c": max(temperatures, default=None),
        "gpu_thermal_slowdown_samples": thermal_slowdown,
        "thermal_valid": False if thermal_abort else (
            bool(temperatures) and max(temperatures) < args.max_gpu_temperature_c and
            thermal_slowdown == 0),
        "server_stopped_cleanly": context.get("server_stopped_cleanly"),
        "partial_record_count": len(records),
        "status_counts": summary.get("statuses"),
        "max_send_lateness_ms": max(lateness, default=None),
        "failure_reason": f"{type(error).__name__}: {error}",
        "invalid_classification": status,
        "valid": False,
        "treatment_order": order,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--workload", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/release-v1/benchmark"))
    parser.add_argument("--purpose", choices=("release_comparison", "reproduction_smoke",
                                                "targeted_correctness"),
                        default="release_comparison")
    parser.add_argument("--treatments", default="fifo,sawds_static,sawds_adaptive")
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--rate", type=float, default=8.0)
    parser.add_argument("--rates", help="optional comma-separated load curve; overrides --rate")
    parser.add_argument("--chunk-tokens", type=int, default=512)
    parser.add_argument("--generation-reserve-tokens", type=int, default=64)
    parser.add_argument("--mode", choices=("open_fixed", "open_poisson"), default="open_poisson")
    parser.add_argument("--concurrency", type=int, default=32)
    parser.add_argument("--seed", type=int, default=51003)
    parser.add_argument("--max-sender-lateness-ms", type=float, default=100.0)
    parser.add_argument("--max-start-temperature-c", type=int, default=65)
    parser.add_argument("--max-gpu-temperature-c", type=int, default=85)
    parser.add_argument("--cooldown-timeout-s", type=int, default=1200)
    parser.add_argument("--run-timeout-s", type=int, default=7200)
    parser.add_argument("--cuda-visible-devices", default="0")
    args = parser.parse_args()
    args.server = args.server.resolve()
    args.model = args.model.resolve()
    args.workload = args.workload.resolve()
    if not args.server.is_file() or not os.access(args.server, os.X_OK):
        parser.error(f"server executable is missing or not executable: {args.server}")
    if not args.model.is_file() or sha256(args.model) != EXPECTED_MODEL_SHA256:
        parser.error("model is missing or does not match the frozen TinyLlama SHA-256")
    rates = ([float(item.strip()) for item in args.rates.split(",") if item.strip()]
             if args.rates else [args.rate])
    if (not args.workload.is_file() or args.requests <= 0 or args.trials <= 0 or
            not rates or any(rate <= 0 for rate in rates) or args.chunk_tokens <= 0 or
            args.generation_reserve_tokens < 0 or args.max_start_temperature_c <= 0 or
            args.max_gpu_temperature_c <= args.max_start_temperature_c or
            args.cooldown_timeout_s <= 0 or len(set(rates)) != len(rates)):
        parser.error("workload, requests, trials, rate, chunk, or reserve is invalid")
    treatments = [item.strip() for item in args.treatments.split(",") if item.strip()]
    if (not treatments or len(set(treatments)) != len(treatments) or
            any(item not in POLICIES for item in treatments)):
        parser.error(f"unknown treatment; choose from {', '.join(POLICIES)}")
    if args.trials < 5:
        print("warning: fewer than five trials; do not report headline confidence intervals",
              file=sys.stderr)

    out = args.output.resolve()
    campaign_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    campaign_dir = out / campaign_id
    campaign_dir.mkdir(parents=True, exist_ok=False)
    try:
        gpu = subprocess.run(["nvidia-smi", "-L"], check=True, capture_output=True, text=True)
        environment = compiler_manifest(args.server)
        help_run = subprocess.run([str(args.server), "--help"], check=False,
                                  capture_output=True, text=True, timeout=15)
        if help_run.returncode != 0 or "--model" not in help_run.stdout:
            raise RuntimeError("server --help failed; binary is not the expected InferScale server")
        import yaml
        workload_config = yaml.safe_load(args.workload.read_text(encoding="utf-8"))
        manifest = {
            "campaign_id": campaign_id,
            "campaign_purpose": args.purpose,
            "created_at_utc": datetime.now(timezone.utc).isoformat(),
            "repository_commit": git("rev-parse", "HEAD"),
            "working_tree_diff_sha256": hashlib.sha256(git("diff", "HEAD", "--binary").encode()).hexdigest(),
            "environment": environment,
            "backend": {"name": "llama.cpp", "version": "v0.4.1",
                        "commit": environment["llama_cpp_commit"]},
            "model": {"path": str(args.model), "bytes": args.model.stat().st_size,
                      "sha256": sha256(args.model), "quantization": "Q4_K_M"},
            "server": {"path": str(args.server), "sha256": sha256(args.server),
                       "help_sha256": hashlib.sha256(help_run.stdout.encode()).hexdigest()},
            "gpu_inventory": gpu.stdout.strip(),
            "workload": {"path": str(args.workload), "sha256": sha256(args.workload)},
            "slo_contract": {name: {"ttft_ms": request.get("ttft_slo_ms"),
                                     "e2e_ms": request.get("e2e_slo_ms"),
                                     "deadline_ms": request.get("deadline_ms")}
                             for request in workload_config.get("classes", [])
                             for name in [request.get("name", "unspecified")]},
            "tooling": {
                "python_dependencies": {
                    name: importlib.metadata.version(name)
                    for name in ("httpx", "PyYAML")},
                "direct_cpp_dependencies": {
                    "Boost": "1.83.0 exact", "GoogleTest": "1.14.0 exact",
                    "nlohmann/json": "3.11.3 exact"},
                "dependency_lock_sha256": sha256(ROOT / "requirements-bench.txt"),
                "load_generator_sha256": sha256(ROOT / "bench/run.py"),
                "statistics_sha256": sha256(ROOT / "bench/stats.py"),
                "analysis_sha256": sha256(ROOT / "bench/release_analysis.py"),
                "load_generator_commit": git("rev-parse", "HEAD"),
                "analysis_commit": git("rev-parse", "HEAD"),
            },
            "load": {"requests_per_trial": args.requests, "trial_count": args.trials,
                     "offered_rates_rps": rates, "arrival_mode": args.mode,
                     "concurrency": args.concurrency, "base_seed": args.seed,
                     "max_sender_lateness_ms": args.max_sender_lateness_ms,
                     "prefill_chunk_tokens": args.chunk_tokens,
                     "generation_reserve_tokens": args.generation_reserve_tokens},
            "policies": {key: POLICIES[key] for key in treatments},
            "requested_configuration": {
                "context_per_sequence": 2048, "parallel_sequences": 16,
                "batch_tokens": 2048, "ubatch_tokens": 2048,
                "gpu_layers": 99, "threads": 8, "batch_threads": 8,
                "hard_logical_tokens": 29491, "soft_pressure_percent": 70,
                "high_pressure_percent": 85, "safety_margin_tokens": 16,
                "generation_reserve_tokens": args.generation_reserve_tokens,
                "chunk_tokens": args.chunk_tokens,
                "static_epoch_prefill_tokens": 2048,
                "sawds_weights_interactive_batch": [1024, 256],
                "credit_cap_tokens": 4096, "starvation_alarm_ms": 5000,
                "sampling": "greedy", "http_threads": 4,
                "http_body_limit_bytes": 65536, "prompt_limit_bytes": 32768,
                "output_token_limit": 128, "stream_buffer_bytes": 4096,
            },
            "validity": {"max_sender_lateness_ms": args.max_sender_lateness_ms,
                         "minimum_gpu_metrics_coverage": 0.9,
                         "max_start_temperature_c": args.max_start_temperature_c,
                         "max_paired_start_temperature_delta_c": 5,
                         "max_gpu_temperature_c": args.max_gpu_temperature_c,
                         "cooldown_timeout_s": args.cooldown_timeout_s,
                         "no_thermal_throttle": True},
        }
        (campaign_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                                    encoding="utf-8")
        order_rng = random.Random(args.seed)
        index_path = campaign_dir / "run-index.jsonl"
        with index_path.open("w", encoding="utf-8") as index:
            for rate_index, rate in enumerate(rates):
                for trial in range(1, args.trials + 1):
                    order = list(treatments)
                    order_rng.shuffle(order)
                    paired_seed = args.seed + trial * 17
                    manifest.setdefault("trial_orders", []).append(
                        {"trial": trial, "seed": paired_seed, "rate": rate, "order": order})
                    (campaign_dir / "manifest.json").write_text(
                        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
                    for treatment in order:
                        print(f"rate={rate:g} trial={trial}/{args.trials} treatment={treatment} seed={paired_seed}",
                              flush=True)
                        runs_before = set((campaign_dir / "runs").glob("*"))
                        try:
                            result = run_one(args, trial, treatment, paired_seed, rate,
                                             campaign_dir)
                        except Exception as error:
                            result = invalid_run_record(
                                args, trial, treatment, paired_seed, rate, campaign_dir,
                                error, runs_before, order)
                            index.write(json.dumps(result, separators=(",", ":")) + "\n")
                            index.flush()
                            manifest.setdefault("invalid_attempts", []).append({
                                "trial": trial, "treatment": treatment, "seed": paired_seed,
                                "rate": rate,
                                "classification": result["invalid_classification"],
                                "failure_reason": result["failure_reason"],
                                "context": result.get("context"),
                                "gpu_samples": result.get("gpu_samples"),
                            })
                            (campaign_dir / "manifest.json").write_text(
                                json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
                            if result["invalid_classification"] != "INVALID_THERMAL":
                                raise
                            print(f"retained invalid thermal trial={trial} treatment={treatment}; "
                                  "cooling before next treatment", file=sys.stderr, flush=True)
                            continue
                        result["treatment_order"] = order
                        index.write(json.dumps(result, separators=(",", ":")) + "\n")
                        index.flush()
                        if not result["valid"]:
                            raise RuntimeError(f"invalid run {result['run_id']}")
        (campaign_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                                    encoding="utf-8")
        print(campaign_dir)
        return 0
    except Exception as error:
        (campaign_dir / "campaign-error.txt").write_text(
            f"{type(error).__name__}: {error}\n", encoding="utf-8")
        print(f"campaign failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
