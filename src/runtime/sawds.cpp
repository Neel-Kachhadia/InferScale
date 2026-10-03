#include "inferscale/scheduler/sawds.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace inferscale {
namespace {

std::size_t class_index(RequestClass service_class) {
  switch (service_class) {
    case RequestClass::Interactive: return 0;
    case RequestClass::Batch: return 1;
  }
  throw std::invalid_argument("invalid request service class");
}

std::uint64_t bucket(std::chrono::milliseconds value,
                     std::chrono::milliseconds width) noexcept {
  if (width.count() <= 0 || value.count() <= 0) return 0;
  return static_cast<std::uint64_t>(value.count() / width.count());
}

std::chrono::milliseconds elapsed(ClockTime now, ClockTime since) noexcept {
  if (now <= since) return std::chrono::milliseconds::zero();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - since);
}

bool more_urgent(const SchedulerCandidate* left, const SchedulerCandidate* right,
                 ClockTime now, const SawdsConfig& config) noexcept {
  const auto left_wait = elapsed(now, left->ready_since);
  const auto right_wait = elapsed(now, right->ready_since);
  const bool left_starved = left_wait >= config.starvation_after;
  const bool right_starved = right_wait >= config.starvation_after;
  if (left_starved != right_starved) return left_starved;

  const auto deadline_bucket = [&](const SchedulerCandidate* candidate) {
    if (!candidate->deadline) return std::numeric_limits<std::uint64_t>::max();
    if (*candidate->deadline <= now) return std::uint64_t{0};
    return std::min(config.max_deadline_bucket, bucket(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            *candidate->deadline - now), config.deadline_bucket));
  };
  const auto left_deadline = deadline_bucket(left);
  const auto right_deadline = deadline_bucket(right);
  if (left_deadline != right_deadline) return left_deadline < right_deadline;

  const auto left_age = std::min(config.max_age_bucket, bucket(left_wait, config.age_bucket));
  const auto right_age = std::min(config.max_age_bucket, bucket(right_wait, config.age_bucket));
  if (left_age != right_age) return left_age > right_age;
  if (left->arrival_order != right->arrival_order) return left->arrival_order < right->arrival_order;
  return left->key < right->key;
}

std::uint64_t replenish(std::uint64_t current, std::uint64_t weight,
                        std::uint64_t cap) noexcept {
  if (current >= cap || weight >= cap - current) return cap;
  return current + weight;
}

std::uint32_t decode_reserve(std::uint32_t max_tokens,
                             std::uint16_t basis_points) noexcept {
  if (max_tokens == 0 || basis_points == 0) return 0;
  const auto numerator = static_cast<std::uint64_t>(max_tokens) * basis_points;
  const auto rounded_up = (numerator + 9999U) / 10000U;
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      max_tokens, std::max<std::uint64_t>(1, rounded_up)));
}

}  // namespace

void validate_sawds_config(const SawdsConfig& config) {
  if (config.credit_cap_tokens == 0 || config.class_weights[0] == 0 ||
      config.class_weights[1] == 0 || config.starvation_after.count() <= 0 ||
      config.age_bucket.count() <= 0 || config.deadline_bucket.count() <= 0 ||
      config.max_age_bucket == 0 || config.max_deadline_bucket == 0 ||
      config.minimum_decode_share_basis_points > 10000) {
    throw std::invalid_argument("invalid SA-WDS configuration");
  }
}

