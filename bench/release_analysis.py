#!/usr/bin/env python3
"""Summarize release trials at the independent-trial level and render SVGs."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any, Callable


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def percentile(values: list[float], q: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    pos = (len(ordered) - 1) * q
    lo = int(pos)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (pos - lo)


def bootstrap_median_interval(values: list[float], seed: int,
                              repetitions: int = 20_000) -> tuple[float | None, float | None]:
    if not values:
        return None, None
    rng = random.Random(seed)
    estimates = [statistics.median(rng.choices(values, k=len(values)))
                 for _ in range(repetitions)]
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def read_lines(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()]


def metric_fields(summary: dict[str, Any], row: dict[str, Any], *,
                  campaign_error: str | None = None,
                  max_start_temperature_c: float | None = None) -> dict[str, Any]:
    classes = summary.get("by_class", {})
    deltas = summary.get("server_metric_deltas") or {}
    result: dict[str, Any] = {
        "run_id": summary.get("run_id"),
        "workload": summary.get("workload"),
        "trial": row.get("trial"),
        "seed": row.get("seed"),
        "treatment": row.get("treatment"),
        "offered_rate_rps": summary.get("offered_rate_rps"),
        "requests_offered": summary.get("requested_count"),
        "requests_succeeded": summary.get("successful_requests"),
        "requests_per_s": summary.get("requests_per_s"),
        "output_tokens_per_s": summary.get("output_tokens_per_s"),
        "goodput_requests_per_s": summary.get("goodput_requests_per_s"),
        "overall_ttft_p99_ms": summary.get("ttft_ms", {}).get("p99"),
        "overall_e2e_p99_ms": summary.get("e2e_ms", {}).get("p99"),
        "max_send_lateness_ms": summary.get("max_send_lateness_ms"),
        "server_metrics_coverage": summary.get("server_metrics_sampling_coverage"),
        "server_metrics_fresh": summary.get("server_metrics_fresh_for_client_results"),
        "record_drops": summary.get("record_drops"),
        "summary_valid": summary.get("valid") is True,
        "index_valid": row.get("valid") is True,
        "thermal_valid": row.get("thermal_valid") is True,
        "gpu_start_temperature_c": row.get("gpu_start_temperature_c"),
        "campaign_error": campaign_error,
        "cancelled_requests": summary.get("cancelled_requests"),
        "cancel_to_global_resource_quiescence_ms": summary.get(
            "cancel_to_global_resource_quiescence_ms"),
        "global_resource_quiescence_reached": summary.get("global_resource_quiescence_reached"),
    }
    for name in ("interactive", "batch"):
        group = classes.get(name, {})
        result[f"{name}_successful"] = group.get("successful_requests", 0)
        result[f"{name}_ttft_p50_ms"] = group.get("ttft_ms", {}).get("p50")
        result[f"{name}_ttft_p90_ms"] = group.get("ttft_ms", {}).get("p90")
        result[f"{name}_ttft_p95_ms"] = group.get("ttft_ms", {}).get("p95")
        result[f"{name}_ttft_p99_ms"] = group.get("ttft_ms", {}).get("p99")
        result[f"{name}_e2e_p99_ms"] = group.get("e2e_ms", {}).get("p99")
        result[f"{name}_piece_interarrival_p99_ms"] = group.get(
            "piece_interarrival_ms", {}).get("p99")
        result[f"{name}_goodput_requests_per_s"] = group.get("goodput_requests_per_s")
        result[f"{name}_sample_count"] = group.get("sample_count", 0)
        class_metrics = deltas.get(name, {})
        result[f"{name}_ready_wait_p99_upper_bound_ms"] = class_metrics.get(
            "scheduler_wait_p99_upper_bound_ms")
        result[f"{name}_ready_wait_observations"] = class_metrics.get(
            "scheduler_wait_observations")
        result[f"{name}_max_ready_wait_ms"] = class_metrics.get("maximum_ready_wait_ms_after")
        result[f"{name}_starvation_events"] = class_metrics.get("starvation_events")
        result[f"{name}_rejections"] = class_metrics.get("rejected")
        result[f"{name}_resource_deferred"] = class_metrics.get("resource_deferred")
        result[f"{name}_service_tokens"] = class_metrics.get("service_tokens")
    success = int(result.get("interactive_successful") or 0) + int(result.get("batch_successful") or 0)
    tokens = {key: int(result[key] or 0) for key in
              ("interactive_service_tokens", "batch_service_tokens")}
    total_service_tokens = sum(tokens.values())
    result["interactive_service_share"] = (
        tokens["interactive_service_tokens"] / total_service_tokens
        if total_service_tokens else None)
    result["batch_service_share"] = (
        tokens["batch_service_tokens"] / total_service_tokens
        if total_service_tokens else None)
    result["completed_success_fraction"] = (
        success / result["requests_offered"] if result["requests_offered"] else None)
    start_temperature = result["gpu_start_temperature_c"]
    start_temperature_valid = (
        max_start_temperature_c is None or
        (isinstance(start_temperature, (int, float)) and
         start_temperature <= max_start_temperature_c))
    result["valid"] = (result["summary_valid"] and result["index_valid"] and
                       result["thermal_valid"] and start_temperature_valid and
                       campaign_error is None)
    return result


def load_trials(index_paths: list[Path]) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    trials: list[dict[str, Any]] = []
    manifests: dict[str, dict[str, Any]] = {}
    for index_path in index_paths:
        rows = read_lines(index_path)
        manifest_path = index_path.parent / "manifest.json"
        manifest = read_json(manifest_path) if manifest_path.exists() else {}
        error_path = index_path.parent / "campaign-error.txt"
        campaign_error = error_path.read_text(encoding="utf-8").strip() if error_path.exists() else None
        load = manifest.get("load", {})
        policies = manifest.get("policies", {})
        rates = load.get("offered_rates_rps", [])
        trial_count = load.get("trial_count")
        if rates and policies and isinstance(trial_count, int):
            expected_keys = {(trial, rate, treatment)
                             for rate in rates
                             for trial in range(1, trial_count + 1)
                             for treatment in policies}
            indexed_keys = {(row.get("trial"), row.get("offered_rate_rps"), row.get("treatment"))
                            for row in rows}
            if len(rows) != len(expected_keys) or indexed_keys != expected_keys:
                campaign_error = campaign_error or (
                    f"incomplete or duplicate run index: expected {len(expected_keys)} keys, "
                    f"observed {len(rows)} rows and {len(indexed_keys)} unique keys")
        campaign_id = str(manifest.get("campaign_id", index_path.parent.name))
        manifest["campaign_error"] = campaign_error
        manifests[campaign_id] = manifest
        validity = manifest.get("validity", {})
        max_start_temperature = validity.get("max_start_temperature_c")
        for row in rows:
            summary_path = Path(row["summary"])
            if not summary_path.is_absolute():
                summary_path = Path.cwd() / summary_path
            summary = read_json(summary_path)
            enriched = metric_fields(summary, row, campaign_error=campaign_error,
                                     max_start_temperature_c=max_start_temperature)
            resolved = row.get("resolved_configuration") or manifest.get("resolved_configuration", {})
            chunk = resolved.get("chunk_tokens", manifest.get("load", {}).get(
                "prefill_chunk_tokens", 512))
            reserve = resolved.get("generation_reserve_tokens", manifest.get("load", {}).get(
                "generation_reserve_tokens", 64))
            enriched["source_treatment"] = row.get("treatment")
            if chunk != 512:
                enriched["treatment"] = f"{row.get('treatment')}_chunk{chunk}"
            if reserve != 64:
                enriched["treatment"] += f"_reserve{reserve}"
            enriched["prefill_chunk_tokens"] = chunk
            enriched["generation_reserve_tokens"] = reserve
            enriched.update({"campaign_id": campaign_id,
                             "campaign_manifest": str(manifest_path),
                             "summary_path": str(summary_path),
                             "records_path": row.get("records"),
                             "metrics_path": row.get("server_metrics"),
                             "gpu_samples_path": row.get("gpu_samples"),
                             "run_index_path": str(index_path),
                             "treatment_order": row.get("treatment_order"),
                             "run_row": row})
            trials.append(enriched)
    if not trials and not manifests:
        raise ValueError("no trial records found")
    return trials, manifests


def group_key(row: dict[str, Any]) -> tuple[str, float | None]:
    return str(row.get("workload")), row.get("offered_rate_rps")


def paired_keys(row: dict[str, Any]) -> tuple[Any, ...]:
    return (row.get("workload"), row.get("offered_rate_rps"),
            row.get("trial"), row.get("seed"))


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    columns: list[str] = []
    for row in rows:
        for key in row:
            if key not in columns and not isinstance(row[key], (dict, list)):
                columns.append(key)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def summary_and_intervals(trials: list[dict[str, Any]], output: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    valid_trials = [row for row in trials if row.get("valid") is True]
    metric_names = ["requests_per_s", "output_tokens_per_s", "goodput_requests_per_s",
                    "interactive_ttft_p99_ms", "interactive_e2e_p99_ms",
                    "batch_ttft_p99_ms", "interactive_ready_wait_p99_upper_bound_ms",
                    "batch_ready_wait_p99_upper_bound_ms", "interactive_service_share",
                    "batch_service_share", "interactive_rejections", "batch_rejections",
                    "max_send_lateness_ms"]
    summary_rows: list[dict[str, Any]] = []
    for key in sorted(set(group_key(row) for row in valid_trials)):
        for treatment in sorted({row["treatment"] for row in valid_trials if group_key(row) == key}):
            entries = [row for row in valid_trials if group_key(row) == key and
                       row["treatment"] == treatment]
            summary_row: dict[str, Any] = {"workload": key[0], "offered_rate_rps": key[1],
                                           "treatment": treatment,
                                           "independent_trials": len(entries),
                                           "pooled_requests_offered": sum(
                                               int(entry.get("requests_offered") or 0) for entry in entries),
                                           "successful_observations": sum(
                                               int(entry.get("requests_succeeded") or 0) for entry in entries)}
            for metric in metric_names:
                values = [float(entry[metric]) for entry in entries if entry.get(metric) is not None]
                summary_row[f"median_{metric}"] = statistics.median(values) if values else None
                summary_row[f"min_trial_{metric}"] = min(values) if values else None
                summary_row[f"max_trial_{metric}"] = max(values) if values else None
            summary_rows.append(summary_row)

    paired_rows: list[dict[str, Any]] = []
    groups: dict[tuple[str, float | None], dict[tuple[Any, ...], dict[str, dict[str, Any]]]] = defaultdict(dict)
    for row in valid_trials:
        pair = groups[group_key(row)].setdefault(paired_keys(row), {})
        if row["treatment"] in pair:
            raise ValueError(f"duplicate paired trial for {group_key(row)} {paired_keys(row)} {row['treatment']}")
        pair[row["treatment"]] = row
    outcomes = ["interactive_ttft_p99_ms", "goodput_requests_per_s", "requests_per_s",
                "output_tokens_per_s", "interactive_service_share",
                "interactive_ready_wait_p99_upper_bound_ms"]
    for load_group, paired in sorted(groups.items()):
        treatment_names = {treatment for pair in paired.values() for treatment in pair}
        comparisons = {("sawds_static", "fifo"), ("sawds_adaptive", "fifo")}
        for treatment in treatment_names:
            if treatment.endswith("_chunk2048"):
                baseline = treatment.removesuffix("_chunk2048")
                if baseline in treatment_names:
                    comparisons.add((treatment, baseline))
            if "_reserve" in treatment:
                baseline = treatment.rsplit("_reserve", 1)[0]
                if baseline in treatment_names:
                    comparisons.add((treatment, baseline))
        for treatment, baseline_name in sorted(comparisons):
            effects: dict[str, list[float]] = defaultdict(list)
            paired_count = 0
            for pair in paired.values():
                base, variant = pair.get(baseline_name), pair.get(treatment)
                if base is None or variant is None:
                    continue
                paired_count += 1
                for outcome in outcomes:
                    left, right = base.get(outcome), variant.get(outcome)
                    if left is not None and right is not None:
                        effects[outcome].append(float(right) - float(left))
            for outcome, values in effects.items():
                low, high = (bootstrap_median_interval(values,
                    90_000 + sum(map(ord, f"{load_group}-{treatment}-{outcome}")))
                    if len(values) >= 5 else (None, None))
                paired_rows.append({
                    "workload": load_group[0], "offered_rate_rps": load_group[1],
                    "comparison": f"{treatment} - {baseline_name}", "metric": outcome,
                    "independent_paired_trials": len(values), "paired_trial_keys": paired_count,
                    "median_paired_effect": statistics.median(values),
                    "bootstrap_95pct_low": low, "bootstrap_95pct_high": high,
                    "status": "estimated" if len(values) >= 5 else "insufficient_pairs",
                    "bootstrap_unit": "paired trial; no request-level resampling",
                    "bootstrap_repetitions": 20_000,
                    "direction_note": "negative is lower for latency/wait; positive is higher for throughput/goodput",
                    "trial_effects": json.dumps(values),
                })
    write_csv(output / "trial-summary.csv", summary_rows)
    write_csv(output / "confidence-intervals.csv", paired_rows)
    write_csv(output / "per-trial.csv", trials)
    return summary_rows, paired_rows


COLORS = {"fifo": "#176b87", "sawds_static": "#d46a38", "sawds_adaptive": "#7251a2",
          "chunk512": "#176b87", "chunk2048": "#d46a38", "baseline": "#555555"}


def svg_frame(title: str, subtitle: str, x_label: str, y_label: str,
              width: int = 960, height: int = 560) -> tuple[list[str], tuple[int, int, int, int]]:
    left, top, right, bottom = 90, 76, width - 35, height - 85
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#ffffff"/>',
             f'<text x="{left}" y="34" font-family="sans-serif" font-size="22" font-weight="700">{title}</text>',
             f'<text x="{left}" y="57" font-family="sans-serif" font-size="12" fill="#555">{subtitle}</text>',
             f'<line x1="{left}" y1="{bottom}" x2="{right}" y2="{bottom}" stroke="#222"/>',
             f'<line x1="{left}" y1="{top}" x2="{left}" y2="{bottom}" stroke="#222"/>',
             f'<text x="{(left+right)//2}" y="{height-35}" text-anchor="middle" font-family="sans-serif" font-size="14">{x_label}</text>',
             f'<text x="22" y="{(top+bottom)//2}" transform="rotate(-90 22 {(top+bottom)//2})" text-anchor="middle" font-family="sans-serif" font-size="14">{y_label}</text>']
    return lines, (left, top, right, bottom)


def line_figure(path: Path, title: str, subtitle: str, x_label: str, y_label: str,
                series: dict[str, list[tuple[float, float, float | tuple[float, float] | None]]],
                source_ids: list[str], *, cdf: bool = False) -> None:
    lines, (left, top, right, bottom) = svg_frame(title, subtitle, x_label, y_label)
    all_points = [point for values in series.values() for point in values]
    if not all_points:
        lines.append(f'<text x="{left+25}" y="{top+35}" font-family="sans-serif" font-size="18">NOT RUN: no matching indexed evidence</text>')
    else:
        xmin = min(item[0] for item in all_points)
        xmax = max(item[0] for item in all_points)
        ymin = 0.0 if not cdf else 0.0
        all_y = [item[1] for item in all_points]
        for _, value, bounds in all_points:
            if isinstance(bounds, tuple):
                all_y.extend(bounds)
            elif bounds is not None:
                all_y.extend((value - bounds, value + bounds))
        ymax = max(all_y) or 1.0
        if ymax <= ymin:
            ymax = ymin + 1.0
        ymax *= 1.08
        if xmax <= xmin:
            xmax = xmin + 1.0
        for step in range(6):
            frac = step / 5
            y = bottom - frac * (bottom - top)
            val = ymin + frac * (ymax - ymin)
            lines.append(f'<line x1="{left}" y1="{y:.1f}" x2="{right}" y2="{y:.1f}" stroke="#e5e5e5"/>')
            lines.append(f'<text x="{left-9}" y="{y+4:.1f}" text-anchor="end" font-family="sans-serif" font-size="11">{val:.2f}</text>')
        for name, values in sorted(series.items()):
            values = sorted(values)
            color = COLORS.get(name, "#444444")
            coords: list[tuple[float, float]] = []
            for x, y, spread in values:
                px = left + (x - xmin) / (xmax - xmin) * (right - left)
                py = bottom - (y - ymin) / (ymax - ymin) * (bottom - top)
                coords.append((px, py))
                if spread is not None and not cdf:
                    low, high = spread if isinstance(spread, tuple) else (y - spread, y + spread)
                    low_y = bottom - (max(ymin, low) - ymin) / (ymax - ymin) * (bottom - top)
                    high_y = bottom - (min(ymax, high) - ymin) / (ymax - ymin) * (bottom - top)
                    lines.append(f'<line x1="{px:.1f}" y1="{low_y:.1f}" x2="{px:.1f}" y2="{high_y:.1f}" stroke="{color}" stroke-width="2"/>')
                lines.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="4" fill="{color}"/>')
            if len(coords) > 1:
                points = " ".join(f"{x:.1f},{y:.1f}" for x, y in coords)
                lines.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.5"/>')
            legend_y = top + 20 + 20 * list(sorted(series)).index(name)
            lines.append(f'<line x1="{right-150}" y1="{legend_y}" x2="{right-125}" y2="{legend_y}" stroke="{color}" stroke-width="3"/>')
            lines.append(f'<text x="{right-118}" y="{legend_y+4}" font-family="sans-serif" font-size="12">{name}</text>')
        if xmin < xmax:
            for step in range(5):
                frac = step / 4
                x = left + frac * (right - left)
                value = xmin + frac * (xmax - xmin)
                lines.append(f'<text x="{x:.1f}" y="{bottom+20}" text-anchor="middle" font-family="sans-serif" font-size="11">{value:.2f}</text>')
    unique_sources = sorted(set(source_ids))
    lines.append(f'<text x="{left}" y="552" font-family="monospace" font-size="8" fill="#666">indexed runs: {", ".join(unique_sources[:24])}</text>')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines + ["</svg>", ""]), encoding="utf-8")


def write_text_svg(path: Path, title: str, paragraphs: list[str], source: str) -> None:
    lines = ['<svg xmlns="http://www.w3.org/2000/svg" width="960" height="560" viewBox="0 0 960 560">',
             '<rect width="100%" height="100%" fill="#fff"/>',
             f'<text x="55" y="45" font-family="sans-serif" font-size="23" font-weight="700">{title}</text>']
    for i, paragraph in enumerate(paragraphs):
        lines.append(f'<text x="55" y="{100+i*45}" font-family="sans-serif" font-size="17">{paragraph}</text>')
    lines.append(f'<text x="55" y="535" font-family="monospace" font-size="10" fill="#666">source: {source}</text>')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines + ["</svg>", ""]), encoding="utf-8")


def run_paths(row: dict[str, Any], cwd: Path) -> tuple[Path | None, Path | None]:
    records = Path(row["records_path"]) if row.get("records_path") else None
    metrics = Path(row["metrics_path"]) if row.get("metrics_path") else None
    if records and not records.is_absolute(): records = cwd / records
    if metrics and not metrics.is_absolute(): metrics = cwd / metrics
    return records, metrics


def generate_figures(trials: list[dict[str, Any]], manifests: dict[str, dict[str, Any]],
                     output: Path, cwd: Path) -> None:
    figures = output / "figures"
    valid_trials = [row for row in trials if row.get("valid") is True]
    ids = [str(row.get("run_id")) for row in valid_trials]
    for filename, metric, title, ylabel in [
        ("throughput-vs-load.svg", "requests_per_s", "Successful throughput vs offered load", "Successful requests/s"),
        ("slo-goodput-vs-load.svg", "goodput_requests_per_s", "SLO goodput vs offered load", "SLO-meeting requests/s"),
        ("p99-ttft-vs-load.svg", "interactive_ttft_p99_ms", "Interactive client TTFT p99 vs offered load", "TTFT p99 (ms)"),
    ]:
        curve_trials = [row for row in valid_trials if row.get("workload") == "phase5_loadcurve"]
        if not curve_trials:
            curve_trials = [row for row in valid_trials if row.get("workload") == "phase5_interactive"]
        curves: dict[str, list[tuple[float, float, float | tuple[float, float] | None]]] = defaultdict(list)
        for workload_rate in sorted(set(group_key(row) for row in curve_trials)):
            for treatment in sorted({row["treatment"] for row in curve_trials
                                     if group_key(row) == workload_rate}):
                values = [float(row[metric]) for row in curve_trials if
                          group_key(row) == workload_rate and row["treatment"] == treatment and
                          row.get(metric) is not None]
                if values and workload_rate[1] is not None:
                    curves[treatment].append((float(workload_rate[1]), statistics.median(values),
                                              (min(values), max(values))))
        line_figure(figures / filename, title,
                    "Dots are medians of per-trial values; whiskers show the min-to-max trial range, not a confidence interval.",
                    "Offered requests/s", ylabel, curves, ids)

    eligible = [row for row in trials if row.get("valid") is True and
                row.get("workload") == "phase5_interactive"]
    if eligible:
        selected_group = max(row["offered_rate_rps"] for row in eligible)
        cdf_series: dict[str, list[tuple[float, float, float | None]]] = {}
        cdf_ids: list[str] = []
        for treatment in sorted({row["treatment"] for row in eligible
                                 if row["offered_rate_rps"] == selected_group}):
            values: list[float] = []
            for row in eligible:
                if row["treatment"] != treatment or row["offered_rate_rps"] != selected_group:
                    continue
                records, _ = run_paths(row, cwd)
                if records and records.exists():
                    values.extend((record["first_token_ns"] - record["intended_arrival_ns"]) / 1e6
                                  for record in read_lines(records)
                                  if record.get("priority_class") == "interactive" and
                                  record.get("status") == "ok" and record.get("first_token_ns") is not None)
                    cdf_ids.append(str(row.get("run_id")))
            values.sort()
            step = max(1, len(values) // 1500)
            sampled = values[::step]
            cdf_series[treatment] = [(x, min(1.0, (i * step + 1) / len(values)), None)
                                     for i, x in enumerate(sampled) if values]
        line_figure(figures / "ttft-cdf.svg", "Interactive TTFT CDF near selected load",
                    f"Pooled successful request samples at {selected_group:g} offered requests/s; descriptive only, not an independent-trial interval.",
                    "Client TTFT from intended arrival (ms)", "Cumulative fraction",
                    cdf_series, cdf_ids, cdf=True)
    else:
        write_text_svg(figures / "ttft-cdf.svg", "Interactive TTFT CDF",
                       ["NOT RUN: no phase5_interactive trials were indexed."], "release run index")

    chunk_groups: dict[str, dict[int, float]] = defaultdict(dict)
    chunk_ids: list[str] = []
    for row in trials:
        if row.get("valid") is not True:
            continue
        if row.get("workload") != "phase5_mixed" or row.get("source_treatment") != "sawds_static":
            continue
        manifest = manifests.get(str(row.get("campaign_id")), {})
        resolved = row.get("resolved_configuration") or manifest.get("resolved_configuration", {})
        chunk = resolved.get("chunk_tokens", manifest.get("load", {}).get("prefill_chunk_tokens"))
        if chunk in (512, 2048) and row.get("interactive_ttft_p99_ms") is not None:
            chunk_groups[f"chunk{chunk}"][int(row.get("seed") or row.get("trial") or 0)] = float(
                row["interactive_ttft_p99_ms"])
            chunk_ids.append(str(row.get("run_id")))
    paired_seeds = sorted({seed for values in chunk_groups.values() for seed in values})
    chunk_points = {name: [(float(index + 1), values[seed], None)
                           for index, seed in enumerate(paired_seeds) if seed in values]
                    for name, values in chunk_groups.items()}
    line_figure(figures / "long-prefill-interference.svg", "Interactive TTFT under long-prefill contention",
                "SA-WDS static; points align by paired seed when both chunk settings are present.",
                "Paired trial index", "Interactive TTFT p99 (ms)", chunk_points, chunk_ids)

    fair_series: dict[str, list[tuple[float, float, float | None]]] = defaultdict(list)
    fair_ids: list[str] = []
    for row in trials:
        if row.get("valid") is not True:
            continue
        if row.get("workload") != "phase5_mixed":
            continue
        for order, cls in enumerate(("interactive", "batch"), 1):
            value = row.get(f"{cls}_ready_wait_p99_upper_bound_ms")
            if value is not None:
                fair_series[f"{row['treatment']}:{cls}"].append((float(order), float(value), None))
                fair_ids.append(str(row.get("run_id")))
    line_figure(figures / "fairness-wait.svg", "Class scheduler-ready wait",
                "Values are bucketed upper bounds over issued work-item waits; service-token share is in trial-summary.csv.",
                "Class position (1 interactive, 2 batch)", "p99 ready wait upper bound (ms)",
                fair_series, fair_ids)

    timelines: list[tuple[dict[str, Any], list[dict[str, Any]]]] = []
    for row in trials:
        if row.get("valid") is not True:
            continue
        _, metrics_path = run_paths(row, cwd)
        if metrics_path and metrics_path.exists():
            samples = read_lines(metrics_path)
            if samples:
                timelines.append((row, samples))
    pressure_candidate = max(timelines, key=lambda pair: sum(
        sample.get("kv", {}).get("pressure") in ("soft", "high", "hard")
        for sample in pair[1]), default=None)
    if pressure_candidate:
        row, samples = pressure_candidate
        resource_series = {
            "committed": [(float(sample.get("offset_ms", 0)),
                           float(sample.get("kv", {}).get("committed_tokens", 0)), None)
                           for sample in samples if "kv" in sample],
            "soft reserved": [(float(sample.get("offset_ms", 0)),
                                float(sample.get("kv", {}).get("soft_reserved_tokens", 0)), None)
                               for sample in samples if "kv" in sample],
        }
        line_figure(figures / "resource-pressure-timeline.svg", "Logical KV pressure timeline",
                    f"Run {row.get('run_id')}; pressure state is shown separately; admissions/rejections are in resource-pressure-events.csv.",
                    "Run time (ms)", "Logical sequence-token positions", resource_series,
                    [str(row.get("run_id"))])
        pressure_levels = {"normal": 0.0, "soft": 1.0, "high": 2.0, "hard": 3.0}
        pressure_series = {"pressure state": [
            (float(sample.get("offset_ms", 0)),
             pressure_levels.get(str(sample.get("kv", {}).get("pressure", "normal")), 0.0), None)
            for sample in samples if "kv" in sample]}
        line_figure(figures / "resource-pressure-state.svg", "Logical KV pressure state",
                    f"Run {row.get('run_id')}; event transitions are in resource-pressure-events.csv.",
                    "Run time (ms)", "State (0 normal, 1 soft, 2 high, 3 hard)",
                    pressure_series, [str(row.get("run_id"))])
        event_rows: list[dict[str, Any]] = []
        previous_pressure = None
        previous_admitted = None
        previous_rejected = None
        for sample in samples:
            kv = sample.get("kv", {})
            scheduler = sample.get("scheduler", {})
            classes = (scheduler.get("interactive", {}), scheduler.get("batch", {}))
            admitted = sum(int(value.get("admitted", 0)) for value in classes)
            rejected = sum(int(value.get("rejected", 0)) for value in classes)
            pressure = kv.get("pressure")
            events = []
            if pressure != previous_pressure:
                events.append("pressure_transition")
            if previous_admitted is not None and admitted > previous_admitted:
                events.append("admission")
            if previous_rejected is not None and rejected > previous_rejected:
                events.append("rejection")
            if events:
                event_rows.append({"run_id": row.get("run_id"),
                                   "offset_ms": sample.get("offset_ms"),
                                   "pressure": pressure,
                                   "committed_tokens": kv.get("committed_tokens"),
                                   "soft_reserved_tokens": kv.get("soft_reserved_tokens"),
                                   "authorized_tokens": kv.get("authorized_tokens"),
                                   "admitted_total": admitted, "rejected_total": rejected,
                                   "event": ";".join(events)})
            previous_pressure = pressure
            previous_admitted = admitted
            previous_rejected = rejected
        write_csv(output / "resource-pressure-events.csv", event_rows)
    else:
        write_text_svg(figures / "resource-pressure-timeline.svg", "Logical KV pressure timeline",
                       ["NOT RUN: no indexed server metrics timeline."], "release run index")
        write_text_svg(figures / "resource-pressure-state.svg", "Logical KV pressure state",
                       ["NOT RUN: no indexed server metrics timeline."], "release run index")
        write_csv(output / "resource-pressure-events.csv", [])

    adaptive_budget_series: dict[str, list[tuple[float, float, float | None]]] = defaultdict(list)
    adaptive_latency_series: dict[str, list[tuple[float, float, float | None]]] = defaultdict(list)
    adaptive_ids: list[str] = []
    for row, samples in timelines:
        if row.get("treatment") != "sawds_adaptive":
            continue
        for sample in samples:
            for event in sample.get("controller_events", []):
                x = float(sample.get("offset_ms", 0))
                adaptive_budget_series["prefill budget"].append(
                    (x, float(event.get("budget_after", 0)), None))
                adaptive_latency_series["interactive TTFT EWMA"].append((
                    x, float(event.get("interactive_ttft_ewma_ms", 0)), None))
                adaptive_ids.append(str(row.get("run_id")))
    line_figure(figures / "controller-timeline.svg", "Adaptive prefill budget",
                "Trace samples carry pressure signals, reason, and decode backlog in server-metrics.jsonl.",
                "Run time (ms)", "Prefill budget (tokens)", adaptive_budget_series, adaptive_ids)
    line_figure(figures / "controller-interactive-ewma.svg", "Interactive TTFT feedback signal",
                "Interactive TTFT EWMA from adaptive controller events; source runs listed below.",
                "Run time (ms)", "Interactive TTFT EWMA (ms)",
                adaptive_latency_series, adaptive_ids)

    cancel_series: dict[str, list[tuple[float, float, float | None]]] = defaultdict(list)
    cancel_ids: list[str] = []
    for row in trials:
        if row.get("valid") is not True:
            continue
        if not (row.get("cancelled_requests") or 0):
            continue
        summary = read_json(Path(row["summary_path"]))
        stop_p99 = summary.get("cancel_to_client_stop_ms", {}).get("p99")
        release = row.get("cancel_to_global_resource_quiescence_ms")
        trial_id = float(row.get("trial") or len(cancel_ids) + 1)
        if stop_p99 is not None:
            cancel_series["client stop after cancellation deadline"].append((trial_id, float(stop_p99), None))
        if release is not None:
            cancel_series["last cancel to global resource quiescence"].append((trial_id, float(release), None))
        cancel_ids.append(str(row.get("run_id")))
    line_figure(figures / "cancellation-latency.svg", "Cancellation latency evidence",
                "First is client-observed close latency; second is last requested cancel to global logical resource quiescence, not per-request release.",
                "Trial", "Latency (ms)", cancel_series, cancel_ids)

    write_text_svg(figures / "profiler-summary.svg", "Profiler result: backend execution dominates",
                   ["1000-batch C4 Nsight Systems sample: batch-plan build 4.284 ms total.",
                    "Backend execution ranges: 11.878 s total; builder upper bound 0.036%.",
                    "CUDA sync/launch costs were visible; no retained custom CUDA optimization.",
                    "Nsight projection is overlap-aware; API durations are not additive wall time."],
                   "docs/profiling/PROFILE_PHASE4.md and OPTIMIZATION_EXPERIMENT.md")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", action="append", type=Path, required=True,
                        help="run-index.jsonl; repeat for campaigns/ablations")
    parser.add_argument("--output", type=Path, default=Path("artifacts/release-v1/benchmark"))
    args = parser.parse_args()
    output = args.output.resolve()
    trials, manifests = load_trials([path.resolve() for path in args.index])
    _, intervals = summary_and_intervals(trials, output)
    generate_figures(trials, manifests, output, Path.cwd())
    summary = {"indexed_trials": len(trials), "campaigns": sorted(manifests),
               "valid_runs": sum(row.get("valid") is True for row in trials),
               "invalid_runs": sum(row.get("valid") is not True for row in trials),
               "campaign_errors": {campaign: manifest.get("campaign_error")
                                   for campaign, manifest in manifests.items()
                                   if manifest.get("campaign_error")},
               "campaign_error_count": sum(bool(manifest.get("campaign_error"))
                                            for manifest in manifests.values()),
               "paired_interval_rows": len(intervals),
               "trial_unit": "campaign/trial/paired seed",
               "bootstrap": "20,000 percentile resamples of paired trial effects; not request-level"}
    (output / "analysis-summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                                 encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if (summary["valid_runs"] == len(trials) and
                 summary["campaign_error_count"] == 0) else 2


if __name__ == "__main__":
    raise SystemExit(main())
