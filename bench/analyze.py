#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

from stats import summarize


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("records", type=Path)
    parser.add_argument("--elapsed-s", type=float, required=True)
    parser.add_argument("--max-send-lateness-ms", type=float, default=100.0)
    args = parser.parse_args()
    records = [json.loads(line) for line in args.records.read_text(encoding="utf-8").splitlines()]
    print(json.dumps(summarize(records, args.elapsed_s, args.max_send_lateness_ms), indent=2))


if __name__ == "__main__":
    main()
