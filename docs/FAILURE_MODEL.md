# Failure model and current coverage

Backend results use `RequestRecoverable`, `EngineRecoverable`, `EngineRestartRequired`, and `ProcessFatal`. Invalid requests, deadline expiry, client cancellation, and queue/resource rejection terminate only the addressed request. Backend execution returns one result for the whole issued `BatchTicket`; therefore a `RequestRecoverable` execution result fails every request represented by that ticket, then retires the ticket and clears each affected sequence. It does not reconstruct or restart the backend context. Failures classified `EngineRestartRequired` and uncaught backend exceptions classified `ProcessFatal` fail all runtime work, stop new admission, and make `/health` and `/ready` return 503. The HTTP process remains alive until an external supervisor or operator restarts it or sends SIGTERM; automatic in-process engine restart and automatic process exit are not implemented. Unknown backend statuses map conservatively to `ProcessFatal`.

A request failure, deadline, cancellation, or shutdown records terminal intent first. The reactor stops scheduling for that `RequestKey`, retains issued-ticket pins, suppresses stale output, and queues a backend clear only after every ticket retires. Sequence reuse then waits for llama synchronization, successful `llama_memory_seq_rm`, and a matching clear acknowledgement. A clear failure leaves the slot and lease quarantined, transitions the service unhealthy, and prevents clean shutdown completion. The executable's 10-second shutdown deadline then takes its documented exit-124 hard-stop path; that process-level timeout path is implemented but has not been deliberately fault-injected.

| Failure | Current classification and behavior |
| --- | --- |
| Invalid JSON, unsupported fields, over-context input, request body too large | Rejected by HTTP validation before engine submission; no sequence lease is created. |
| Queue, sequence, or KV admission pressure | Explicit bounded rejection; no engine work is issued for the rejected request. |
| Client disconnect, cancellation, deadline | Request terminal intent; no future work is scheduled after reactor processing. Issued tickets remain pinned and charged until retirement and clear. |
| Pinned `llama_decode` status `1` or `-1`; batch validation/execution failure | `RequestRecoverable`; fail every request in the affected ticket, clear their sequences, and preserve service if clears succeed. |
| `EngineRecoverable` | Supported by the runtime contract and FakeEngine fault injection; it fails the affected request. The production llama.cpp adapter currently does not emit this class. |
| Decode state mismatch, other nonzero decode status, output postprocess invariant | `EngineRestartRequired`; stop admission, fail runtime work, return unhealthy readiness, require external service/process restart. |
| Uncaught backend exception or unrecoverable runtime/clear invariant | `ProcessFatal`; same conservative unhealthy-service handling. The process does not exit by itself. |
| SIGTERM / SIGINT | Stop acceptance, retire work and clear sequences; process hard-stops with exit 124 if the 10-second deadline expires. |

The model-backed cancellation test closes the client during a long prefill; a subsequent request succeeds. The model-backed slow-reader test uses 186 bytes of test output capacity and a 1024-byte send buffer, leaves the SSE response unread, reaches the five-second write timeout, and then verifies the same model service can complete a new request. Prompt 1's frozen active-shutdown evidence sends SIGTERM after receiving a real generated piece; the stream closes, in-flight ownership retires, and CUDA allocations are released. Prompt 3 did not repeat that GPU test. Runtime unit/integration tests retain deterministic FakeEngine barriers and generation/ticket race checks.

The generic `classify_engine_error` helper is tested as a conservative category
mapper, but the production llama.cpp adapter does not call it. `LlamaEngine`
maps pinned `llama_decode` statuses 1 and -1 directly to `RequestRecoverable`
and maps other nonzero statuses to `EngineRestartRequired`; `EngineRecoverable`
is available to the runtime contract and FakeEngine but is not currently emitted
by that adapter.

The current-source TSan suite's default launch remains subject to this host's address-space mapping limitation. With the per-process workaround `setarch x86_64 -R ctest --test-dir /tmp/inferscale-prompt3-tsan --output-on-failure`, runtime, policy, and HTTP CTest targets passed (3/3), including the 256-request cancellation/drain regression. No system ASLR setting was changed. GCC also warned that Boost.Asio's `atomic_thread_fence` is not modeled by TSan, so this is a clean run of the supported targets with instrumentation limitation, not proof that the fences themselves were race-checked. Current-source ASan/UBSan CTest passed 3/3 with `detect_leaks=0`; a separate CTest attempt with leak detection enabled reached test teardown and failed with `LeakSanitizer does not work under ptrace`. LSan is not reported as passing. Operational lease, sequence, request, stream, and queue counters are included in the pending sustained soak.

Remaining limitations: compact generation-history state for an unbounded long-lived process; move vocabulary tokenization off the HTTP I/O thread if contention warrants; return a request-scoped rejection code for reactor-level resource rejection; and automatic backend restart is unsupported. On restart-required or process-fatal failure, an external supervisor must restart the service.
