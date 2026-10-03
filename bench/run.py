#!/usr/bin/env python3
"""Coordinated-omission-safe async load generator for OpenAI-compatible SSE APIs."""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import random
import time
import uuid
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit, urlunsplit

import httpx
import yaml

from stats import summarize


def metrics_url(completions_url: str) -> str:
    parsed = urlsplit(completions_url)
    suffix = "/v1/completions"
    if parsed.path.endswith(suffix):
        path = parsed.path[:-len(suffix)] + "/metrics"
    else:
        path = parsed.path.rsplit("/", 1)[0] + "/metrics"
    return urlunsplit((parsed.scheme, parsed.netloc, path or "/metrics", "", ""))


async def fetch_server_metrics(client: httpx.AsyncClient, url: str) -> tuple[dict[str, Any] | None, str | None]:
    try:
        response = await client.get(url, timeout=2.0)
        if response.status_code >= 400:
            return None, f"http_{response.status_code}"
        value = response.json()
        if not isinstance(value, dict):
            return None, "invalid_json_shape"
        return value, None
    except (httpx.HTTPError, json.JSONDecodeError) as error:
        return None, type(error).__name__


def completed_metrics_reached(metrics: dict[str, Any] | None,
                              expected_completed: dict[str, int],
                              expected_responses: dict[str, int] | None = None,
                              baseline: dict[str, Any] | None = None) -> bool:
    if metrics is None:
        return False
    scheduler = metrics.get("scheduler")
    if not isinstance(scheduler, dict):
        return False
    for service_class, expected in expected_completed.items():
        class_metrics = scheduler.get(service_class)
        if not isinstance(class_metrics, dict):
            return False
        old_metrics = (baseline or {}).get("scheduler", {}).get(service_class, {})
        old_completed = int(old_metrics.get("completed", 0))
        if int(class_metrics.get("completed", -1)) - old_completed < expected:
            return False
        if expected_responses is not None:
            old_handled = int(old_metrics.get("admitted", 0)) + int(old_metrics.get("rejected", 0))
            handled = int(class_metrics.get("admitted", 0)) + int(class_metrics.get("rejected", 0))
            if handled - old_handled < expected_responses.get(service_class, 0):
                return False
    return True


async def fetch_final_server_metrics(
        client: httpx.AsyncClient, url: str,
        expected_completed: dict[str, int],
        expected_responses: dict[str, int] | None = None,
        baseline: dict[str, Any] | None = None, *, max_wait_ms: int = 250,
        poll_interval_ms: int = 10) -> tuple[dict[str, Any] | None, str | None, float, bool]:
    """Wait briefly for the runtime's periodically refreshed final metrics snapshot."""
    started = time.perf_counter()
    metrics, error = await fetch_server_metrics(client, url)
    if metrics is None:
        return metrics, error, (time.perf_counter() - started) * 1000, False
    reached = lambda value: completed_metrics_reached(
        value, expected_completed, expected_responses, baseline)
    while not reached(metrics):
        remaining = max_wait_ms / 1000 - (time.perf_counter() - started)
        if remaining <= 0:
            break
        await asyncio.sleep(min(poll_interval_ms / 1000, remaining))
        next_metrics, next_error = await fetch_server_metrics(client, url)
        if next_metrics is not None:
            metrics = next_metrics
            error = None
        elif next_error is not None:
            error = next_error
    stable = reached(metrics)
    return metrics, error, (time.perf_counter() - started) * 1000, stable


def server_metric_deltas(before: dict[str, Any] | None,
                         after: dict[str, Any] | None) -> dict[str, Any] | None:
    if before is None or after is None:
        return None
    before_classes = before.get("scheduler", {})
    after_classes = after.get("scheduler", {})
    fields = ("admitted", "rejected", "rejected_invalid", "rejected_kv_capacity",
              "rejected_sequence_capacity", "rejected_runtime_state", "resource_deferred",
              "resource_unblocked", "completed", "slo_met", "service_tokens",
              "starvation_events", "hard_limit_aborted")
    deltas: dict[str, Any] = {}
    for service_class in ("interactive", "batch"):
        old = before_classes.get(service_class, {})
        new = after_classes.get(service_class, {})
        deltas[service_class] = {
            field: max(0, int(new.get(field, 0)) - int(old.get(field, 0)))
            for field in fields
        }
        deltas[service_class]["scheduler_wait_p99_ms_after"] = int(
            new.get("scheduler_wait_p99_ms", 0))
    return deltas


