#!/usr/bin/env python3
"""Run an offered-load sweep and retain a compact CSV curve."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload", required=True)
    parser.add_argument("--rates", required=True, help="comma-separated offered requests/s")
    parser.add_argument("--requests", type=int, default=100)
    parser.add_argument("--concurrency", type=int, default=32)
    parser.add_argument("--url", default="http://127.0.0.1:8080/v1/completions")
    parser.add_argument("--model", default="local-model")
    parser.add_argument("--output", type=Path, default=Path("bench/runs/sweep"))
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, rate_text in enumerate(args.rates.split(",")):
        rate = float(rate_text)
        command = [sys.executable, "bench/run.py", "--workload", args.workload,
                   "--mode", "open_fixed", "--rate", str(rate), "--requests", str(args.requests),
                   "--concurrency", str(args.concurrency), "--url", args.url, "--model", args.model,
                   "--seed", str(args.seed), "--output", str(args.output)]
        subprocess.run(command, check=True)
        run_dir = max(args.output.iterdir(), key=lambda path: path.stat().st_mtime_ns)
        summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
        row = {
            "offered_rate_rps": rate,
            "requests_per_s": summary["requests_per_s"],
            "output_tokens_per_s": summary["output_tokens_per_s"],
            "processed_tokens_per_s": summary["processed_tokens_per_s"],
            "goodput_requests_per_s": summary["goodput_requests_per_s"],
            "errors": summary["sample_count"] - summary["successful_requests"],
            "max_send_lateness_ms": summary["max_send_lateness_ms"],
            "valid": summary["valid"],
            "statuses": json.dumps(summary["statuses"], sort_keys=True),
            "run_id": summary["run_id"],
        }
        for metric in ("ttft_ms", "e2e_ms", "itl_ms", "arrival_lateness_ms"):
            for percentile, value in summary[metric].items():
                row[f"{metric}_{percentile}"] = value
        rows.append(row)
    path = args.output / "saturation.csv"
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(path)


if __name__ == "__main__":
    main()
