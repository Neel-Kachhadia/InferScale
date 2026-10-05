# Graph Report - InferScale  (2026-10-05)

## Corpus Check
- 165 files · ~147,265 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 47 file(s) not represented in the graph (top: .csv 22, .jsonl 20, (none) 3)

## Summary
- 2617 nodes · 4224 edges · 199 communities (171 shown, 28 thin omitted)
- Extraction: 88% EXTRACTED · 12% INFERRED · 0% AMBIGUOUS · INFERRED: 515 edges (avg confidence: 0.82)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Runtime Event Handling
- Weighted Deficit Scheduling
- HTTP Request Streaming
- Release Evidence Limitations
- Runtime Release Configuration
- Benchmark Campaign Execution
- Runtime Queue Tests
- Architecture Design Contracts
- Runtime Metric Snapshots
- Cancellation Retirement Tests
- Release Validation Artifacts
- Benchmark Load Generation
- Thermal Release Results
- Runtime Service Commands
- Logical KV Budgeting
- Release Statistical Analysis
- KV Growth Authorization
- Reactor Ownership Helpers
- KV Lease Conservation
- Llama Context Internals
- Runtime Header Dependencies
- Resource Soak Campaign
- Release Artifact Manifest
- Reproduction Build Environment
- Reproduction Build Environment
- Reproduction Build Environment
- HTTP Integration Tests
- Request Generation Keys
- Streaming Buffer Control
- Resource Soak Results
- Sequence Slot Retirement
- Release Dependency Environment
- Requested Runtime Configuration
- Requested Runtime Configuration
- Requested Runtime Configuration
- Requested Runtime Configuration
- Requested Runtime Configuration
- Bounded Queue Operations
- Request Record State
- Scheduler Policy Tests
- Benchmark Validity Protocol
- Benchmark Python Tests
- Runtime Integration Tests
- SSE Server Startup
- Llama Engine Dependencies
- Llama Smoke Execution
- Repository Milestone History
- Reproduction Compiler Details
- Current Source Smoke
- Batch Ticket Ownership
- Llama Batch Construction
- Class Runtime Metrics
- Terminal Request Transitions
- Clean Smoke Manifest
- Fake Engine Execution
- Adaptive Prefill Configuration
- Reproduction Commit Evidence
- Reproduction Compiler Details
- Release Comparison Gates
- Engine Result Payloads
- Request State Snapshots
- Request Admission Specification
- Release Analysis Tests
- Build Reproduction Flags
- Derived Analysis Provenance
- Benchmark Tool Hashes
- Reproduction Dependency Versions
- Reproduction Dependency Versions
- Reproduction Dependency Versions
- Reproduction Dependency Versions
- Reproduction Dependency Versions
- Deterministic Runtime Clocks
- KV Lease Operations
- Load Generator Integration
- Adaptive Prefill Traces
- Benchmark Summary Statistics
- Release Gate Statuses
- Clean Smoke Manifest
- Clean Smoke Manifest
- Clean Smoke Manifest
- Request Record Schema
- Llama Engine Configuration
- Runtime Scheduler Configuration
- Fake Service Shutdown
- Runtime Service Shutdown
- Reactor Scheduler Planning
- Work Plan Issuance
- Runtime Test Harness
- UTF8 Stream Decoding
- New Request Events
- Host RSS Drift
- Workload Arrival Configuration
- Workload Arrival Configuration
- Workload Arrival Configuration
- Workload Arrival Configuration
- Workload Arrival Configuration
- Adaptive Prefill Development
- Llama Engine Interface
- Active Ticket State
- Deadline Enforcement Tests
- Engine Capability Checks
- Engine Sequence State
- Benchmark Recorder Tests
- Engine Execution Results
- Inference Engine Contract
- KV Budget Configuration
- FIFO Request Planning
- Resource Soak Tests
- Release Secret Audit
- CUDA Build Configuration
- Thermal Validity Configuration
- Thermal Validity Configuration
- Thermal Validity Configuration
- CUDA Build Configuration
- Thermal Validity Configuration
- Sequence Clear Requests
- Work Item Specification
- Server Command Options
- Llama Context Capacity
- Release Model Identity
- Release Tag Decision
- Thermal Validity Configuration
- GPU Baseline Validation
- Backend Memory Metrics
- HTTP Completion Response
- Resource Soak Timeline
- Final KV Drainage
- Review Checkpoint Metadata
- Request Schema Definition
- Token Sampling Interface
- HTTP Test Helpers
- Release Campaign Tests
- Thermal Treatment Validity
- Pinned Backend Version
- GPU Memory Plateau
- Peak KV Accounting
- Scheduler Policy Decision
- Interactive SLO Contract
- Reproduction Model Identity
- Interactive SLO Contract
- Reproduction Model Identity
- Interactive SLO Contract
- Reproduction Model Identity
- Interactive SLO Contract
- Reproduction Model Identity
- Interactive SLO Contract
- Reproduction Model Identity
- Request Token Arrival
- Batch Completion Event
- GPU Temperature Range
- Profiling Optimization Decision
- Live Request Range
- Live Sequence Range
- Reproduction Backend Version
- Reproduction Backend Version
- Reproduction Server Identity
- Reproduction Backend Version
- Reproduction Server Identity
- Reproduction Backend Version
- Reproduction Server Identity
- Reproduction Server Identity
- Analyzer Review Tasks
- HTTP Status Schema
- Prompt Token Schema
- SSE Server Construction
- NVTX Runtime Profiling
- Pending Scheduler Commit
- Sequence Clear Events
- Fake Sequence Position
- Fake Tokenization
- Acceptance Timestamp Schema
- Requested Output Schema
- Sample Identifier Schema
- Decode Share Protection
- Server Integer Parsing
- Actual Prompt Schema
- Cancellation Flag Schema
- Completion Timestamp Schema
- Intended Arrival Schema
- Priority Class Schema
- Run Identifier Schema
- Workload Seed Schema
- SLO Outcome Schema
- TTFT Target Schema
- Burst Workload Arrivals
- Cancellation Workload Arrivals
- Deadline Workload Arrivals
- Interactive Closed Loop
- Long Context Workload
- Phase Five Workloads
- Static Prefill Default
- KV Pressure State
- Llama Reference Build
- Release Reproduction Script
- Reference Server Script
- Llama Sequence Clearing
- Mixed SLO Workload
- Long Generation Workload
- Mixed Phase Five Workload

## God Nodes (most connected - your core abstractions)
1. `RuntimeReactor` - 117 edges
2. `RuntimeService` - 56 edges
3. `RuntimeMetricsSnapshot` - 55 edges
4. `TEST()` - 52 edges
5. `TEST()` - 46 edges
6. `KvBudgetManager` - 42 edges
7. `RequestKey` - 42 edges
8. `FakeEngine` - 41 edges
9. `LlamaEngine::Impl` - 40 edges
10. `RequestRecord` - 38 edges