def build_arrivals(config: dict[str, Any], count: int, rate: float, seed: int) -> list[float]:
    rng = random.Random(seed)
    mode = config["mode"]
    if mode == "closed_loop":
        return [0.0] * count
    if mode == "open_fixed":
        if rate <= 0:
            raise ValueError("open-loop rate must be positive")
        return [i / rate for i in range(count)]
    if mode == "open_poisson":
        if rate <= 0:
            raise ValueError("Poisson rate must be positive")
        arrivals: list[float] = []
        current = 0.0
        for _ in range(count):
            current += -math.log1p(-rng.random()) / rate
            arrivals.append(current)
        return arrivals
    if mode == "burst":
        arrivals = []
        for burst in config["bursts"]:
            at = float(burst["at_s"])
            spacing = float(burst.get("interval_ms", 0)) / 1000
            arrivals.extend(at + i * spacing for i in range(int(burst["count"])))
        return sorted(arrivals[:count])
    raise ValueError(f"unknown arrival mode: {mode}")


def choose_request(config: dict[str, Any], rng: random.Random) -> dict[str, Any]:
    classes = config["classes"]
    selected = rng.choices(classes, weights=[float(c.get("weight", 1)) for c in classes], k=1)[0]
    request = dict(selected)
    prompt = request["prompt_tokens"]
    output = request["output_tokens"]
    request["prompt_tokens"] = rng.randint(*prompt) if isinstance(prompt, list) else int(prompt)
    request["output_tokens"] = rng.randint(*output) if isinstance(output, list) else int(output)
    return request


