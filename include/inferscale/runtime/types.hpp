#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inferscale {

using ClockTime = std::chrono::steady_clock::time_point;
using TokenId = std::int32_t;

struct RequestKey {
  std::uint64_t id{};
  std::uint32_t generation{};
  auto operator<=>(const RequestKey&) const = default;
};

struct RequestKeyHash {
  std::size_t operator()(const RequestKey& key) const noexcept {
    const auto id = std::hash<std::uint64_t>{}(key.id);
    const auto generation = std::hash<std::uint32_t>{}(key.generation);
    return id ^ (generation + 0x9e3779b9U + (id << 6U) + (id >> 2U));
  }
};

enum class WorkKind : std::uint8_t { Prefill, Decode };
enum class RequestClass : std::uint8_t { Interactive, Batch };
enum class KvPressureState : std::uint8_t { Normal, SoftPressure, HighPressure, HardLimit };
enum class GrowthResolution : std::uint8_t { Unknown, NotExecuted, Committed };

struct WorkItem {
  RequestKey key;
  WorkKind kind{};
  std::uint32_t token_begin{};
  std::uint32_t token_count{};
  std::uint32_t expected_kv_growth{};
  auto operator<=>(const WorkItem&) const = default;
};

struct WorkPlan {
  std::vector<WorkItem> items;
  std::uint32_t total_tokens{};
  std::uint32_t sequence_count{};
};

struct RequestSpec {
  std::uint64_t id{};
  std::uint32_t prompt_tokens{};
  std::uint32_t max_output_tokens{};
  std::optional<ClockTime> deadline;
  std::uint32_t stream_capacity_bytes{4096};
  std::shared_ptr<const std::vector<TokenId>> prompt_token_ids{};
  RequestClass service_class{RequestClass::Interactive};
  std::optional<std::chrono::milliseconds> ttft_slo;
  std::optional<std::chrono::milliseconds> e2e_slo;
};

enum class FailureClass : std::uint8_t {
  RequestRecoverable,
  EngineRecoverable,
  EngineRestartRequired,
  ProcessFatal
};

struct EngineOutput {
  std::string piece;
  bool end_of_sequence{false};
};

struct EngineResult {
  bool ok{true};
  FailureClass failure{FailureClass::RequestRecoverable};
  // Aligned with BatchTicket entries. Prefill entries have no output.
  std::vector<std::optional<EngineOutput>> outputs;
  std::string message;
  GrowthResolution growth_resolution{GrowthResolution::Unknown};
  // Exact per-entry sequence growth when growth_resolution is Committed.
  std::vector<std::uint32_t> committed_kv_growth;
};

struct EngineCapabilities {
  std::uint32_t max_sequences{};
  std::uint32_t max_batch_tokens{};
  std::uint32_t context_limit{};
  bool cuda_enabled{false};
  std::uint32_t max_output_piece_bytes{11};
  std::uint32_t max_prefill_chunk_tokens{64};
  std::uint32_t resolved_total_context{};
};

struct BackendMemoryStats {
  // Engine-maintained estimate in token positions, not physical KV bytes or
  // an authoritative query of backend/device allocation.
  std::uint64_t estimated_committed_tokens{};
  std::uint32_t live_sequences{};
};

}  // namespace inferscale
