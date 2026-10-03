#!/usr/bin/env python3
"""Run an open-loop, non-causal reference trial against pinned llama-server."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from run_release_campaign import (  # noqa: E402
    EXPECTED_MODEL_SHA256, GpuSampler, display_path, free_port, gpu_sample_summary, sha256,
    warmup,
)

EXPECTED_BACKEND_COMMIT = "b29c606e28a01b1bc8c1351026a0fa6e616bf6c4"


def wait_health(url: str, process: subprocess.Popen[bytes], timeout_s: float = 180) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"llama-server exited during startup: {process.returncode}")
        try:
            with urllib.request.urlopen(url, timeout=2) as response:
                if response.status == 200:
                    return
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.25)
    raise TimeoutError("llama-server health endpoint did not become ready")


def stop_server(process: subprocess.Popen[bytes]) -> bool:
    if process.poll() is not None:
        return process.returncode == 0
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=12)
        return process.returncode == 0
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True,
                        help="pinned llama.cpp source used to build the external binary")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--workload", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/release-v1/benchmark"))
    parser.add_argument("--requests", type=int, default=500)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--rate", type=float, default=24.0)
    parser.add_argument("--seed", type=int, default=51003)
    args = parser.parse_args()
    args.server, args.source, args.model, args.workload = (
        args.server.resolve(), args.source.resolve(), args.model.resolve(), args.workload.resolve())
    if not args.server.is_file() or not os.access(args.server, os.X_OK):
        parser.error("--server must be the external llama-server executable")
    if sha256(args.model) != EXPECTED_MODEL_SHA256:
        parser.error("external reference model checksum does not match frozen validation model")
    backend_commit = subprocess.check_output(
        ["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True).strip()
    if backend_commit != EXPECTED_BACKEND_COMMIT:
        parser.error(f"external reference source is not the pinned llama.cpp commit: {backend_commit}")
    if args.requests <= 0 or args.trials <= 0 or args.rate <= 0:
        parser.error("requests, trials, and offered rate must be positive")

    campaign_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    campaign = args.output.resolve() / "external" / campaign_id
    runs_root = campaign / "runs"
    runs_root.mkdir(parents=True, exist_ok=False)
    manifest = {
        "campaign_id": campaign_id, "treatment": "external_llama_server",
        "causal_status": "system reference only; not a scheduler-causal comparison",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "backend": {"name": "llama.cpp", "version": "v0.4.1", "commit": backend_commit},
        "binary": {"path": str(args.server), "sha256": sha256(args.server)},
        "model": {"path": str(args.model), "bytes": args.model.stat().st_size,
                  "sha256": sha256(args.model), "quantization": "Q4_K_M"},
        "workload": {"path": str(args.workload), "sha256": sha256(args.workload)},
        "load": {"requests_per_trial": args.requests, "trial_count": args.trials,
                 "offered_rate_rps": args.rate, "arrival_mode": "open_poisson",
                 "concurrency": 128, "base_seed": args.seed,
                 "max_sender_lateness_ms": 100.0},
        "configuration": {
            "context_total": 32768, "parallel": 16, "context_per_sequence": 2048,
            "batch_tokens": 2048, "ubatch_tokens": 2048, "continuous_batching": True,
            "kv_unified": False, "flash_attention": "off", "gpu_layers": 99,
            "threads": 8, "threads_batch": 8, "threads_http": 4,
            "cache_type_k": "f16", "cache_type_v": "f16",
            "sampling": "greedy: seed=1,temp=0,top_k=1,top_p=1",
            "metrics_enabled": True,
        },
        "environment": {"kernel": os.uname().release,
                         "gpu_inventory": subprocess.run(
                             ["nvidia-smi", "-L"], check=True, capture_output=True,
                             text=True).stdout.strip(),
                         "cuda_compiler": subprocess.run(
                             ["nvcc", "--version"], check=True, capture_output=True,
                             text=True).stdout.splitlines()[-1]},
        "validity": {"max_sender_lateness_ms": 100,
                     "max_gpu_temperature_c": 85,
                     "no_thermal_throttle": True},
    }
    (campaign / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                             encoding="utf-8")
    index_path = campaign / "run-index.jsonl"
    for trial in range(1, args.trials + 1):
        port = free_port()
        base = f"http://127.0.0.1:{port}"
        command = [str(args.server), "--model", str(args.model), "--alias", "local-model",
                   "--host", "127.0.0.1", "--port", str(port), "--ctx-size", "32768",
                   "--parallel", "16", "--batch-size", "2048", "--ubatch-size", "2048",
                   "--cont-batching", "--no-kv-unified", "--flash-attn", "off",
                   "--n-gpu-layers", "99", "--threads", "8", "--threads-batch", "8",
                   "--threads-http", "4", "--cache-type-k", "f16", "--cache-type-v", "f16",
                   "--seed", "1", "--temp", "0", "--top-k", "1", "--top-p", "1",
                   "--no-webui", "--metrics"]
        label = f"trial-{trial:02d}-r{args.rate:g}-external"
        trial_runs_root = runs_root / f"trial-{trial:02d}"
        trial_runs_root.mkdir()
        server_log = campaign / f"{label}.server.log"
        gpu_path = campaign / f"{label}.gpu.jsonl"
        env = os.environ.copy()
        env["CUDA_VISIBLE_DEVICES"] = "0"
        with server_log.open("wb") as log:
            process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log,
                                       stderr=subprocess.STDOUT)
        sampler = GpuSampler(gpu_path)
        sampler.start()
        started = datetime.now(timezone.utc).isoformat()
        try:
            wait_health(base + "/health", process)
            warmup(base + "/v1/completions")
            generator_command = [sys.executable, str(ROOT / "bench/run.py"),
                                 "--workload", str(args.workload), "--url",
                                 base + "/v1/completions", "--model", "local-model",
                                 "--output", str(trial_runs_root), "--requests", str(args.requests),
                                 "--mode", "open_poisson", "--rate", str(args.rate),
                                 "--concurrency", "128", "--seed", str(args.seed + trial * 17),
                                 "--max-send-lateness-ms", "100", "--skip-server-metrics"]
            load_log = campaign / f"{label}.loadgen.log"
            with load_log.open("wb") as log:
                load_process = subprocess.Popen(generator_command, cwd=ROOT, env=env,
                                                stdout=log, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 3600
                while load_process.poll() is None:
                    if sampler.thermal_violation.is_set():
                        load_process.terminate()
                        load_process.wait(timeout=5)
                        raise RuntimeError("GPU reached a thermal validity limit during external trial")
                    if time.monotonic() >= deadline:
                        load_process.kill()
                        load_process.wait(timeout=5)
                        raise TimeoutError("external load generator exceeded 3600s")
                    time.sleep(0.2)
            if load_process.returncode != 0:
                raise RuntimeError(f"external load generator exited {load_process.returncode}")
            run_dirs = sorted(trial_runs_root.glob("*/summary.json"),
                              key=lambda path: path.stat().st_mtime_ns)
            if len(run_dirs) != 1:
                raise RuntimeError(f"expected one external loadgen summary; got {len(run_dirs)}")
            summary_path = run_dirs[-1]
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
            server_clean = stop_server(process)
            sampler.close()
            gpu_summary = gpu_sample_summary(sampler.samples)
            temperatures = [sample.get("temperature.gpu") for sample in sampler.samples
                            if isinstance(sample.get("temperature.gpu"), (int, float))]
            thermal_valid = (bool(temperatures) and max(temperatures) < 85 and
                             not sampler.thermal_violation.is_set())
            elapsed = time.monotonic() - sampler.started_monotonic
            gpu_coverage = min(1.0, len(sampler.samples) / max(1, int(elapsed / sampler.interval_s)))
            gpu_errors = sum("error" in sample for sample in sampler.samples)
            row: dict[str, Any] = {
                "trial": trial, "treatment": "external_llama_server",
                "seed": args.seed + trial * 17, "offered_rate_rps": args.rate,
                "requested": args.requests, "run_id": summary["run_id"],
                "run_dir": display_path(summary_path.parent),
                "summary": display_path(summary_path),
                "records": display_path(summary_path.parent / "requests.jsonl"),
                "server_metrics": None,
                "server_log": display_path(server_log),
                "loadgen_log": display_path(load_log),
                "gpu_samples": display_path(gpu_path),
                "server_command": command, "loadgen_command": generator_command,
                "server_stopped_cleanly": server_clean,
                "gpu_telemetry_coverage": gpu_coverage,
                "gpu_telemetry_errors": gpu_errors,
                **gpu_summary,
                "thermal_valid": thermal_valid,
                "valid": bool(summary.get("valid")) and server_clean and thermal_valid and
                         gpu_coverage >= 0.9 and gpu_errors == 0,
                "status_counts": summary.get("statuses"),
                "send_lateness_p99_ms": summary.get("arrival_lateness_ms", {}).get("p99"),
                "max_send_lateness_ms": summary.get("max_send_lateness_ms"),
                "successful_requests": summary.get("successful_requests"),
            }
            with index_path.open("a", encoding="utf-8") as output:
                output.write(json.dumps(row, separators=(",", ":")) + "\n")
            if not row["valid"]:
                raise RuntimeError(f"external trial {trial} is invalid")
            print(f"external trial={trial}/{args.trials} success={summary.get('successful_requests')} "
                  f"ttft_p99_ms={summary.get('ttft_ms', {}).get('p99')}", flush=True)
        finally:
            if process.poll() is None:
                stop_server(process)
            sampler.close()
    print(campaign)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
