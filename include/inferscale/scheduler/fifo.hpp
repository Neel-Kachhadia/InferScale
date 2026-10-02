#pragma once

#include "inferscale/runtime/types.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace inferscale {

struct ReadyRequest {
  RequestKey key;
  WorkKind kind{};
  std::uint32_t token_begin{};
  std::uint32_t tokens_remaining{};
};

inline WorkPlan fifo_plan(const std::vector<ReadyRequest>& ready,
                          std::uint32_t max_tokens,
                          std::uint32_t max_sequences,
                          std::uint32_t max_prefill_chunk) {
  WorkPlan plan;
  for (const auto& request : ready) {
    if (plan.sequence_count >= max_sequences || plan.total_tokens >= max_tokens) break;
    if (request.tokens_remaining == 0) continue;
    const auto limit = request.kind == WorkKind::Prefill ? max_prefill_chunk : 1U;
    const auto count = std::min({request.tokens_remaining, limit, max_tokens - plan.total_tokens});
    if (count == 0) continue;
    plan.items.push_back({request.key, request.kind, request.token_begin, count, count});
    plan.total_tokens += count;
    ++plan.sequence_count;
  }
  return plan;
}

inline bool valid_work_plan(const WorkPlan& plan) {
  std::uint64_t total = 0;
  std::vector<RequestKey> keys;
  keys.reserve(plan.items.size());
  for (const auto& item : plan.items) {
    if (item.key.id == 0 || item.key.generation == 0 || item.token_count == 0 ||
        item.expected_kv_growth > item.token_count) return false;
    // ponytail: O(n^2) duplicate check; plans are sequence-bounded, use a hash set if that limit grows.
    if (std::find(keys.begin(), keys.end(), item.key) != keys.end()) return false;
    keys.push_back(item.key);
    total += item.token_count;
  }
  return total == plan.total_tokens && plan.items.size() == plan.sequence_count;
}

}  // namespace inferscale