## Surprising Connections (you probably didn't know these)
- `cancel_before_submit_never_enters_fifo()` --calls--> `memory_stats`  [INFERRED]
  tests/runtime_test.cpp → include/inferscale/engine/fake_engine.hpp
- `sequence_clear_failure_quarantines_resource_and_stops_admission()` --calls--> `fail_next_clear`  [INFERRED]
  tests/runtime_test.cpp → include/inferscale/engine/fake_engine.hpp
- `TEST()` --calls--> `reservation_for`  [INFERRED]
  tests/policy_test.cpp → include/inferscale/runtime/kv_budget.hpp
- `TEST()` --calls--> `can_reserve_below_soft_watermark`  [INFERRED]
  tests/policy_test.cpp → include/inferscale/runtime/kv_budget.hpp
- `TEST()` --calls--> `reserve`  [INFERRED]
  tests/policy_test.cpp → include/inferscale/runtime/kv_budget.hpp

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Proposed cancellation-safe ownership** — inferscale_project_architecture_v2_02_system_architecture_runtime_reactor, inferscale_project_architecture_v2_02_system_architecture_batch_ticket, inferscale_project_architecture_v2_04_component_design_kv_budget_manager, inferscale_project_architecture_v2_02_system_architecture_work_item [EXTRACTED 1.00]
- **Same-runtime scheduling causal study** — inferscale_project_architecture_v2_06_phase_2_runtime_fifo_control, inferscale_project_architecture_v2_04_component_design_sa_wds, inferscale_project_architecture_v2_07_phase_3_kv_and_scheduling_chunked_prefill, inferscale_project_architecture_v2_04_component_design_adaptive_prefill_controller [EXTRACTED 1.00]
- **Useful service and honest benchmark evidence** — inferscale_project_architecture_v2_11_benchmarking_and_metrics_open_loop, inferscale_project_architecture_v2_11_benchmarking_and_metrics_slo_goodput, inferscale_project_architecture_v2_11_benchmarking_and_metrics_thermal_validity, readme_release_evidence [INFERRED 0.85]
- **Single-owner request execution flow** — docs_architecture_completions_endpoint, docs_architecture_runtimereactor, docs_architecture_workplan, docs_architecture_llamabatchbuilder, docs_architecture_batchticket, docs_architecture_llamaengine, docs_architecture_utf8_stream_buffer [EXTRACTED 1.00]
- **Release inference validity gates** — docs_benchmark_methodology_complete_valid_pairs, docs_benchmark_methodology_paired_trial_bootstrap, docs_benchmark_methodology_headline_p99_population, docs_benchmark_methodology_gpu_validity [EXTRACTED 1.00]
- **Logical resource conservation** — docs_kv_accounting_kvlease, docs_kv_accounting_growth_authorization, docs_kv_accounting_hard_capacity, docs_kv_accounting_pressure_states [EXTRACTED 1.00]
- **Validated software with limited performance evidence** — docs_prompt3_gates_release_policy, docs_prompt3_gates_invalid_comparison, docs_reproduction_current_source_smoke, docs_resource_soak_fifo_soak, docs_resource_soak_soak_source_binding [EXTRACTED 1.00]
- **Safety constrained token service** — docs_scheduling_sa_wds, docs_scheduling_eligibility, docs_scheduling_fairness_scope, docs_scheduling_decode_protection, docs_scheduling_chunked_prefill [EXTRACTED 1.00]
- **Evidence for retaining simpler batch planning** — docs_profiling_profile_phase4_backend_dominance, docs_profiling_profile_phase4_overlap_interpretation, docs_profiling_optimization_experiment_rejected_buffer_reuse [EXTRACTED 1.00]

## Communities (199 total, 28 thin omitted)

### Community 0 - "Runtime Event Handling"
Cohesion: 0.04
Nodes (59): id, deque, RuntimeEvent, unordered_map, RuntimeReactor, adaptive_cooldown_remaining_, adaptive_ewma_ttft_ms_, adaptive_prefill_reason_ (+51 more)

### Community 1 - "Weighted Deficit Scheduling"
Cohesion: 0.05
Nodes (51): array, ClockTime, milliseconds, optional, RequestClass, uint16_t, uint32_t, uint64_t (+43 more)

### Community 2 - "HTTP Request Streaming"
Cohesion: 0.06
Nodes (46): awaitable, co_spawn, detached, functional, ClearCommand, request, notify_state_change, redirect_error (+38 more)

### Community 3 - "Release Evidence Limitations"
Cohesion: 0.07
Nodes (49): External restart for unhealthy backend, Unbounded accepted-ID generation history, Logical KV accounting boundary, Runtime and evidence limitations, SSE piece intervals are an ITL proxy, Rejected buffer-reuse optimization experiment, Rejected LlamaBatchBuilder buffer reuse, CUDA execution and synchronization dominance (+41 more)

### Community 4 - "Runtime Release Configuration"
Cohesion: 0.04
Nodes (46): cooldown_windows, enabled_by_default, high_wait_ms, initial_tokens, low_wait_ms, maximum_tokens, minimum_tokens, step_tokens (+38 more)

### Community 5 - "Benchmark Campaign Execution"
Cohesion: 0.10
Nodes (38): datetime, Exception, hashlib, importlib_metadata, platform, main(), Popen, Run an open-loop, non-causal reference trial against pinned llama-server. (+30 more)

### Community 6 - "Runtime Queue Tests"
Cohesion: 0.21
Nodes (41): clear_sequence, fail_next, cancel, issue, make_fifo_plan, metrics, post, resolve_resource_stall (+33 more)

### Community 7 - "Architecture Design Contracts"
Cohesion: 0.08
Nodes (44): CPU correctness and hygiene CI, CMake runtime and backend targets, InferScale architectural proposal overview, Mixed traffic engineering goals, Generation-fenced BatchTicket retirement, Proposed runtime architecture, Single-owner runtime reactor, Exact prefill and decode WorkItem ranges (+36 more)

### Community 8 - "Runtime Metric Snapshots"
Cohesion: 0.05
Nodes (44): vector, RuntimeMetricsSnapshot, adaptive_prefill_enabled, adaptive_prefill_reason, adaptive_prefill_trace, admission_ready, authorized_kv_tokens, available_kv_tokens (+36 more)

### Community 9 - "Cancellation Retirement Tests"
Cohesion: 0.08
Nodes (37): FakeEngine, capabilities, condition_, decode_delay_, execute, fail_clear_, fail_next_clear, inflight_ (+29 more)

