"""Small dependency-free statistics used by the benchmark recorder."""

from __future__ import annotations

from math import isclose, isfinite
from typing import Iterable, Mapping, Any


def percentile(values: Iterable[float], p: float) -> float | None:
    if not 0.0 <= p <= 1.0:
        raise ValueError("p must be in [0, 1]")
    data = sorted(float(value) for value in values if isfinite(float(value)))
    if not data:
        return None
    position = (len(data) - 1) * p
    lower = int(position)
    upper = min(lower + 1, len(data) - 1)
    fraction = position - lower
    return data[lower] + (data[upper] - data[lower]) * fraction


def latency_percentiles(values_ms: Iterable[float]) -> dict[str, float | None]:
    data = list(values_ms)
    return {f"p{int(p * 100)}": percentile(data, p) for p in (0.50, 0.90, 0.95, 0.99)}


def _summarize_group(records: list[Mapping[str, Any]], elapsed_s: float,
                     max_send_lateness_ms: float) -> dict[str, Any]:
    successful = [r for r in records if r.get("status") == "ok"]
    ttft_ms = [
        (r["first_token_ns"] - r["intended_arrival_ns"]) / 1e6
        for r in successful if r.get("first_token_ns") is not None
    ]
    e2e_ms = [
        (r["completion_ns"] - r["intended_arrival_ns"]) / 1e6
        for r in successful if r.get("completion_ns") is not None
    ]
    service_ttft_ms = [
        (r["first_token_ns"] - r["send_ns"]) / 1e6
        for r in successful if r.get("first_token_ns") is not None and r.get("send_ns") is not None
    ]
    service_e2e_ms = [
        (r["completion_ns"] - r["send_ns"]) / 1e6
        for r in successful if r.get("completion_ns") is not None and r.get("send_ns") is not None
    ]
    piece_interarrival_ms = [
        (b - a) / 1e6
        for r in successful
        for a, b in zip(r.get("token_arrivals_ns", ()), r.get("token_arrivals_ns", ())[1:])
    ]
    lateness_ms = [
        max(0, r["send_ns"] - r["intended_arrival_ns"]) / 1e6
        for r in records if r.get("send_ns") is not None
    ]
    slo_samples = [r for r in records
                   if r.get("ttft_slo_ms") is not None or r.get("e2e_slo_ms") is not None]
    exact_outputs = [r["actual_output_tokens"] for r in successful
                     if r.get("actual_output_tokens") is not None]
    exact_prompts = [r["actual_prompt_tokens"] for r in successful
                     if r.get("actual_prompt_tokens") is not None]
    slo_good = sum(r.get("status") == "ok" and bool(r.get("slo_met")) for r in slo_samples)
    cancelled = [r for r in records if r.get("cancelled") and
                 r.get("send_ns") is not None and r.get("completion_ns") is not None and
                 r.get("cancel_after_ms") is not None]
    cancel_to_stop = [
        max(0.0, (r["completion_ns"] -
                  (r["send_ns"] + float(r["cancel_after_ms"]) * 1e6)) / 1e6)
        for r in cancelled
    ]
    actual_count = sum(exact_outputs)
    output_usage_complete = bool(successful) and len(exact_outputs) == len(successful)
    return {
        "sample_count": len(records),
        "successful_requests": len(successful),
        "elapsed_s": elapsed_s,
        "ttft_ms": latency_percentiles(ttft_ms),
        "e2e_ms": latency_percentiles(e2e_ms),
        "service_ttft_ms": latency_percentiles(service_ttft_ms),
        "service_e2e_ms": latency_percentiles(service_e2e_ms),
        "piece_interarrival_ms": latency_percentiles(piece_interarrival_ms),
        "arrival_lateness_ms": latency_percentiles(lateness_ms),
        "requests_per_s": len(successful) / elapsed_s if elapsed_s > 0 else None,
        "output_tokens_per_s": actual_count / elapsed_s
            if elapsed_s > 0 and output_usage_complete else None,
        "output_token_observations": len(exact_outputs),
        "output_token_coverage": len(exact_outputs) / len(successful) if successful else None,
        "prompt_token_observations": len(exact_prompts),
        "processed_tokens_per_s": (sum(exact_outputs) + sum(exact_prompts)) / elapsed_s
            if elapsed_s > 0 and len(exact_outputs) == len(successful) and
            len(exact_prompts) == len(successful) else None,
        "slo_samples": len(slo_samples),
        "slo_attainment": slo_good / len(slo_samples) if slo_samples else None,
        "goodput_requests_per_s": slo_good / elapsed_s if elapsed_s > 0 else None,
        "cancelled_requests": len(cancelled),
        "cancel_to_client_stop_ms": latency_percentiles(cancel_to_stop),
        "max_send_lateness_ms": max(lateness_ms, default=0.0),
        "valid": max(lateness_ms, default=0.0) <= max_send_lateness_ms,
        "statuses": {status: sum(r.get("status") == status for r in records)
                     for status in sorted({str(r.get("status")) for r in records})},
        "error_classes": {error: sum(r.get("error_class") == error for r in records)
                          for error in sorted({str(r.get("error_class")) for r in records
                                               if r.get("error_class") is not None})},
    }


def summarize(records: list[Mapping[str, Any]], elapsed_s: float,
              max_send_lateness_ms: float) -> dict[str, Any]:
    summary = _summarize_group(records, elapsed_s, max_send_lateness_ms)
    classes = sorted({str(record.get("priority_class", "unspecified")) for record in records})
    summary["by_class"] = {
        service_class: _summarize_group(
            [record for record in records
             if str(record.get("priority_class", "unspecified")) == service_class],
            elapsed_s, max_send_lateness_ms)
        for service_class in classes
    }
    return summary


def self_check() -> None:
    assert percentile([], 0.5) is None
    assert percentile([1, 2, 3, 4], 0.5) == 2.5
    assert isclose(percentile([1, 2, 3, 4], 0.99) or 0.0, 3.97)
    assert percentile([float("nan"), 7], 0.5) == 7
    try:
        percentile([1], 1.1)
    except ValueError:
        pass
    else:
        raise AssertionError("out-of-range percentile did not fail")


if __name__ == "__main__":
    self_check()
    print("statistics self-check passed")
