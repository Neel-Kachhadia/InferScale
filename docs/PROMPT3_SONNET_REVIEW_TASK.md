# Prompt 3 final advisory review

Review this candidate as an independent senior C++ and inference-infrastructure
engineer. Find material implementation, test, benchmark-causality, or
claim-integrity defects that would make `v1.0-resume-grade` misleading.

Identify concrete blockers only. For each finding, give the affected file or
claim, the failure or misinterpretation it enables, and the smallest objective
check that would confirm it. Classify each finding as `MATERIAL REQUIRED FIX`,
`VALID BUT OUT OF SCOPE / FUTURE WORK`, `UNSUPPORTED BY EVIDENCE`, or
`OPTIONAL POLISH`. Do not redesign the runtime or infer a passing gate from
documentation alone.

The recorded release conclusion is intentionally `NOT RELEASED`: the required
sustained GPU comparisons stopped at the predeclared 85 C thermal limit, yielded
no valid paired trials, and the soak was not run. The project retains Prompt 2
development evidence, small reproducibility smokes, and extensive CPU/FakeEngine
validation. Review whether the documentation and machine-readable artifacts
preserve those distinctions, accurately describe the code, and avoid presenting
invalid campaigns as performance evidence. Objective runtime and benchmark
evidence in this packet takes precedence over reviewer opinion.

Return a concise list of findings or state that you found no material blockers.