### Community 10 - "Release Validation Artifacts"
Cohesion: 0.06
Nodes (36): Release comparison figures not generated, Non-comparative soak and validity figures, No comparison-eligible paired trial, Harness-only reproduction smokes, Local raw benchmark evidence, 85 C thermal exclusion, Release artifact SHA256 index, Clean detached-worktree reproduction (+28 more)

### Community 11 - "Benchmark Load Generation"
Cohesion: 0.11
Nodes (27): AsyncClient, build_arrivals(), choose_request(), completed_metrics_reached(), fetch_final_server_metrics(), fetch_server_metrics(), histogram_p99_upper_bound(), main() (+19 more)

### Community 12 - "Thermal Release Results"
Cohesion: 0.06
Nodes (33): complete_pairs, invalid_config_mismatch, invalid_thermal, rows, runner_valid, valid_standalone, complete_pairs, invalid_config_mismatch (+25 more)

### Community 13 - "Runtime Service Commands"
Cohesion: 0.06
Nodes (30): EngineCommand, ExecuteCommand, ticket, uint64_t, unique_ptr, RuntimeService, backend_name_, cancel (+22 more)

### Community 14 - "Logical KV Budgeting"
Cohesion: 0.08
Nodes (21): uint64_t, unordered_map, KvBudgetManager, abandon, authorize_growth, can_reserve_below_soft_watermark, commit_growth, config_ (+13 more)

### Community 15 - "Release Statistical Analysis"
Cohesion: 0.18
Nodes (30): bootstrap_median_interval(), evidence_path(), exit_explanation(), generate_figures(), gpu_power_configuration_errors(), group_key(), line_figure(), load_trials() (+22 more)

### Community 16 - "KV Growth Authorization"
Cohesion: 0.13
Nodes (30): add_fits, can_reserve, charged, owns, unregister_lease, KvPressureState, uint16_t, uint32_t (+22 more)

### Community 17 - "Reactor Ownership Helpers"
Cohesion: 0.12
Nodes (29): assert_owner, handle, inflight_ticket_count, ClockTime, milliseconds, optional, RuntimeEvent, shared_ptr (+21 more)

### Community 18 - "KV Lease Conservation"
Cohesion: 0.08
Nodes (28): Memory Pressure workload, Slow Reader workload, Direct pinned libllama, External llama-server comparison, Generation fencing and clear handshake, Single-owner reactor, Clear acknowledgement authorizes release, Logical sequence-token accounting (+20 more)

### Community 19 - "Llama Context Internals"
Cohesion: 0.07
Nodes (28): int8_t, llama_context, llama_model, llama_sampler, int32_t, llama_pos, llama_seq_id, llama_token (+20 more)

### Community 20 - "Runtime Header Dependencies"
Cohesion: 0.16
Nodes (14): chrono, compare, cstdint, gtest, classify_engine_error(), FailureClass, optional, unordered_map (+6 more)

### Community 21 - "Resource Soak Campaign"
Cohesion: 0.13
Nodes (21): httpx, os, re, Stop a child before continuing after an invalid load or thermal abort., terminate_process(), warmup(), blocks_are_valid(), event_queue_rejections() (+13 more)

### Community 22 - "Release Artifact Manifest"
Cohesion: 0.08
Nodes (25): analysis_intervals, analysis_per_trial, analysis_source_commit, analysis_summary, benchmark_methodology, benchmark_summary, confidence_intervals, current_source_validation (+17 more)

### Community 23 - "Reproduction Build Environment"
Cohesion: 0.08
Nodes (26): path, version, CMAKE_BUILD_TYPE, CMAKE_CUDA_ARCHITECTURES, CMAKE_CUDA_FLAGS, CMAKE_CXX_FLAGS, GGML_CUDA, INFERSCALE_ENABLE_NVTX (+18 more)

### Community 24 - "Reproduction Build Environment"
Cohesion: 0.08
Nodes (26): path, version, CMAKE_BUILD_TYPE, CMAKE_CUDA_ARCHITECTURES, CMAKE_CUDA_FLAGS, CMAKE_CXX_FLAGS, GGML_CUDA, INFERSCALE_ENABLE_NVTX (+18 more)

### Community 25 - "Reproduction Build Environment"
Cohesion: 0.08
Nodes (26): path, version, CMAKE_BUILD_TYPE, CMAKE_CUDA_ARCHITECTURES, CMAKE_CUDA_FLAGS, CMAKE_CXX_FLAGS, GGML_CUDA, INFERSCALE_ENABLE_NVTX (+18 more)

### Community 26 - "HTTP Integration Tests"
Cohesion: 0.17
Nodes (20): charconv, connect, core, csignal, cstdlib, fake_server, http, io_context (+12 more)

### Community 27 - "Request Generation Keys"
Cohesion: 0.11
Nodes (24): CancelEvent, key, DeadlineEvent, key, StreamWritableEvent, key, RequestKey, generation (+16 more)

### Community 28 - "Streaming Buffer Control"
Cohesion: 0.13
Nodes (15): deque, mutex, size_t, uint32_t, StreamBuffer, chunks_, end_, mutex_ (+7 more)

### Community 29 - "Resource Soak Results"
Cohesion: 0.08
Nodes (25): resource_soak, accepted, avoidable_oom_markers, campaign_id, cancelled, clean_shutdown, compact_timeline, completed (+17 more)

### Community 30 - "Sequence Slot Retirement"
Cohesion: 0.10
Nodes (24): optional, SequenceId, uint32_t, unordered_map, vector, SequenceAllocator, allocate, begin_clear (+16 more)

### Community 31 - "Release Dependency Environment"
Cohesion: 0.08
Nodes (24): Boost, GoogleTest, nlohmann_json, environment, cmake, compiler, compute_capability, cpu (+16 more)

### Community 32 - "Requested Runtime Configuration"
Cohesion: 0.08
Nodes (24): requested_configuration, batch_threads, batch_tokens, chunk_tokens, context_per_sequence, credit_cap_tokens, generation_reserve_tokens, gpu_layers (+16 more)

### Community 33 - "Requested Runtime Configuration"
Cohesion: 0.08
Nodes (24): requested_configuration, batch_threads, batch_tokens, chunk_tokens, context_per_sequence, credit_cap_tokens, generation_reserve_tokens, gpu_layers (+16 more)

### Community 34 - "Requested Runtime Configuration"
Cohesion: 0.08
Nodes (24): requested_configuration, batch_threads, batch_tokens, chunk_tokens, context_per_sequence, credit_cap_tokens, generation_reserve_tokens, gpu_layers (+16 more)

