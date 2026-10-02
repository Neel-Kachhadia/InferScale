# InferScale — Optional Research Extension: True Physical Paged KV / Paged Attention

## Status
**Not part of resume-grade v1.**

This document exists to prevent architectural ambiguity. InferScale must not claim physical paged KV unless this extension or an equivalent backend implementation is actually completed.

## What "true paged KV" means
Logical accounting alone is not paged KV.

A genuine implementation requires the runtime/backend to own or participate in:

```text
fixed-size physical KV blocks
free-block allocator
per-sequence block tables
attention lookup through block mappings
safe block reclamation
partial-block handling
copy/share semantics if prefix sharing exists
backend kernels that consume non-contiguous block layout
```

If attention still assumes the backend's original contiguous/unified memory arrangement and InferScale merely counts "pages," the feature is resource accounting, not PagedAttention.

## Research architecture

```text
Scheduler
   |
   v
BlockManager ------------------------------+
   |                                       |
   +-> free block pool                     |
   +-> sequence block tables               |
   +-> refcounts / ownership               |
   +-> optional prefix sharing / CoW       |
                                           v
                              paged KV memory backend
                                           |
                                           v
                               paged attention kernels
                                           |
                                          GPU
```

## Required correctness invariants

```text
no block has conflicting exclusive owners
refcount equals number of live references
free block has zero references
sequence logical token position maps to valid block/offset
block cannot be recycled while an in-flight kernel references it
copy-on-write occurs before mutating a shared block
partial tail blocks have defined valid-token range
cancellation releases only after in-flight retirement
```

## Minimum research phases
This extension should be treated as a separate project branch/milestone, not squeezed into core Phase 3.

### R1 — block manager + CPU reference model
Implement allocation, sequence block tables, randomized invariant tests, and deterministic replay without GPU attention changes.

### R2 — backend memory integration
Allocate physical KV blocks in the actual backend representation and prove sequence-equivalent storage/load behavior.

### R3 — attention integration
Modify or add the attention path so queries read K/V through block tables. Verify numerical/token equivalence across supported model architectures/edge cases.

### R4 — sharing/CoW (optional)
Add prefix block sharing only after exclusive ownership is correct.

### R5 — benchmark study
Compare memory fragmentation/capacity/concurrency and end-to-end latency/throughput against the original backend memory path.

## Correctness evidence

Required before any resume claim:

- deterministic greedy/token equivalence where expected;
- multiple model architectures if claiming generality;
- randomized block allocation/release tests;
- cancellation and sequence-reuse races;
- Compute Sanitizer for new CUDA paths;
- stress near full block capacity;
- prefix-sharing CoW tests if implemented;
- state save/load behavior documented if affected.

## Performance evidence

Do not claim a benefit merely because allocation is paged.

Measure:

```text
maximum concurrent sequences before capacity failure
internal/unusable memory loss
block allocator overhead
attention kernel performance
TTFT/ITL
throughput/goodput
GPU memory peak
prefix sharing benefit if present
```

## Why this is deferred
A true paged-KV design crosses model-memory layout and attention execution. It is large enough to compromise the feasibility of the core one-GPU runtime project if made mandatory.

Deferring it raises the quality of v1 because:

```text
core claims stay honest
scheduler causality stays clear
implementation remains finishable
research extension remains genuinely ambitious
```

## Resume wording if not implemented
Use:

> KV-aware admission and per-request resource accounting.

Do not use:

> PagedAttention, paged KV cache, physical KV block allocator.

## Resume wording if genuinely implemented
Only after all research gates pass, a bullet may accurately say something like:

> Implemented a physical block-based KV memory path with sequence block tables and paged attention integration, reducing **[measured memory/capacity metric]** under **[exact workload]** while preserving **[correctness evidence]**.

Fill only with measured values.
