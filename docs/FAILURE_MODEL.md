# Failure model: current foundation

The runtime uses `RequestRecoverable`, `EngineRecoverable`, `EngineRestartRequired`, and `ProcessFatal`. Known injected fake-engine failures are classified explicitly; unknown backend codes map to `ProcessFatal`. A request failure first enters `FAILED_PENDING_RETIRE`; ticket counts still drain, and the sequence is not reusable until a successful clear acknowledgement.

Cancellation and deadlines similarly mark terminal intent before cleanup. Stale `RequestKey` events increment a counter and do not mutate a newer generation. Decode output from a cancelled generation is discarded.

The current reactor records process-fatal state and keeps a sequence allocated after clear failure. Fatal engine state closes outstanding request streams and retires in-flight tickets. A failed backend clear leaves its sequence quarantined; the fake service does not attempt engine restart or reuse that sequence. The fake server handles SIGINT/SIGTERM and monitors runtime plus active HTTP sessions against a 10 second hard deadline; it exits with status 124 if that deadline expires. The service destructor has the same bounded fallback.

Engine restart/reload and production telemetry are not implemented. The latest TSan run passed all 22 runtime tests. The HTTP test still aborts before startup with `ThreadSanitizer: unexpected memory mapping` on repeated attempts, so service-level race-detector evidence is unavailable. There is no integrated production libllama server yet; the direct libllama executable is a standalone smoke proof and still needs a model and usable backend to execute.