### Community 35 - "Requested Runtime Configuration"
Cohesion: 0.08
Nodes (24): requested_configuration, batch_threads, batch_tokens, chunk_tokens, context_per_sequence, credit_cap_tokens, generation_reserve_tokens, gpu_layers (+16 more)

### Community 36 - "Requested Runtime Configuration"
Cohesion: 0.08
Nodes (24): requested_configuration, batch_threads, batch_tokens, chunk_tokens, context_per_sequence, credit_cap_tokens, generation_reserve_tokens, gpu_layers (+16 more)

### Community 37 - "Bounded Queue Operations"
Cohesion: 0.12
Nodes (16): BoundedQueue, closed_, mutex_, not_empty_, not_full_, queue_, condition_variable, deque (+8 more)

### Community 38 - "Request Record State"
Cohesion: 0.08
Nodes (24): ClockTime, optional, RequestRecord, arrival_order, arrived_at, backpressured, clear_queued, first_output_at (+16 more)

### Community 39 - "Scheduler Policy Tests"
Cohesion: 0.09
Nodes (23): AgeBucketsAreCappedBeforeStableTieBreakers, AgingAndDeadlineOrderingAreStableWithinAClass, BoundedCreditsCarryWeightedServiceAcrossSmallEpochs, ConservativeCapacityLeavesTheDeclaredReserve, DecodeProtectionLeavesFinitePrefillProgress, DuplicateIdentityAndMoveCannotDuplicateAReservation, GrowthCanUseOnlyActuallyUnchargedHardHeadroom, IdleClassDropsStaleCreditsAndDoesNotStallActiveClass (+15 more)

### Community 40 - "Benchmark Validity Protocol"
Cohesion: 0.09
Nodes (23): benchmark_protocol, analysis_source_commit, arrival_mode, bootstrap, cool_start_rule, max_sender_lateness_ms, mixed_slo, paired_seed_scheme (+15 more)

### Community 41 - "Benchmark Python Tests"
Cohesion: 0.15
Nodes (15): asyncio, Run an offered-load sweep and retain a compact CSV curve., contextlib, csv, io, Json, pathlib, socket (+7 more)

### Community 42 - "Runtime Integration Tests"
Cohesion: 0.14
Nodes (20): cancellation_fences_stale_generation_and_discards_output, completion_post_waits_for_bounded_queue_space, adaptive_prefill_trace, make_configured_plan, prefill_epoch_budget, long_pieces_use_engine_bound_for_reservation_and_backpressure, RuntimeConcurrency, RuntimeIntegration (+12 more)

### Community 43 - "SSE Server Startup"
Cohesion: 0.11
Nodes (19): enable_shared_from_this<SseServer>, executor_type, shared_ptr, size_t, strand, SseServer, accept_next, accept_strand_ (+11 more)

### Community 44 - "Llama Engine Dependencies"
Cohesion: 0.15
Nodes (15): algorithm, climits, cstddef, filesystem, ggml_backend, condition_variable, mutex, StreamRead (+7 more)

### Community 45 - "Llama Smoke Execution"
Cohesion: 0.13
Nodes (16): cstdio, llama_batch, size_t, LlamaBatchBuilder::populate_llama_batch(), BackendGuard, BatchGuard, value, int32_t (+8 more)

### Community 46 - "Repository Milestone History"
Cohesion: 0.11
Nodes (18): milestone-1-baseline, milestone-2-safe-runtime, milestone-3-adaptive-scheduling, milestone-4-profiled-optimization, repository, analysis_hardening_commit, branch, campaign_harness_commit (+10 more)

### Community 47 - "Reproduction Compiler Details"
Cohesion: 0.11
Nodes (18): path, version, environment, cmake, cmake_preset_equivalent, cmake_source_commit, cmake_source_directory, cmake_source_tracked_diff_sha256 (+10 more)

### Community 48 - "Current Source Smoke"
Cohesion: 0.12
Nodes (16): backend, commit, name, version, campaign_id, campaign_purpose, created_at_utc, gpu_inventory (+8 more)

### Community 49 - "Batch Ticket Ownership"
Cohesion: 0.15
Nodes (13): DecodePositionIsPromptLengthAndDuplicateEntriesAreRejected, BatchTicket, armed_, entries_, retired_, uint64_t, vector, LlamaBatchBuilderTest (+5 more)

### Community 50 - "Llama Batch Construction"
Cohesion: 0.15
Nodes (16): int32_t, optional, SequenceId, size_t, TokenId, uint64_t, LlamaBatchPlan, logits_count (+8 more)

### Community 51 - "Class Runtime Metrics"
Cohesion: 0.12
Nodes (17): ClassRuntimeMetrics, admitted, completed, hard_limit_aborted, maximum_ready_wait_ms, rejected, rejected_invalid, rejected_kv_capacity (+9 more)

### Community 52 - "Terminal Request Transitions"
Cohesion: 0.19
Nodes (16): maybe_queue_clear, note_terminal, observe_adaptive_prefill, lifecycle, RequestState, legal_transition(), terminal_intent(), transition() (+8 more)

### Community 53 - "Clean Smoke Manifest"
Cohesion: 0.12
Nodes (15): campaign_id, created_at_utc, gpu_inventory, policies, fifo, repository_commit, server, help_sha256 (+7 more)

### Community 54 - "Fake Engine Execution"
Cohesion: 0.14
Nodes (11): cctype, fake_engine, FailureClass, milliseconds, size_t, FakeEngine::execute(), FakeEngine::fail_next(), FakeEngine::FakeEngine() (+3 more)

### Community 55 - "Adaptive Prefill Configuration"
Cohesion: 0.12
Nodes (15): AdaptivePrefillConfig, cooldown_windows, enabled, high_decode_backlog, high_executor_idle_basis_points, high_interactive_wait_ms, initial_tokens, low_decode_backlog (+7 more)

### Community 56 - "Reproduction Commit Evidence"
Cohesion: 0.13
Nodes (15): reproduction, analysis_hardening_commit, analysis_source_commit, clean_worktree, current_source_commit, current_source_validation, current_status, historical_smoke_source_commit (+7 more)

### Community 57 - "Reproduction Compiler Details"
Cohesion: 0.13
Nodes (15): path, version, environment, cmake, cmake_preset_equivalent, compiler, cpu, cuda_compiler (+7 more)

### Community 58 - "Release Comparison Gates"
Cohesion: 0.14
Nodes (15): Complete valid paired treatment gate, Future publication campaign, Thermal and power validity gates, Headline p99 population gate, Paired trial bootstrap, Protocol provenance limitations, Enforced power-limit mismatch, Release arm validity (+7 more)

### Community 59 - "Engine Result Payloads"
Cohesion: 0.14
Nodes (15): GrowthResolution, EngineOutput, end_of_sequence, piece, EngineResult, committed_kv_growth, failure, growth_resolution (+7 more)

