from __future__ import annotations

import json
import sys
import asyncio
import contextlib
import io
import socket
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))

import stats
from run import build_arrivals, run as run_load
import yaml


class BenchmarkTests(unittest.TestCase):
    def test_statistics_on_known_distribution(self) -> None:
        self.assertEqual(stats.percentile([1, 2, 3, 4], 0.5), 2.5)
        self.assertAlmostEqual(stats.percentile([1, 2, 3, 4], 0.99), 3.97)
        self.assertIsNone(stats.percentile([], 0.95))

    def test_open_arrivals_repeat_with_seed_and_ignore_response_times(self) -> None:
        workload = {"mode": "open_poisson"}
        first = build_arrivals(workload, 50, 4.0, 77)
        second = build_arrivals(workload, 50, 4.0, 77)
        self.assertEqual(first, second)
        self.assertTrue(all(a < b for a, b in zip(first, first[1:])))
        fixed = build_arrivals({"mode": "open_fixed"}, 4, 2.0, 77)
        self.assertEqual(fixed, [0.0, 0.5, 1.0, 1.5])

    def test_required_workloads_are_parseable(self) -> None:
        names = {"interactive_short", "long_context", "mixed_slo", "burst", "cancel", "slow_reader"}
        found = {path.stem for path in (ROOT / "bench/workloads").glob("*.yaml")}
        self.assertTrue(names <= found)
        for path in (ROOT / "bench/workloads").glob("*.yaml"):
            data = yaml.safe_load(path.read_text(encoding="utf-8"))
            self.assertIn(data["arrival"]["mode"], {"closed_loop", "open_fixed", "open_poisson", "burst"})
            self.assertTrue(data["classes"])

    def test_raw_record_retains_required_fields_and_slo_stats(self) -> None:
        required = {"run_id", "sample_id", "seed", "priority_class", "prompt_tokens",
                    "requested_output_tokens", "actual_output_tokens", "intended_arrival_ns",
                    "send_ns", "accepted_ns", "first_token_ns", "completion_ns", "status",
                    "cancelled", "error_class"}
        record = {key: None for key in required}
        record.update({"run_id": "r", "sample_id": 1, "seed": 5, "priority_class": "interactive",
                      "prompt_tokens": 10, "requested_output_tokens": 2, "intended_arrival_ns": 100,
                      "send_ns": 100, "accepted_ns": 120, "first_token_ns": 150,
                      "completion_ns": 200, "status": "ok", "cancelled": False,
                      "token_arrivals_ns": [150, 170], "slo_met": True})
        self.assertFalse(required - record.keys())
        record.update({"ttft_slo_ms": 50, "e2e_slo_ms": 100})
        failed = dict(record, sample_id=2, status="transport_error", slo_met=None,
                      send_ns=100, completion_ns=180, first_token_ns=None)
        result = stats.summarize([record, failed], 1.0, 0.1)
        self.assertEqual(result["ttft_ms"]["p50"], 0.00005)
        self.assertEqual(result["e2e_ms"]["p50"], 0.0001)
        self.assertEqual(result["itl_ms"]["p50"], 0.00002)
        self.assertEqual(result["service_e2e_ms"]["p50"], 0.0001)
        self.assertEqual(result["goodput_requests_per_s"], 1.0)
        self.assertEqual(result["slo_samples"], 2)
        self.assertEqual(result["slo_attainment"], 0.5)
        self.assertTrue(result["valid"])
        self.assertIsNone(result["output_tokens_per_s"])
        json.dumps(record)

    def test_latency_and_slo_include_sender_lateness(self) -> None:
        record = {
            "status": "ok", "intended_arrival_ns": 1_000_000_000,
            "send_ns": 1_500_000_000, "first_token_ns": 1_650_000_000,
            "completion_ns": 1_700_000_000, "token_arrivals_ns": [],
            "actual_output_tokens": 1, "actual_prompt_tokens": 1,
            "ttft_slo_ms": 200, "e2e_slo_ms": 300, "slo_met": False,
        }
        failed_fast = dict(record, status="http_error", first_token_ns=None,
                           completion_ns=1_501_000_000, actual_output_tokens=None,
                           actual_prompt_tokens=None, slo_met=None)
        result = stats.summarize([record, failed_fast], 1.0, 1000)
        self.assertEqual(result["ttft_ms"]["p50"], 650)
        self.assertEqual(result["e2e_ms"]["p50"], 700)
        self.assertEqual(result["service_ttft_ms"]["p50"], 150)
        self.assertEqual(result["service_e2e_ms"]["p50"], 200)
        self.assertEqual(result["slo_attainment"], 0)


class LoadGeneratorIntegrationTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        self.malformed = False
        self.error_event = False
        self.omit_done = False
        self.delay_s = 0.0
        probe = None
        try:
            probe = socket.socket()
            probe.bind(("127.0.0.1", 0))
        except OSError as error:
            raise unittest.SkipTest("sandbox does not permit loopback TCP") from error
        finally:
            if probe is not None:
                probe.close()
        try:
            self.server = await asyncio.start_server(self._handle, "127.0.0.1", 0)
        except OSError as error:
            cause = error.__cause__
            if getattr(error, "errno", None) == 1 or getattr(cause, "errno", None) == 1 or \
                    "could not bind on any address" in str(error):
                raise unittest.SkipTest("sandbox does not permit loopback TCP") from error
            raise
        self.port = self.server.sockets[0].getsockname()[1]

    async def asyncTearDown(self) -> None:
        self.server.close()
        await self.server.wait_closed()

    async def _handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            headers = await reader.readuntil(b"\r\n\r\n")
            length = next((int(line.split(b":", 1)[1]) for line in headers.split(b"\r\n")
                           if line.lower().startswith(b"content-length:")), 0)
            await reader.readexactly(length)
            writer.write(b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                         b"Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n")
            await writer.drain()
            lines = ([b"data: {not-json}\n\n"] if self.malformed else [
                b'data: {"choices":[{"text":"A"}]}\n\n',
                b'data: {"choices":[{"text":"B"}]}\n\n',
                b'data: {"choices":[],"usage":{"prompt_tokens":5,"completion_tokens":2}}\n\n',
                b"data: [DONE]\n\n",
            ])
            if self.error_event:
                lines.insert(2, b'data: {"error":{"message":"backend failed"}}\n\n')
            if self.omit_done:
                lines = [line for line in lines if line != b"data: [DONE]\n\n"]
            for line in lines:
                if self.delay_s:
                    await asyncio.sleep(self.delay_s)
                writer.write(f"{len(line):X}\r\n".encode() + line + b"\r\n")
                await writer.drain()
            writer.write(b"0\r\n\r\n")
            await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            writer.close()
            with contextlib.suppress(ConnectionError):
                await writer.wait_closed()

    async def _run(self, *, requests: int = 2, cancel_after_ms: int | None = None) -> tuple[dict, list[dict]]:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            workload = temp_path / "workload.yaml"
            cancel = f"    cancel_after_ms: {cancel_after_ms}\n" if cancel_after_ms else ""
            workload.write_text(
                "name: local-mock\nseed: 9\nrequests: " + str(requests) +
                "\narrival:\n  mode: open_fixed\n  rate_rps: 100\n  concurrency: 2\nclasses:\n"
                "  - name: interactive\n    prompt_tokens: 5\n    output_tokens: 2\n" + cancel +
                "    ttft_slo_ms: 1000\n    e2e_slo_ms: 2000\n", encoding="utf-8")
            args = SimpleNamespace(workload=str(workload), requests=None, mode=None, rate=None,
                                   concurrency=None, seed=None, output=str(temp_path / "runs"),
                                   url=f"http://127.0.0.1:{self.port}/v1/completions", model="test",
                                   max_send_lateness_ms=1000.0)
            with contextlib.redirect_stdout(io.StringIO()):
                run_dir = await run_load(args)
            summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
            records = [json.loads(line) for line in (run_dir / "requests.jsonl").read_text(encoding="utf-8").splitlines()]
            return summary, records

    async def test_open_loop_writes_complete_raw_sse_records(self) -> None:
        summary, records = await self._run()
        self.assertTrue(summary["valid"])
        self.assertEqual(summary["sample_count"], 2)
        self.assertEqual(summary["successful_requests"], 2)
        self.assertEqual(summary["output_token_observations"], 2)
        self.assertTrue(all(r["accepted_ns"] and r["first_token_ns"] for r in records))
        self.assertTrue(all(r["actual_output_tokens"] == 2 for r in records))
        self.assertTrue(all(r["slo_met"] for r in records))

    async def test_malformed_sse_is_retained_as_failed_record(self) -> None:
        self.malformed = True
        summary, records = await self._run(requests=1)
        self.assertEqual(summary["statuses"], {"malformed_sse": 1})
        self.assertEqual(records[0]["status"], "malformed_sse")

    async def test_midstream_error_is_not_counted_as_success(self) -> None:
        self.error_event = True
        summary, records = await self._run(requests=1)
        self.assertEqual(summary["statuses"], {"sse_error": 1})
        self.assertFalse(records[0]["slo_met"])

    async def test_stream_without_done_marker_is_incomplete(self) -> None:
        self.omit_done = True
        summary, records = await self._run(requests=1)
        self.assertEqual(summary["statuses"], {"incomplete_stream": 1})

    async def test_client_timeout_records_cancellation(self) -> None:
        self.delay_s = 0.05
        summary, records = await self._run(requests=1, cancel_after_ms=5)
        self.assertEqual(summary["statuses"], {"cancelled": 1})
        self.assertTrue(records[0]["cancelled"])


if __name__ == "__main__":
    unittest.main()
