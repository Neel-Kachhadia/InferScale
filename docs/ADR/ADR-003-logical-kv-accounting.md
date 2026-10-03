# ADR-003: Logical KV resource accounting for v1

**Status:** Accepted

InferScale v1 owns hard resource budgeting and admission in logical
sequence-token units. It does not own physical PagedAttention, KV blocks,
block tables, or prefix sharing. The hard cap is derived conservatively from
the pinned backend's resolved context and is checked on every growth
authorization.

Each generation-fenced request owns one reactor-mutated `KvLease`. Committed
growth, soft reservation, and outstanding authorization share the hard
capacity. Successful backend clear acknowledgement is the authority for
release; request cancellation, ticket completion alone, and the software
`memory_stats()` estimate are not. Unknown backend mutation remains charged
until clear.

This keeps resource admission explicit and testable without claiming physical
KV ownership. Any future physical allocator requires a separate decision and
must not reinterpret these logical counters as pages or bytes.
