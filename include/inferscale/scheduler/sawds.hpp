#pragma once

#include "inferscale/runtime/types.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace inferscale {

struct SchedulerCandidate {
  RequestKey key;
  RequestClass service_class{RequestClass::Interactive};
  WorkKind kind{};
  std::uint32_t token_begin{};
  std::uint32_t tokens_remaining{};
  ClockTime ready_since{};
  std::optional<ClockTime> deadline;
  std::uint64_t arrival_order{};
  std::uint64_t soft_reserved_tokens{};
};

struct SawdsConfig {
  std::array<std::uint64_t, 2> class_weights{1024, 256};
  std::uint64_t credit_cap_tokens{4096};
  std::chrono::milliseconds starvation_after{5000};
  std::chrono::milliseconds age_bucket{1000};
  std::uint64_t max_age_bucket{60};
  std::chrono::milliseconds deadline_bucket{100};
  std::uint64_t max_deadline_bucket{600};
  std::uint16_t minimum_decode_share_basis_points{2500};
};

struct SawdsPlan {
  WorkPlan work;
  // Credits after inactive classes are discarded and the current weighted
  // service round is opened, before charging the proposed plan. The reactor
  // uses this base to charge the resource-trimmed plan it actually issues.
  std::array<std::uint64_t, 2> credits_before_service{};
  std::array<std::uint64_t, 2> credits_after{};
};

void validate_sawds_config(const SawdsConfig& config);

SawdsPlan sawds_plan(const std::vector<SchedulerCandidate>& ready,
                     const std::array<std::uint64_t, 2>& credits,
                     ClockTime now, std::uint32_t max_tokens,
                     std::uint32_t max_sequences,
                     std::uint32_t max_prefill_chunk,
                     std::uint32_t max_prefill_tokens_per_epoch,
                     const SawdsConfig& config = {});

}  // namespace inferscale