### Community 60 - "Request State Snapshots"
Cohesion: 0.13
Nodes (15): milliseconds, RequestClass, RequestState, RequestSnapshot, estimated_committed_tokens, generated_tokens, inflight_tickets, key (+7 more)

### Community 61 - "Request Admission Specification"
Cohesion: 0.13
Nodes (15): ClockTime, milliseconds, RequestClass, shared_ptr, TokenId, RequestSpec, deadline, e2e_slo (+7 more)

### Community 62 - "Release Analysis Tests"
Cohesion: 0.15
Nodes (3): paired_trials(), ReleaseAnalysisTests, trial()

### Community 63 - "Build Reproduction Flags"
Cohesion: 0.14
Nodes (14): build, clean_build_dir, clean_server_sha256, flags, preset_equivalent, source_candidate_commit, BUILD_TESTING, CMAKE_BUILD_TYPE (+6 more)

### Community 64 - "Derived Analysis Provenance"
Cohesion: 0.14
Nodes (14): derived_analysis, analysis_code_sha256, analysis_source_commit, comparison_eligible_pairs, effect_rows, indexed_arms, interval_rows, intervals (+6 more)

### Community 65 - "Benchmark Tool Hashes"
Cohesion: 0.14
Nodes (14): tool_hashes, analysis_commit, analysis_sha256, load_generator_commit, load_generator_sha256, python_dependency_lock_sha256, release_campaign_plan_sha256, release_campaign_runner_sha256 (+6 more)

### Community 66 - "Reproduction Dependency Versions"
Cohesion: 0.14
Nodes (14): Boost, GoogleTest, nlohmann/json, httpx, PyYAML, tooling, analysis_commit, analysis_sha256 (+6 more)

### Community 67 - "Reproduction Dependency Versions"
Cohesion: 0.14
Nodes (14): Boost, GoogleTest, nlohmann/json, httpx, PyYAML, tooling, analysis_commit, analysis_sha256 (+6 more)

### Community 68 - "Reproduction Dependency Versions"
Cohesion: 0.14
Nodes (14): Boost, GoogleTest, nlohmann/json, httpx, PyYAML, tooling, analysis_commit, analysis_sha256 (+6 more)

### Community 69 - "Reproduction Dependency Versions"
Cohesion: 0.14
Nodes (14): Boost, GoogleTest, nlohmann/json, httpx, PyYAML, tooling, analysis_commit, analysis_sha256 (+6 more)

### Community 70 - "Reproduction Dependency Versions"
Cohesion: 0.14
Nodes (14): Boost, GoogleTest, nlohmann/json, httpx, PyYAML, tooling, analysis_commit, analysis_sha256 (+6 more)

### Community 71 - "Deterministic Runtime Clocks"
Cohesion: 0.15
Nodes (9): atomic, Clock, now, atomic, ClockTime, duration, rep, ManualClock (+1 more)

### Community 72 - "KV Lease Operations"
Cohesion: 0.16
Nodes (9): can_authorize, register_lease, update_peak, KvLease, lease_id_, owner_, KvBudgetManager::authorize_growth(), KvBudgetManager::commit_growth() (+1 more)

### Community 73 - "Load Generator Integration"
Cohesion: 0.22
Nodes (3): StreamReader, StreamWriter, LoadGeneratorIntegrationTests

### Community 74 - "Adaptive Prefill Traces"
Cohesion: 0.15
Nodes (13): AdaptivePrefillReason, AdaptivePrefillTraceEntry, batch_id, budget_after, budget_before, decode_backlog, decode_tokens, executor_idle_basis_points (+5 more)

### Community 75 - "Benchmark Summary Statistics"
Cohesion: 0.24
Nodes (11): argparse, main(), latency_percentiles(), percentile(), Any, Small dependency-free statistics used by the benchmark recorder., self_check(), summarize() (+3 more)

### Community 76 - "Release Gate Statuses"
Cohesion: 0.15
Nodes (13): gate_statuses, A, B, C, D, E, F, G (+5 more)

### Community 77 - "Clean Smoke Manifest"
Cohesion: 0.15
Nodes (12): campaign_id, campaign_purpose, created_at_utc, gpu_inventory, policies, fifo, repository_commit, trial_orders (+4 more)

### Community 78 - "Clean Smoke Manifest"
Cohesion: 0.15
Nodes (12): campaign_id, campaign_purpose, created_at_utc, gpu_inventory, policies, fifo, repository_commit, trial_orders (+4 more)

### Community 79 - "Clean Smoke Manifest"
Cohesion: 0.15
Nodes (12): campaign_id, campaign_purpose, created_at_utc, gpu_inventory, policies, fifo, repository_commit, trial_orders (+4 more)

### Community 80 - "Request Record Schema"
Cohesion: 0.15
Nodes (13): type, type, type, type, properties, actual_output_tokens, e2e_slo_ms, error_class (+5 more)

### Community 81 - "Llama Engine Configuration"
Cohesion: 0.15
Nodes (13): int32_t, string, uint32_t, LlamaEngineConfig, batch_threads, batch_tokens, context_per_sequence, gpu_layers (+5 more)

### Community 82 - "Runtime Scheduler Configuration"
Cohesion: 0.15
Nodes (13): RuntimeConfig, adaptive_prefill, admission_generation_reserve_tokens, hard_usable_kv_tokens, high_pressure_basis_points, kv_safety_margin_tokens, max_prefill_tokens_per_epoch, sawds (+5 more)

### Community 83 - "Fake Service Shutdown"
Cohesion: 0.18
Nodes (8): FakeRuntimeService, FailureClass, milliseconds, shared_ptr, ShutdownTimer, time_point, main(), monitor_shutdown()

### Community 84 - "Runtime Service Shutdown"
Cohesion: 0.20
Nodes (12): executor_loop, request_shutdown, runtime_loop, wait_for_shutdown, RuntimeService::RuntimeService(), shared_ptr, ShutdownTimer, time_point (+4 more)

### Community 85 - "Reactor Scheduler Planning"
Cohesion: 0.21
Nodes (12): collect_scheduler_candidates, limit_plan_to_hard_growth, nanoseconds, RequestClass, uint32_t, nonnegative_nanoseconds(), RuntimeReactor::make_configured_plan(), RuntimeReactor::make_fifo_plan() (+4 more)

### Community 86 - "Work Plan Issuance"
Cohesion: 0.24
Nodes (12): is_ready, service_class_index, WorkPlan, items, sequence_count, total_tokens, valid_work_plan(), RuntimeReactor::collect_scheduler_candidates() (+4 more)

