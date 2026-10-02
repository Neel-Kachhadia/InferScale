# ADR-001: Embed the pinned libllama API

**Status:** Accepted

InferScale will own its request lifecycle and scheduler and call the pinned llama.cpp library directly. An internal `llama-server` proxy would move admission, batching, cancellation, and sequence ownership back into another scheduler, preventing InferScale from acting as the scientific control.

The external comparison remains an unmodified `llama-server` build from the same pinned source. The pin for this phase is v0.4.1, commit `b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`. The smoke target uses the public C API for model loading, vocabulary tokenization, explicit sequence-aware batches, prefill/decode, sampling, and sequence clearing.

The proof can compile without a model. Successful inference remains unverified until a model file and usable target backend are available.