class Recorder:
    def __init__(self, path: Path, capacity: int = 2048) -> None:
        self.path = path
        self.queue: asyncio.Queue[dict[str, Any] | None] = asyncio.Queue(maxsize=capacity)
        self.dropped = 0
        self.task: asyncio.Task[None] | None = None

    async def start(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.task = asyncio.create_task(self._write())

    def record(self, item: dict[str, Any]) -> None:
        try:
            self.queue.put_nowait(item)
        except asyncio.QueueFull:
            self.dropped += 1

    async def close(self) -> None:
        assert self.task is not None
        await self.queue.put(None)
        await self.task

    async def _write(self) -> None:
        with self.path.open("w", encoding="utf-8") as output:
            while True:
                item = await self.queue.get()
                if item is None:
                    break
                line = json.dumps(item, separators=(",", ":"), ensure_ascii=False) + "\n"
                await asyncio.to_thread(output.write, line)


async def read_sse(response: httpx.Response, slow_reader_ms: int) -> tuple[int | None, int | None, int | None, list[int], bool, bool, bool]:
    first_token_ns: int | None = None
    actual_prompt: int | None = None
    actual_output: int | None = None
    token_arrivals: list[int] = []
    saw_data = False
    saw_done = False
    saw_error = False
    async for line in response.aiter_lines():
        if slow_reader_ms:
            await asyncio.sleep(slow_reader_ms / 1000)
        if not line.startswith("data:"):
            continue
        data = line[5:].strip()
        if data == "[DONE]":
            saw_done = True
            continue
        try:
            event = json.loads(data)
        except json.JSONDecodeError as error:
            raise ValueError("malformed SSE JSON data") from error
        if not isinstance(event, dict):
            raise ValueError("SSE event must contain a JSON object")
        if event.get("error") is not None:
            saw_error = True
        usage = event.get("usage")
        if isinstance(usage, dict):
            if isinstance(usage.get("prompt_tokens"), int):
                actual_prompt = usage["prompt_tokens"]
            if isinstance(usage.get("completion_tokens"), int):
                actual_output = usage["completion_tokens"]
        choices = event.get("choices", [])
        if not isinstance(choices, list):
            raise ValueError("SSE choices must be an array")
        if choices:
            choice = choices[0]
            if not isinstance(choice, dict):
                raise ValueError("SSE choice must be an object")
            piece = choice.get("text")
            if piece is None and isinstance(choice.get("delta"), dict):
                piece = choice["delta"].get("content")
            if piece:
                now = time.perf_counter_ns()
                first_token_ns = first_token_ns or now
                token_arrivals.append(now)
                saw_data = True
    return first_token_ns, actual_prompt, actual_output, token_arrivals, saw_data, saw_done, saw_error


async def run(args: argparse.Namespace) -> Path:
    config = yaml.safe_load(Path(args.workload).read_text(encoding="utf-8"))
    arrival = config["arrival"]
    count = args.requests or int(config.get("requests", 100))
    mode = args.mode or arrival["mode"]
    arrival = dict(arrival, mode=mode)
    rate = args.rate if args.rate is not None else float(arrival.get("rate_rps", 1.0))
    concurrency = args.concurrency or int(arrival.get("concurrency", 1))
    if count <= 0 or concurrency <= 0:
        raise ValueError("requests and concurrency must be positive")
    seed = args.seed if args.seed is not None else int(config.get("seed", 1))
    schedules = build_arrivals(arrival, count, rate, seed)
    if len(schedules) != count:
        count = len(schedules)
    run_id = str(uuid.uuid4())
    run_dir = Path(args.output) / run_id
    raw_path = run_dir / "requests.jsonl"
    recorder = Recorder(raw_path)
    await recorder.start()
    rng = random.Random(seed)
    requests = [choose_request(config, rng) for _ in range(count)]
    for request in requests:
        if request.get("cancel_after_ms") is None and rng.random() < float(config.get("cancel_probability", 0)):
            request["cancel_after_ms"] = int(config.get("cancel_after_ms", 100))
    semaphore = asyncio.Semaphore(concurrency)
    tasks: list[asyncio.Task[None]] = []
    completed_by_class = {"interactive": 0, "batch": 0}
    response_by_class = {"interactive": 0, "batch": 0}
    schedule_start = time.perf_counter_ns()

    async with httpx.AsyncClient(timeout=None, limits=httpx.Limits(max_connections=concurrency)) as client:
        server_metrics_before, metrics_error = await fetch_server_metrics(
            client, metrics_url(args.url))
        async def send(sample_id: int, intended_ns: int, request: dict[str, Any]) -> None:
            async with semaphore:
                record: dict[str, Any] = {
                    "run_id": run_id,
                    "sample_id": sample_id,
                    "seed": seed,
                    "priority_class": request["name"],
                    "prompt_tokens": request["prompt_tokens"],
                    "requested_output_tokens": request["output_tokens"],
                    "actual_output_tokens": None,
                    "actual_prompt_tokens": None,
                    "intended_arrival_ns": intended_ns,
                    "send_ns": time.perf_counter_ns(),
                    "accepted_ns": None,
                    "first_token_ns": None,
                    "completion_ns": None,
                    "status": "transport_error",
                    "cancelled": False,
                    "error_class": None,
                    "token_arrivals_ns": [],
                    "ttft_slo_ms": request.get("ttft_slo_ms"),
                    "e2e_slo_ms": request.get("e2e_slo_ms"),
                    "slo_met": None,
                }
                text = ("word " * request["prompt_tokens"]).strip()
                payload = {"model": args.model, "prompt": text,
                           "max_tokens": request["output_tokens"], "stream": True,
                           "stream_options": {"include_usage": True}}
                headers = {"x-inferscale-priority": request["name"]}
                if request.get("deadline_ms") is not None:
                    headers["x-inferscale-deadline-ms"] = str(request["deadline_ms"])
                if request.get("ttft_slo_ms") is not None:
                    headers["x-inferscale-ttft-slo-ms"] = str(request["ttft_slo_ms"])
                if request.get("e2e_slo_ms") is not None:
                    headers["x-inferscale-e2e-slo-ms"] = str(request["e2e_slo_ms"])
                cancel_ms = request.get("cancel_after_ms")
                try:
                    async with asyncio.timeout(cancel_ms / 1000 if cancel_ms else None):
                        async with client.stream("POST", args.url, json=payload, headers=headers) as response:
                            record["accepted_ns"] = time.perf_counter_ns()
                            if response.status_code >= 400:
                                record["status"] = "http_error"
                                record["error_class"] = "http"
                                await response.aread()
                            else:
                                first, prompt_count, output_count, arrivals, saw_data, saw_done, saw_error = await read_sse(
                                    response, int(request.get("slow_reader_ms", 0)))
                                record["first_token_ns"] = first
                                record["actual_prompt_tokens"] = prompt_count
                                record["actual_output_tokens"] = output_count
                                record["token_arrivals_ns"] = arrivals
                                if saw_error:
                                    record["status"] = "sse_error"
                                    record["error_class"] = "sse_error"
                                elif not saw_done:
                                    record["status"] = "incomplete_stream"
                                    record["error_class"] = "missing_done_marker"
                                else:
                                    record["status"] = "ok" if saw_data or output_count == 0 else "empty_stream"
                except TimeoutError:
                    record["status"] = "cancelled"
                    record["cancelled"] = True
                    record["error_class"] = "client_cancel"
                except ValueError as error:
                    record["status"] = "malformed_sse"
                    record["error_class"] = str(error)
                except httpx.HTTPError as error:
                    record["status"] = "transport_error"
                    record["error_class"] = type(error).__name__
                record["completion_ns"] = time.perf_counter_ns()
                if record["accepted_ns"] is not None and request["name"] in response_by_class:
                    response_by_class[request["name"]] += 1
                if record["status"] == "ok":
                    if request["name"] in completed_by_class:
                        completed_by_class[request["name"]] += 1
                    ttft = ((record["first_token_ns"] - record["intended_arrival_ns"]) / 1e6
                            if record["first_token_ns"] else None)
                    e2e = (record["completion_ns"] - record["intended_arrival_ns"]) / 1e6
                    t_slo, e_slo = record["ttft_slo_ms"], record["e2e_slo_ms"]
                    record["slo_met"] = (ttft is not None and (t_slo is None or ttft <= t_slo) and
                                          (e_slo is None or e2e <= e_slo))
                recorder.record(record)

        if mode == "closed_loop":
            next_sample = 0

            async def worker() -> None:
                nonlocal next_sample
                while next_sample < count:
                    sample_id = next_sample
                    next_sample += 1
                    intended_ns = time.perf_counter_ns()
                    await send(sample_id, intended_ns, requests[sample_id])

            tasks = [asyncio.create_task(worker()) for _ in range(concurrency)]
        else:
            queue: asyncio.Queue[tuple[int, int, dict[str, Any]] | None] = asyncio.Queue(maxsize=2 * concurrency)

            async def worker() -> None:
                while True:
                    item = await queue.get()
                    if item is None:
                        return
                    sample_id, intended_ns, request = item
                    await send(sample_id, intended_ns, request)

            tasks = [asyncio.create_task(worker()) for _ in range(concurrency)]
            for sample_id, offset_s in enumerate(schedules):
                intended_ns = schedule_start + int(offset_s * 1e9)
                remaining = (intended_ns - time.perf_counter_ns()) / 1e9
                if remaining > 0:
                    await asyncio.sleep(remaining)
                # Intended times are fixed in advance; queue pressure is visible as send lateness.
                await queue.put((sample_id, intended_ns, requests[sample_id]))
            for _ in range(concurrency):
                await queue.put(None)

        await asyncio.gather(*tasks)
        run_end_ns = time.perf_counter_ns()
        (server_metrics_after, after_metrics_error, server_metrics_wait_ms,
         server_metrics_fresh) = await fetch_final_server_metrics(
            client, metrics_url(args.url), completed_by_class, response_by_class,
            server_metrics_before)
        if metrics_error is None:
            metrics_error = after_metrics_error

    await recorder.close()
    records = [json.loads(line) for line in raw_path.read_text(encoding="utf-8").splitlines()]
    summary = summarize(records, (run_end_ns - schedule_start) / 1e9, args.max_send_lateness_ms)
    summary.update({"run_id": run_id, "workload": Path(args.workload).stem,
                    "arrival_mode": mode, "offered_rate_rps": rate,
                    "record_drops": recorder.dropped,
                    "server_metrics_before": server_metrics_before,
                    "server_metrics_after": server_metrics_after,
                    "server_metrics_wait_ms": server_metrics_wait_ms,
                    "server_metrics_fresh_for_client_results": server_metrics_fresh,
                    "server_metric_deltas": server_metric_deltas(
                        server_metrics_before, server_metrics_after),
                    "server_metrics_error": metrics_error,
                    "valid": summary["valid"] and recorder.dropped == 0,
                    "raw_records": str(raw_path)})
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return run_dir


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload", required=True)
    parser.add_argument("--url", default="http://127.0.0.1:8080/v1/completions")
    parser.add_argument("--model", default="local-model")
    parser.add_argument("--output", default="bench/runs")
    parser.add_argument("--requests", type=int)
    parser.add_argument("--mode", choices=("closed_loop", "open_fixed", "open_poisson", "burst"))
    parser.add_argument("--rate", type=float)
    parser.add_argument("--concurrency", type=int)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--max-send-lateness-ms", type=float, default=100.0)
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
