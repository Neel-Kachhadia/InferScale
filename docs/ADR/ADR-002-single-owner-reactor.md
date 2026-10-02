# ADR-002: Single-owner runtime reactor

**Status:** Accepted

One reactor thread owns request lifecycle, FIFO readiness, deadlines, cancellation, sequence assignment, ticket counts, and logical token accounting. Network and engine code post immutable events through bounded queues. This keeps lifecycle transitions deterministic and makes stale generations testable.

The current core binds mutation to the first reactor caller and enforces that thread on subsequent mutation. Sequence clearing is a request/acknowledgement handshake; clearing or reusing a sequence before all tickets retire is rejected. Normal request IDs must be globally unique and are not retained after retirement. A recyclable slot ID must use a nonzero generation hint that increases on each reuse; only these slot generations are retained. The HTTP and production executor adapters are still pending.
