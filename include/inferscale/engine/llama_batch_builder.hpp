#pragma once

#include "inferscale/execution/batch_ticket.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <vector>

struct llama_batch;

namespace inferscale {

struct LlamaBatchToken {
  TokenId token{};
  std::int32_t position{};
  std::int32_t sequence{};
  std::size_t flat_index{};
  std::optional<std::size_t> logits_index;
};

struct LlamaBatchPlan {
  std::vector<LlamaBatchToken> tokens;
  // Indexed like BatchTicket::entries(); populated only for entries that request logits.
  std::vector<std::optional<std::size_t>> logits_index_by_entry;
  std::size_t logits_count{};
};

class LlamaBatchBuilder {
 public:
  // A Decode entry without a decode input consumes a token sampled at final prefill and
  // staged by the engine. Decode inputs are aligned with ticket entries; prefill entries
  // must have no supplied decode input.
  static LlamaBatchPlan build(const BatchTicket& ticket,
                              std::span<const std::optional<TokenId>> decode_inputs = {}) {
    if (!decode_inputs.empty() && decode_inputs.size() != ticket.entries().size()) {
      throw std::invalid_argument("decode inputs must align with batch ticket entries");
    }

    LlamaBatchPlan plan;
    plan.logits_index_by_entry.resize(ticket.entries().size());
    std::unordered_set<RequestKey, RequestKeyHash> keys;
    std::unordered_set<SequenceId> sequences;
    keys.reserve(ticket.entries().size());
    sequences.reserve(ticket.entries().size());
    for (std::size_t entry_index = 0; entry_index < ticket.entries().size(); ++entry_index) {
      const auto& entry = ticket.entries()[entry_index];
      const auto& work = entry.work;
      if (!entry.prompt_token_ids || entry.prompt_token_ids->empty()) {
        throw std::invalid_argument("batch ticket is missing prompt token ids");
      }
      const auto* supplied_decode = decode_inputs.empty() ? nullptr : &decode_inputs[entry_index];
      const bool has_decode_input = supplied_decode != nullptr && supplied_decode->has_value();
      if (entry.sequence > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("sequence id exceeds llama.cpp range");
      }
      if (work.kind != WorkKind::Prefill && work.kind != WorkKind::Decode) {
        throw std::invalid_argument("unsupported work kind in batch ticket");
      }
      if (work.key.id == 0 || work.key.generation == 0 || entry.max_output_tokens == 0 ||
          !keys.insert(work.key).second || !sequences.insert(entry.sequence).second) {
        throw std::invalid_argument("batch ticket contains duplicate or invalid sequence entries");
      }

      if (work.kind == WorkKind::Prefill) {
        if (has_decode_input || work.token_count == 0 ||
            static_cast<std::uint64_t>(work.token_begin) + work.token_count >
                entry.prompt_token_ids->size()) {
          throw std::invalid_argument("invalid prompt range in prefill ticket");
        }
        for (std::uint32_t offset = 0; offset < work.token_count; ++offset) {
          const auto prompt_index = static_cast<std::size_t>(work.token_begin + offset);
          const bool needs_logits = prompt_index + 1 == entry.prompt_token_ids->size();
          append(plan, (*entry.prompt_token_ids)[prompt_index],
                 static_cast<std::uint64_t>(work.token_begin) + offset,
                 entry.sequence, needs_logits, entry_index);
        }
      } else {
        if (work.token_count != 1 || work.token_begin < entry.prompt_token_ids->size() ||
            work.token_begin - entry.prompt_token_ids->size() >= entry.max_output_tokens) {
          throw std::invalid_argument("decode ticket must identify one token after prompt");
        }
        if (has_decode_input) {
          append(plan, **supplied_decode, work.token_begin, entry.sequence, true, entry_index);
        }
      }
    }
    return plan;
  }

  static void populate_llama_batch(::llama_batch& batch, const LlamaBatchPlan& plan,
                                   std::size_t capacity);

 private:
  static void append(LlamaBatchPlan& plan, TokenId token, std::uint64_t position,
                     SequenceId sequence, bool needs_logits, std::size_t entry_index) {
    if (position > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
      throw std::invalid_argument("token position exceeds llama.cpp range");
    }
    const auto flat_index = plan.tokens.size();
    std::optional<std::size_t> logits_index;
    if (needs_logits) {
      // llama_get_logits_ith() resolves positive indexes through output_ids, which is
      // indexed by the flattened input token position (not compact output-row order).
      logits_index = flat_index;
      ++plan.logits_count;
      plan.logits_index_by_entry[entry_index] = logits_index;
    }
    plan.tokens.push_back({token, static_cast<std::int32_t>(position),
                          static_cast<std::int32_t>(sequence), flat_index, logits_index});
  }
};

}  // namespace inferscale
