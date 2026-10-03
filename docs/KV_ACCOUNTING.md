# Logical KV resource accounting

InferScale v1 budgets sequence-token occupancy and admission. It does not own
physical KV allocation, PagedAttention, block tables, or prefix sharing. The
term “KV tokens” below means a conservative logical accounting unit bounded by
the resolved llama.cpp sequence context.

## Hard capacity calibration

The starting policy derives a hard logical capacity from the pinned backend's
resolved sequence count and context size, retaining a configurable 10% reserve
from the resolved aggregate context. For the Prompt-2 validation settings of
16 sequences at 2048 context tokens each, this policy resolves to 29,491 hard
logical tokens from 32,768 configured sequence-token positions. The per-request
safety margin defaults to 16 tokens. This is a logical cap, not an estimate of
bytes per token and not a claim that llama.cpp exposes physical KV cells.

The final Prompt-2 calibration record must accompany the GPU mixed-load
evidence and record the exact model/backend/configuration, resolved context,
observed GPU memory, chosen margin/cap, and the reason the headroom is
conservative. Model buffers, execution workspace, transient allocations, and
runtime overhead remain outside the logical token ledger; observed VRAM is
therefore reported separately.

The measured Prompt-2 profile used TinyLlama Q4_K_M (SHA-256
`9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0`),
llama.cpp `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`, context 2048 x 16,
batch/ubatch 2048, and 99 GPU layers on the RTX 4060 Laptop GPU. It resolved
32,768 logical context positions and a hard cap of 29,491. During the
representative profiled mixed run, one-second `nvidia-smi` sampling observed
2,141 MiB peak of 8,188 MiB; the backend reported a 601 MiB CUDA model buffer.
Across the 15 unprofiled matrix runs, the greatest observed logical charge
was 20,598 tokens and the greatest sampled GPU use was 2,145 MiB. No CUDA OOM
or hard-limit abort occurred. The 10% logical-context reserve is a
configuration-space margin, not a conversion to or claim about physical KV
bytes; model buffers, workspace, and transient allocations are kept outside
that ledger and checked separately by GPU telemetry.

## Lease and conservation model

Each admitted `RequestKey` owns one move-only `KvLease`. Its generation is part
of its identity. The reactor alone mutates leases. The lease tracks soft
reservation, committed growth, currently authorized growth, and peak logical
charge. Its destructor does not release capacity.

The global hard invariant is:

```text
committed + soft_reserved + authorized <= hard_usable_capacity
```

Admission first reserves prompt tokens, a bounded generation-growth reserve,
and a safety margin. Runtime growth consumes that request's soft reservation
first. Growth beyond it must fit shared uncharged hard headroom. Authorization
for every item in a work plan is checked as one bounded plan before backend
submission; the aggregate plan cannot reuse the same global headroom twice.
Successful execution converts exact authorization into committed growth.
Known nonexecution returns authorization to that lease. Unknown/partial
backend mutation keeps the conservative authorization charged until successful
sequence clear.

Unused soft reservations can be reclaimed under pressure. Reclamation never
reduces committed occupancy and never grants another request growth above the
global hard boundary. If every ready request is blocked solely by hard growth,
there are no in-flight tickets, and no clear is pending, the runtime fails one
deterministic victim, then waits for its sequence-clear acknowledgement before
considering another victim. This prevents both silent overcommit and an
unbounded stall with every sequence pinned.

Cancellation and deadlines suppress future work and output, but do not release
an in-flight ticket's charge. A successful completion settles growth even if
the request has become terminal. The lease and backend sequence are released
only after all tickets retire and sequence clear succeeds. A failed clear
quarantines both and marks the runtime unhealthy.

## Pressure states

Pressure is computed from committed plus soft-reserved plus authorized logical
tokens. The initial watermarks are 70% (`SOFT_PRESSURE`) and 85%
(`HIGH_PRESSURE`); reaching the hard capacity is `HARD_LIMIT`. They are
configuration values, not physical-memory thresholds. Normal admissions may
continue while the hard reservation fits; high pressure triggers reclamation
of excess soft reservation and more conservative admission policy. The runtime
reports pressure and each admission/resource-defer reason.

## Reconciliation and limitations

Reconciliation may use only backend state that is authoritative for the exact
sequence and generation. The current `LlamaEngine::memory_stats()` is a
software estimate, not a physical KV-cell query, and is not used to release
accounted capacity. Ambiguous decode failure remains charged through clear.
This design provides hard logical admission bounds over separate llama.cpp
sequence memories; it does not share memory across sequences or assert exact
GPU-byte occupancy.