### Community 87 - "Runtime Test Harness"
Cohesion: 0.25
Nodes (6): api::RuntimeService, milliseconds, HighCapacityRuntime, main(), ResumeEngineOnExit, armed

### Community 88 - "UTF8 Stream Decoding"
Cohesion: 0.31
Nodes (6): string, string_view, Utf8StreamDecoder, pending_, string_view, valid_utf8()

### Community 89 - "New Request Events"
Cohesion: 0.20
Nodes (10): AdmissionResult, atomic, shared_ptr, uint32_t, NewRequestEvent, admission, generation_hint, output_stream (+2 more)

### Community 90 - "Host RSS Drift"
Cohesion: 0.20
Nodes (10): rss_kib, drift_kib, end, final_5m_slope_mib_per_min, final_third_median, frozen_drift_allowance_kib, peak, stable_plateau (+2 more)

### Community 91 - "Workload Arrival Configuration"
Cohesion: 0.20
Nodes (10): load, arrival_mode, base_seed, concurrency, generation_reserve_tokens, max_sender_lateness_ms, offered_rates_rps, prefill_chunk_tokens (+2 more)

### Community 92 - "Workload Arrival Configuration"
Cohesion: 0.20
Nodes (10): load, arrival_mode, base_seed, concurrency, generation_reserve_tokens, max_sender_lateness_ms, offered_rates_rps, prefill_chunk_tokens (+2 more)

### Community 93 - "Workload Arrival Configuration"
Cohesion: 0.20
Nodes (10): load, arrival_mode, base_seed, concurrency, generation_reserve_tokens, max_sender_lateness_ms, offered_rates_rps, prefill_chunk_tokens (+2 more)

### Community 94 - "Workload Arrival Configuration"
Cohesion: 0.20
Nodes (10): load, arrival_mode, base_seed, concurrency, generation_reserve_tokens, max_sender_lateness_ms, offered_rates_rps, prefill_chunk_tokens (+2 more)

### Community 95 - "Workload Arrival Configuration"
Cohesion: 0.20
Nodes (10): load, arrival_mode, base_seed, concurrency, generation_reserve_tokens, max_sender_lateness_ms, offered_rates_rps, prefill_chunk_tokens (+2 more)

### Community 96 - "Adaptive Prefill Development"
Cohesion: 0.20
Nodes (10): Phase3 Mixed workload, Bounded adaptive prefill, Adaptive development tradeoff, Hysteresis and cooldown, Interactive pressure signals, Static fallback, Same-runtime ablation matrix, Class token-service weights (+2 more)

### Community 97 - "Llama Engine Interface"
Cohesion: 0.20
Nodes (9): Impl, unique_ptr, LlamaEngine, capabilities, clear_sequence, execute, impl_, memory_stats (+1 more)

### Community 98 - "Active Ticket State"
Cohesion: 0.20
Nodes (9): SequenceId, shared_ptr, TokenId, uint32_t, TicketEntry, max_output_tokens, prompt_token_ids, sequence (+1 more)

### Community 99 - "Deadline Enforcement Tests"
Cohesion: 0.27
Nodes (8): SteadyClock, expire_deadlines, latest_key, seal_events, shutdown_complete, deadline_prevents_submission_and_shutdown_releases_waiters(), deadlines_are_rechecked_at_ticket_issue_and_completion(), impossible_context_is_rejected_before_sequence_allocation()

### Community 100 - "Engine Capability Checks"
Cohesion: 0.20
Nodes (10): EngineCapabilities, context_limit, cuda_enabled, max_batch_tokens, max_output_piece_bytes, max_prefill_chunk_tokens, max_sequences, resolved_total_context (+2 more)

### Community 101 - "Engine Sequence State"
Cohesion: 0.20
Nodes (10): shared_ptr, SequenceState, emitted, has_pending, key, pending_eog, pending_token, prefetched (+2 more)

### Community 103 - "Engine Execution Results"
Cohesion: 0.28
Nodes (6): LlamaBatchBuilder, populate_llama_batch, FailureClass, string, failure(), LlamaEngine::execute()

### Community 104 - "Inference Engine Contract"
Cohesion: 0.22
Nodes (8): InferenceEngine, capabilities, clear_sequence, execute, memory_stats, tokenize, unique_ptr, require_capabilities()

### Community 105 - "KV Budget Configuration"
Cohesion: 0.22
Nodes (9): uint16_t, KvBudgetConfig, hard_usable_capacity, high_pressure_basis_points, safety_margin_tokens, soft_pressure_basis_points, KvBudgetManager::KvBudgetManager(), budget_config() (+1 more)

### Community 106 - "FIFO Request Planning"
Cohesion: 0.25
Nodes (9): fifo_plan(), uint32_t, vector, WorkKind, ReadyRequest, key, kind, token_begin (+1 more)

### Community 108 - "Release Secret Audit"
Cohesion: 0.25
Nodes (8): security, api_keys_found, baseline_note, model_weights_committed, reviewed_nonsecret_baseline_entries, scanner_version, secret_scan, tracked_file_scan_status

### Community 109 - "CUDA Build Configuration"
Cohesion: 0.25
Nodes (8): CMAKE_BUILD_TYPE, CMAKE_CUDA_ARCHITECTURES, CMAKE_CUDA_FLAGS, CMAKE_CXX_FLAGS, GGML_CUDA, INFERSCALE_ENABLE_NVTX, INFERSCALE_WITH_LLAMA, cuda_build

### Community 110 - "Thermal Validity Configuration"
Cohesion: 0.25
Nodes (8): validity, cooldown_timeout_s, max_gpu_temperature_c, max_paired_start_temperature_delta_c, max_sender_lateness_ms, max_start_temperature_c, minimum_gpu_metrics_coverage, no_thermal_throttle

### Community 111 - "Thermal Validity Configuration"
Cohesion: 0.25
Nodes (8): validity, cooldown_timeout_s, max_gpu_temperature_c, max_paired_start_temperature_delta_c, max_sender_lateness_ms, max_start_temperature_c, minimum_gpu_metrics_coverage, no_thermal_throttle

### Community 112 - "Thermal Validity Configuration"
Cohesion: 0.25
Nodes (8): validity, cooldown_timeout_s, max_gpu_temperature_c, max_paired_start_temperature_delta_c, max_sender_lateness_ms, max_start_temperature_c, minimum_gpu_metrics_coverage, no_thermal_throttle

### Community 113 - "CUDA Build Configuration"
Cohesion: 0.25
Nodes (8): CMAKE_BUILD_TYPE, CMAKE_CUDA_ARCHITECTURES, CMAKE_CUDA_FLAGS, CMAKE_CXX_FLAGS, GGML_CUDA, INFERSCALE_ENABLE_NVTX, INFERSCALE_WITH_LLAMA, cuda_build

