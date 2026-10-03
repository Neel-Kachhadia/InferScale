# Sanitizer and lifetime evidence

| Suite | Result | Scope and limitation |
|---|---|---|
| ASan | PASS WITH LIMITATION | Clean-worktree CPU/FakeEngine CTest: runtime, policy, HTTP 3/3 with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`. |
| UBSan | PASS | Same three clean-worktree targets with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. |
| TSan | PASS WITH LIMITATION | `/tmp/inferscale-phase5-tsan`: runtime, policy, HTTP 3/3 with `setarch x86_64 -R`. Default launch failed before tests with `FATAL: ThreadSanitizer: unexpected memory mapping`. GCC reports Boost.Asio `atomic_thread_fence` is not modeled by TSan. |
| LeakSanitizer | PASS WITH LIMITATION | The LSan-enabled suite reaches teardown, then the host runtime reports `LeakSanitizer does not work under ptrace`. No LSan-clean claim is made. |
| Long-run leak/soak | BLOCKED | A sustained GPU soak was not run after the 16 RPS and 8 RPS trials reached 85 C. Deterministic runtime tests still assert zero live sequences/leases after cleanup, but they do not replace a soak trend. |

The final clean ASan/UBSan run was part of
`/tmp/inferscale-release-check-887bb0b/scripts/reproduce_release.sh` at commit
`887bb0b70c56aaa05326526c1b12c2836d40eab2`. Runtime and policy tests cover
generation fencing, move-only tickets, exact cleanup, hard-capacity
arithmetic, released-lease immutability, unknown growth, and shutdown
retirement. The 256-seed model checks resource totals and final cleanup after
up to 64 operations per seed.
