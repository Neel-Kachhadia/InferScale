# Sanitizer and lifetime evidence

| Suite | Result | Scope and limitation |
|---|---|---|
| ASan | PASS | Current source regression suite, runtime, policy, and HTTP CTest 3/3 with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`. |
| UBSan | PASS | Same current-source CTest targets 3/3 with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. |
| TSan | PASS WITH LIMITATION | Current-source runtime, policy, and HTTP targets 3/3 using `setarch x86_64 -R`. The default launch remains subject to the host's “unexpected memory mapping” issue; GCC does not model Boost.Asio `atomic_thread_fence`. |
| LeakSanitizer | PASS WITH LIMITATION | Host-capable current-source CTest 3/3 with ASan leak detection enabled and no leak report. A separate sandboxed invocation exited with “LeakSanitizer does not work under ptrace”; that host/execution-context limitation remains recorded. |
| Resource soak | PASS WITH LIMITATION | Separate 20.2555-minute FIFO soak: 1,190 samples, 2,462 offers, zero hard-capacity or resource-invariant violations, zero avoidable OOM markers, clean post-drain and shutdown. GPU memory flattened in the final ten minutes. RSS remained within the frozen drift allowance but continued rising; a host-RSS plateau is not established. See docs/RESOURCE_SOAK.md and artifacts/release-v1/resource-soak-summary.json. |

The current-source CPU/FakeEngine suite passed runtime, policy, and HTTP tests 3/3 under ASan/UBSan with LSan enabled, and under TSan with the documented per-process ASLR workaround. The sandboxed LSan teardown failure remains recorded alongside the separate host-capable passing run. The soak adds operational resource-drift evidence independently of sanitizer results.

The runtime regression suite covers generation fencing, move-only tickets, exact cleanup, hard-capacity arithmetic, released-lease immutability, unknown growth, cancellation, and shutdown retirement. The deterministic lifecycle campaign covered 256 seeds with up to 64 operations and eight requests per seed; parser mutation covered 2,048 deterministic cases and is not coverage-guided fuzzing.