### Community 114 - "Thermal Validity Configuration"
Cohesion: 0.25
Nodes (8): validity, cooldown_timeout_s, max_gpu_temperature_c, max_paired_start_temperature_delta_c, max_sender_lateness_ms, max_start_temperature_c, minimum_gpu_metrics_coverage, no_thermal_throttle

### Community 115 - "Sequence Clear Requests"
Cohesion: 0.25
Nodes (8): ClearRequest, key, sequence, SequenceId, vector, RuntimeReactor::adaptive_prefill_trace(), RuntimeReactor::limit_plan_to_hard_growth(), RuntimeReactor::take_clear_requests()

### Community 116 - "Work Item Specification"
Cohesion: 0.25
Nodes (7): WorkKind, WorkItem, expected_kv_growth, key, kind, token_begin, token_count

### Community 117 - "Server Command Options"
Cohesion: 0.25
Nodes (8): string, uint32_t, Options, engine, host, http_threads, model, runtime

### Community 118 - "Llama Context Capacity"
Cohesion: 0.29
Nodes (5): acquire_backend(), checked_total_context(), uint32_t, cuda_backend_available(), release_backend()

### Community 119 - "Release Model Identity"
Cohesion: 0.29
Nodes (7): model, bytes, identifier, path, quantization, sha256, tracked_in_git

### Community 120 - "Release Tag Decision"
Cohesion: 0.29
Nodes (7): release_tag, created, decision, meaning, name, remote_push, status

### Community 121 - "Thermal Validity Configuration"
Cohesion: 0.29
Nodes (7): validity, cooldown_timeout_s, max_gpu_temperature_c, max_sender_lateness_ms, max_start_temperature_c, minimum_gpu_metrics_coverage, no_thermal_throttle

### Community 122 - "GPU Baseline Validation"
Cohesion: 0.29
Nodes (7): Intended-arrival baseline sweep, Real GPU baseline validation, RTX 4060 Laptop GPU, TinyLlama Q4_K_M, Client-clock latency contract, Coordinated-omission-safe arrivals, SLO goodput denominator

### Community 123 - "Backend Memory Metrics"
Cohesion: 0.29
Nodes (7): BackendMemoryStats, estimated_committed_tokens, live_sequences, uint32_t, uint64_t, FakeEngine::memory_stats(), LlamaEngine::memory_stats()

### Community 124 - "HTTP Completion Response"
Cohesion: 0.38
Nodes (7): complete(), completion_body(), response, string, string_body, uint16_t, uint32_t

### Community 125 - "Resource Soak Timeline"
Cohesion: 0.40
Nodes (6): FIFO resource soak timeline, Late GPU memory plateau, Live request and sequence drainage, Logical KV charge spikes, Upward host RSS trend, GPU temperature below thermal stop

### Community 126 - "Final KV Drainage"
Cohesion: 0.33
Nodes (6): authorized_tokens, available_sequences, charged_tokens, committed_tokens, soft_reserved_tokens, kv_end

### Community 127 - "Review Checkpoint Metadata"
Cohesion: 0.33
Nodes (6): review, authentication, checkpoints, claude_code_version, provider, provider_recovery

### Community 128 - "Request Schema Definition"
Cohesion: 0.33
Nodes (5): additionalProperties, required, $schema, title, type

### Community 129 - "Token Sampling Interface"
Cohesion: 0.33
Nodes (5): size_t, string_view, TokenId, vector, LlamaEngine::tokenize()

### Community 130 - "HTTP Test Helpers"
Cohesion: 0.53
Nodes (6): response, string, string_body, uint16_t, get(), post()

### Community 132 - "Thermal Treatment Validity"
Cohesion: 0.40
Nodes (5): No eligible paired comparisons, Treatment-arm validity composition, 12 RPS fallback, 16 RPS interactive primary, 8 RPS mixed SLO

### Community 133 - "Pinned Backend Version"
Cohesion: 0.40
Nodes (5): backend, commit, name, pin_file, version

### Community 134 - "GPU Memory Plateau"
Cohesion: 0.40
Nodes (5): end, final_10m_range, peak, start, gpu_memory_mib

### Community 135 - "Peak KV Accounting"
Cohesion: 0.40
Nodes (5): authorized_tokens, charged_tokens, committed_tokens, soft_reserved_tokens, kv_peak

### Community 136 - "Scheduler Policy Decision"
Cohesion: 0.40
Nodes (5): policy_decision, adaptive, reason, recommended_default, sawds

### Community 137 - "Interactive SLO Contract"
Cohesion: 0.40
Nodes (5): deadline_ms, e2e_ms, ttft_ms, slo_contract, interactive

### Community 138 - "Reproduction Model Identity"
Cohesion: 0.40
Nodes (5): model, bytes, path, quantization, sha256

### Community 139 - "Interactive SLO Contract"
Cohesion: 0.40
Nodes (5): deadline_ms, e2e_ms, ttft_ms, slo_contract, interactive

### Community 140 - "Reproduction Model Identity"
Cohesion: 0.40
Nodes (5): model, bytes, path, quantization, sha256

### Community 141 - "Interactive SLO Contract"
Cohesion: 0.40
Nodes (5): deadline_ms, e2e_ms, ttft_ms, slo_contract, interactive

### Community 142 - "Reproduction Model Identity"
Cohesion: 0.40
Nodes (5): model, bytes, path, quantization, sha256

### Community 143 - "Interactive SLO Contract"
Cohesion: 0.40
Nodes (5): deadline_ms, e2e_ms, ttft_ms, slo_contract, interactive

### Community 144 - "Reproduction Model Identity"
Cohesion: 0.40
Nodes (5): model, bytes, path, quantization, sha256

### Community 145 - "Interactive SLO Contract"
Cohesion: 0.40
Nodes (5): deadline_ms, e2e_ms, ttft_ms, slo_contract, interactive

### Community 146 - "Reproduction Model Identity"
Cohesion: 0.40
Nodes (5): model, bytes, path, quantization, sha256

### Community 147 - "Request Token Arrival"
Cohesion: 0.40
Nodes (5): type, token_arrivals_ns, description, items, type

### Community 148 - "Batch Completion Event"
Cohesion: 0.40
Nodes (5): BatchCompletedEvent, backend_duration, result, ticket, nanoseconds

### Community 149 - "GPU Temperature Range"
Cohesion: 0.50
Nodes (4): end, peak, start, gpu_temperature_c

### Community 150 - "Profiling Optimization Decision"
Cohesion: 0.50
Nodes (4): profiling, conclusion, custom_cuda_optimization_retained, reuse_prompt2_evidence

