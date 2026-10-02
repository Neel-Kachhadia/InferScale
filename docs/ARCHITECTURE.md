# Implemented architecture: Prompt 1

The repository's frozen architecture package remains in `InferScale_Project_Architecture_v2/`. This file records what is implemented in the executable repository today.

## Current path

The current C++ code contains a bounded cross-thread event queue, an owner-thread `RuntimeReactor`, a stable FIFO work-plan builder, a generation-aware sequence allocator, move-only batch tickets, a bounded stream buffer, and a deterministic CPU `FakeEngine`. The reactor applies lifecycle changes, cancellation, deadlines, output suppression, accounting, and clear acknowledgements.

The direct backend target is a small `libllama` proof executable; it is separate from the fake runtime. The pinned external `llama-server` is built from the same llama.cpp checkout by the baseline script and is not used as InferScale's scheduler.

`inferscale_fake_server` is an async Boost.Asio/Beast transport test binary. It runs the reactor and `FakeEngine`, accepts a strict streaming completion subset, bounds request bodies to 64 KiB, active connections to 128, live sequences to 64, and output buffers to 4 KiB per request. The fake adapter treats whitespace-delimited prompt words as synthetic input units and emits deterministic numeric token text. No request path or model field can load a file. This binary does not serve a real model.

## Ownership boundaries

- `RuntimeReactor` is the only writer of the request map, lifecycle state, FIFO order, logical committed-token accounting, and clear queue. Mutating methods bind to the first caller thread and reject later calls from other threads.
- Producers post immutable events through a bounded queue. New-request overflow is rejected; cancellation, writable, clear, and ticket completion delivery use blocking bounded enqueue so lifecycle/ownership events are not silently dropped. The queue is sealed after shutdown retirement and pending admissions are rejected.
- `BatchTicket` is move-only. Dropping an unretired live ticket terminates in debug and release builds. The reactor decrements sequence pins on completion even when cancellation suppresses output.
- A sequence slot becomes reusable only after ticket retirement and a successful engine clear acknowledgement.
- `FakeEngine` owns its sequence positions under a mutex, supports configurable prefill/decode delays, pre- and post-execution barriers, and failure injection.
- The HTTP session owns its socket and uses async reads/writes. It consumes the shared bounded output buffer and posts writable/cancel events; it never mutates reactor lifecycle state. A 5 second socket write timeout turns blocked writes into cancellation. It polls the stream every 2 ms and detects disconnects when a response write fails; it does not independently watch for a peer disconnect while waiting for the next token.

## Work and lifecycle

`WorkItem` names one exact prefill chunk or one decode step. The FIFO builder preserves eligible order, limits work by token, sequence, and prefill-chunk bounds, and does not choose priorities. Terminal intent is retained as a pending-retire state until all tickets retire and the sequence clear succeeds.

The token estimate is logical accounting only. It is not physical KV allocation, paging, or a VRAM measurement.

## Not implemented yet

There is no production `libllama` adapter or backend `BatchBuilder` wired into `RuntimeReactor`, real tokenizer integration, model-backed HTTP service, or GPU E2E. The separate smoke executable builds a sequence-aware libllama batch and proves API compilation, but cannot produce output without a model. The fake runtime reserves 11 bytes per decode, sized for its decimal `uint32` token text; this does not cover variable-length libllama pieces or detokenizer state. Its 4 KiB HTTP stream buffer exceeds the maximum fake response at 128 tokens, so backpressure suppression is unit-tested in the reactor but not exercised end to end over HTTP. The fake server has signal-driven shutdown and a 10 second process hard-stop path; a stuck/corrupted real GPU context still needs the Prompt-3 recovery campaign.