SawdsPlan sawds_plan(const std::vector<SchedulerCandidate>& ready,
                     const std::array<std::uint64_t, 2>& credits,
                     ClockTime now, std::uint32_t max_tokens,
                     std::uint32_t max_sequences,
                     std::uint32_t max_prefill_chunk,
                     std::uint32_t max_prefill_tokens_per_epoch,
                     const SawdsConfig& config) {
  validate_sawds_config(config);

  SawdsPlan result;
  std::array<std::vector<const SchedulerCandidate*>, 2> by_class;
  for (const auto& candidate : ready) {
    const auto index = class_index(candidate.service_class);
    if (candidate.key.id == 0 || candidate.key.generation == 0 ||
        candidate.tokens_remaining == 0 || max_tokens == 0 || max_sequences == 0 ||
        (candidate.kind == WorkKind::Prefill &&
         (max_prefill_chunk == 0 || max_prefill_tokens_per_epoch == 0)) ||
        (candidate.kind != WorkKind::Prefill && candidate.kind != WorkKind::Decode)) continue;
    by_class[index].push_back(&candidate);
  }
  for (auto& group : by_class) {
    std::stable_sort(group.begin(), group.end(), [now, &config](const auto* left, const auto* right) {
      return more_urgent(left, right, now, config);
    });
  }

  // Credits represent service quanta, not per-epoch bonuses. Reopen a weighted
  // round only when every currently active class has spent its balance. Idle
  // classes discard stale credit so they cannot bank a future monopoly.
  bool any_active_credit = false;
  for (std::size_t service_class = 0; service_class < by_class.size(); ++service_class) {
    if (by_class[service_class].empty()) {
      result.credits_before_service[service_class] = 0;
      continue;
    }
    result.credits_before_service[service_class] = std::min(
        credits[service_class], config.credit_cap_tokens);
    any_active_credit = any_active_credit || result.credits_before_service[service_class] > 0;
  }
  if (!any_active_credit) {
    for (std::size_t service_class = 0; service_class < by_class.size(); ++service_class) {
      if (!by_class[service_class].empty()) {
        result.credits_before_service[service_class] = replenish(
            0, config.class_weights[service_class], config.credit_cap_tokens);
      }
    }
  }
  result.credits_after = result.credits_before_service;
  if (max_tokens == 0 || max_sequences == 0) return result;

  std::array<std::vector<bool>, 2> selected;
  for (std::size_t service_class = 0; service_class < selected.size(); ++service_class) {
    selected[service_class].resize(by_class[service_class].size(), false);
  }

  const auto next_candidate = [&](std::size_t service_class, WorkKind kind)
      -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < by_class[service_class].size(); ++index) {
      if (!selected[service_class][index] && by_class[service_class][index]->kind == kind) {
        return index;
      }
    }
    return std::nullopt;
  };
  const auto append = [&](std::size_t service_class, std::size_t index,
                          std::uint32_t count) {
    const auto* candidate = by_class[service_class][index];
    result.work.items.push_back({candidate->key, candidate->kind, candidate->token_begin,
                                 count, count});
    result.work.total_tokens += count;
    ++result.work.sequence_count;
    result.credits_after[service_class] = result.credits_after[service_class] >= count
        ? result.credits_after[service_class] - count : 0;
    selected[service_class][index] = true;
  };
  const auto choose_candidate = [&](WorkKind kind, std::uint32_t available_tokens)
      -> std::optional<std::tuple<std::size_t, std::size_t, bool>> {
    std::optional<std::tuple<std::size_t, std::size_t, bool>> starved_choice;
    for (std::size_t service_class = 0; service_class < by_class.size(); ++service_class) {
      const auto index = next_candidate(service_class, kind);
      if (!index) continue;
      const auto* candidate = by_class[service_class][*index];
      if (elapsed(now, candidate->ready_since) < config.starvation_after) continue;
      if (!starved_choice || more_urgent(candidate,
            by_class[std::get<0>(*starved_choice)][std::get<1>(*starved_choice)], now, config)) {
        starved_choice = std::tuple{service_class, *index, true};
      }
    }
    if (starved_choice) return starved_choice;

    std::optional<std::tuple<std::size_t, std::size_t, bool>> choice;
    for (std::size_t service_class = 0; service_class < by_class.size(); ++service_class) {
      const auto index = next_candidate(service_class, kind);
      if (!index || result.credits_after[service_class] < available_tokens) continue;
      if (!choice || result.credits_after[service_class] >
                         result.credits_after[std::get<0>(*choice)]) {
        choice = std::tuple{service_class, *index, false};
      }
    }
    return choice;
  };
  const auto schedule_decodes = [&](std::uint32_t token_budget,
                                    std::uint32_t sequence_limit) {
    std::uint32_t scheduled = 0;
    while (scheduled < token_budget && result.work.sequence_count < sequence_limit &&
           result.work.total_tokens < max_tokens) {
      const auto choice = choose_candidate(WorkKind::Decode, 1);
      if (!choice) break;
      append(std::get<0>(*choice), std::get<1>(*choice), 1);
      ++scheduled;
    }
    return scheduled;
  };
  const auto schedule_prefills = [&](std::uint32_t token_limit,
                                     std::uint32_t max_prefill_tokens) {
    std::uint32_t scheduled = 0;
    while (scheduled < max_prefill_tokens && result.work.total_tokens < token_limit &&
           result.work.sequence_count < max_sequences) {
      std::optional<std::tuple<std::size_t, std::size_t, std::uint32_t, bool>> choice;
      for (std::size_t service_class = 0; service_class < by_class.size(); ++service_class) {
        const auto index = next_candidate(service_class, WorkKind::Prefill);
        if (!index) continue;
        const auto* candidate = by_class[service_class][*index];
        const bool starved = elapsed(now, candidate->ready_since) >= config.starvation_after;
        if (result.credits_after[service_class] == 0 && !starved) continue;
        const auto remaining_plan_tokens = token_limit - result.work.total_tokens;
        const auto remaining_prefill_tokens = max_prefill_tokens - scheduled;
        const auto credit = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            result.credits_after[service_class], std::numeric_limits<std::uint32_t>::max()));
        const auto count = starved
            ? std::min({candidate->tokens_remaining, max_prefill_chunk,
                        remaining_plan_tokens, remaining_prefill_tokens})
            : std::min({candidate->tokens_remaining, max_prefill_chunk,
                        remaining_plan_tokens, remaining_prefill_tokens, credit});
        if (count == 0) continue;
        const bool supersedes = !choice || (starved && !std::get<3>(*choice)) ||
            (starved == std::get<3>(*choice) &&
             (starved
                  ? more_urgent(candidate,
                        by_class[std::get<0>(*choice)][std::get<1>(*choice)], now, config)
                  : result.credits_after[service_class] >
                        result.credits_after[std::get<0>(*choice)]));
        if (supersedes) {
          choice = std::tuple{service_class, *index, count, starved};
        }
      }
      if (!choice) break;
      append(std::get<0>(*choice), std::get<1>(*choice), std::get<2>(*choice));
      scheduled += std::get<2>(*choice);
    }
    return scheduled;
  };

  std::uint32_t decode_candidates = 0;
  for (const auto& group : by_class) {
    decode_candidates += static_cast<std::uint32_t>(std::count_if(
        group.begin(), group.end(), [](const auto* candidate) {
          return candidate->kind == WorkKind::Decode;
        }));
  }
  const bool starved_prefill = std::any_of(by_class.begin(), by_class.end(),
      [now, &config](const auto& group) {
        return std::any_of(group.begin(), group.end(), [now, &config](const auto* candidate) {
          return candidate->kind == WorkKind::Prefill &&
                 elapsed(now, candidate->ready_since) >= config.starvation_after;
        });
      });
  auto decode_budget = decode_candidates == 0 ? 0U
      : decode_reserve(max_tokens, config.minimum_decode_share_basis_points);
  // Preserve one token as well as one sequence slot for an aged prefill. The
  // normal decode reserve resumes immediately after that eligible chunk gets
  // an issue opportunity.
  if (starved_prefill && decode_budget == max_tokens && decode_budget != 0) {
    --decode_budget;
  }
  const auto decode_sequence_limit = starved_prefill && max_sequences != 0
      ? max_sequences - 1U : max_sequences;
  const auto protected_decodes = schedule_decodes(decode_budget,
                                                   decode_sequence_limit);
  const auto decode_share_left = decode_budget - protected_decodes;

  bool eligible_decode_left = false;
  for (std::size_t service_class = 0; service_class < by_class.size(); ++service_class) {
    if (result.credits_after[service_class] == 0) continue;
    if (next_candidate(service_class, WorkKind::Decode)) {
      eligible_decode_left = true;
      break;
    }
  }
  const auto reserved_for_decode = eligible_decode_left ? decode_share_left : 0U;
  const auto prefill_limit = max_tokens - reserved_for_decode;
  const auto prefill_tokens = schedule_prefills(prefill_limit, max_prefill_tokens_per_epoch);

  const auto remaining_tokens = max_tokens - result.work.total_tokens;
  schedule_decodes(remaining_tokens, max_sequences);

  // If no decode could spend its reserved share, prefill gets the remainder
  // during this epoch. The share is therefore protection, never a hard pin.
  if (!eligible_decode_left && prefill_tokens < max_prefill_tokens_per_epoch) {
    schedule_prefills(max_tokens, max_prefill_tokens_per_epoch - prefill_tokens);
  }
  return result;
}

}  // namespace inferscale