### Community 151 - "Live Request Range"
Cohesion: 0.50
Nodes (4): end, peak, start, requests_start_peak_end

### Community 152 - "Live Sequence Range"
Cohesion: 0.50
Nodes (4): sequences_start_peak_end, end, peak, start

### Community 153 - "Reproduction Backend Version"
Cohesion: 0.50
Nodes (4): backend, commit, name, version

### Community 154 - "Reproduction Backend Version"
Cohesion: 0.50
Nodes (4): backend, commit, name, version

### Community 155 - "Reproduction Server Identity"
Cohesion: 0.50
Nodes (4): server, help_sha256, path, sha256

### Community 156 - "Reproduction Backend Version"
Cohesion: 0.50
Nodes (4): backend, commit, name, version

### Community 157 - "Reproduction Server Identity"
Cohesion: 0.50
Nodes (4): server, help_sha256, path, sha256

### Community 158 - "Reproduction Backend Version"
Cohesion: 0.50
Nodes (4): backend, commit, name, version

### Community 159 - "Reproduction Server Identity"
Cohesion: 0.50
Nodes (4): server, help_sha256, path, sha256

### Community 160 - "Reproduction Server Identity"
Cohesion: 0.50
Nodes (4): server, help_sha256, path, sha256

### Community 161 - "Analyzer Review Tasks"
Cohesion: 0.50
Nodes (4): Analyzer material-finding follow-up task, Planned treatment pair preservation, Portable summary-path resolution, Benchmark runner and analyzer review task

### Community 162 - "HTTP Status Schema"
Cohesion: 0.50
Nodes (4): maximum, minimum, type, http_status_code

### Community 163 - "Prompt Token Schema"
Cohesion: 0.50
Nodes (4): description, minimum, type, prompt_tokens

### Community 164 - "SSE Server Construction"
Cohesion: 0.50
Nodes (4): endpoint, io_context, size_t, SseServer::SseServer()

### Community 166 - "Pending Scheduler Commit"
Cohesion: 0.50
Nodes (4): uint64_t, PendingSchedulerCommit, credits_after, plan

### Community 167 - "Sequence Clear Events"
Cohesion: 0.50
Nodes (4): SequenceClearedEvent, key, succeeded, RuntimeReactor::process_clear()

### Community 168 - "Fake Sequence Position"
Cohesion: 0.50
Nodes (4): SequenceId, uint32_t, FakeEngine::clear_sequence(), FakeEngine::sequence_position()

### Community 169 - "Fake Tokenization"
Cohesion: 0.50
Nodes (4): string_view, TokenId, vector, FakeEngine::tokenize()

### Community 170 - "Acceptance Timestamp Schema"
Cohesion: 0.67
Nodes (3): description, type, accepted_ns

### Community 171 - "Requested Output Schema"
Cohesion: 0.67
Nodes (3): requested_output_tokens, minimum, type

### Community 172 - "Sample Identifier Schema"
Cohesion: 0.67
Nodes (3): sample_id, minimum, type

### Community 173 - "Decode Share Protection"
Cohesion: 0.67
Nodes (3): Decode share protection, FIFO default policy, SLO-aware weighted deficit scheduling

### Community 174 - "Server Integer Parsing"
Cohesion: 0.67
Nodes (3): Integer, string_view, parse_integer()

## Knowledge Gaps
- **1267 isolated node(s):** `schema_version`, `status`, `generated_at_utc`, `root`, `branch` (+1262 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1570 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **28 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `RuntimeReactor` connect `Runtime Event Handling` to `Runtime Queue Tests`, `Runtime Metric Snapshots`, `Cancellation Retirement Tests`, `Runtime Service Commands`, `Logical KV Budgeting`, `Reactor Ownership Helpers`, `Runtime Header Dependencies`, `Request Generation Keys`, `Sequence Slot Retirement`, `Bounded Queue Operations`, `Request Record State`, `Pending Scheduler Commit`, `Runtime Integration Tests`, `Class Runtime Metrics`, `Terminal Request Transitions`, `Adaptive Prefill Configuration`, `KV Pressure State`, `Deterministic Runtime Clocks`, `Adaptive Prefill Traces`, `Runtime Scheduler Configuration`, `Reactor Scheduler Planning`, `Work Plan Issuance`, `New Request Events`, `Active Ticket State`, `Deadline Enforcement Tests`, `Sequence Clear Requests`?**
  _High betweenness centrality (0.066) - this node is a cross-community bridge._
- **Why does `RuntimeService` connect `Runtime Service Commands` to `Runtime Event Handling`, `HTTP Request Streaming`, `Deadline Enforcement Tests`, `Engine Capability Checks`, `Bounded Queue Operations`, `SSE Server Construction`, `Deterministic Runtime Clocks`, `Inference Engine Contract`, `Runtime Metric Snapshots`, `SSE Server Startup`, `Llama Engine Dependencies`, `Fake Service Shutdown`, `Runtime Service Shutdown`, `HTTP Integration Tests`?**
  _High betweenness centrality (0.037) - this node is a cross-community bridge._
- **Why does `RequestKey` connect `Request Generation Keys` to `Runtime Event Handling`, `Weighted Deficit Scheduling`, `Runtime Queue Tests`, `Logical KV Budgeting`, `KV Growth Authorization`, `Reactor Ownership Helpers`, `Runtime Header Dependencies`, `Sequence Slot Retirement`, `Request Record State`, `Sequence Clear Events`, `Terminal Request Transitions`, `Request State Snapshots`, `KV Lease Operations`, `Deadline Enforcement Tests`, `Engine Sequence State`, `FIFO Request Planning`, `Sequence Clear Requests`, `Work Item Specification`, `Backend Memory Metrics`?**
  _High betweenness centrality (0.026) - this node is a cross-community bridge._
- **Are the 21 inferred relationships involving `TEST()` (e.g. with `authorize_growth` and `can_authorize`) actually correct?**
  _`TEST()` has 21 INFERRED edges - model-reasoned connections that need verification._
- **What connects `schema_version`, `status`, `generated_at_utc` to the rest of the system?**
  _1267 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Runtime Event Handling` be split into smaller, more focused modules?**
  _Cohesion score 0.03623611922852133 - nodes in this community are weakly interconnected._
- **Should `Weighted Deficit Scheduling` be split into smaller, more focused modules?**
  _Cohesion score 0.05429864253393665 - nodes in this community are weakly interconnected._
## Token Accounting Note

Semantic extraction used session agents. Their tool results do not expose token counts. Reported zero token counts are unmeasured placeholders, not zero consumption.

## Graph Health Warning

316 dangling-endpoint edges; 5 self-loop edges; 418 collapsed directed edges; 429 collapsed undirected edges. Graph may be incomplete; see graph-health.json for details.
