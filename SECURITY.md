# Security notes

InferScale is a local single-process inference service. The server binds to
`127.0.0.1` by default; deployments that bind to another interface must put
their own authentication and network controls in front of it. This prototype
does not implement authentication or authorization.

The HTTP request parser caps the body at 64 KiB. Prompt text is capped at 32
KiB, `max_tokens` is restricted to 1–128, JSON shape and supported fields are
validated, runtime queues and per-response stream buffers are bounded, and
deadlines and cancellation are handled by the runtime. The API accepts prompt
text only: it does not accept filesystem paths, shell commands, model paths,
or arbitrary backend options from requests. Model paths and CUDA/backend
settings are process-start configuration.

The benchmark scripts use local output paths supplied by the operator. Treat
raw prompts and request records as potentially sensitive when adapting the
harness to real user data. The checked-in synthetic workloads use repeated
`word` tokens. GGUF model weights and raw soak/benchmark request records are
kept outside version control.

Build inputs are pinned in CMake: Boost 1.83.0, GoogleTest 1.14.0,
nlohmann/json 3.11.3, and llama.cpp commit
`b29c606e28a01b1bc8c1351026a0fa6e616bf6c4`. Python benchmark dependencies are
pinned in `requirements-bench.txt`; CI review scanning uses
`requirements-dev.txt`. These are pins, not a claim that the full transitive
OS or GPU toolchain is hermetic.

Run `detect-secrets` against the tracked source before release. Its current
allowlisted findings are SHA-256/git commit identifiers and are documented in
the release scan record; no credentials, API keys, or model weights are
intended to be committed. Report a security issue privately to the repository
maintainer rather than opening a public issue with exploit details.
