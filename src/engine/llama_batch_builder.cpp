#include "inferscale/engine/llama_batch_builder.hpp"

#include <llama.h>

#include <climits>
#include <stdexcept>

namespace inferscale {

void LlamaBatchBuilder::populate_llama_batch(::llama_batch& batch,
                                             const LlamaBatchPlan& plan,
                                             std::size_t capacity) {
  if (plan.tokens.size() > capacity ||
      plan.tokens.size() > static_cast<std::size_t>(INT_MAX) ||
      (plan.tokens.size() != 0 && (batch.token == nullptr || batch.pos == nullptr ||
       batch.n_seq_id == nullptr || batch.seq_id == nullptr || batch.logits == nullptr))) {
    throw std::invalid_argument("llama batch storage is too small for the plan");
  }
  for (std::size_t index = 0; index < plan.tokens.size(); ++index) {
    const auto& token = plan.tokens[index];
    if (token.flat_index != index || token.sequence < 0 ||
        token.logits_index.value_or(index) != index ||
        (token.logits_index && *token.logits_index > static_cast<std::size_t>(INT_MAX))) {
      throw std::invalid_argument("invalid flattened llama batch plan");
    }
    batch.token[index] = token.token;
    batch.pos[index] = token.position;
    batch.n_seq_id[index] = 1;
    if (batch.seq_id[index] == nullptr) {
      throw std::invalid_argument("llama batch has no sequence-id storage");
    }
    batch.seq_id[index][0] = token.sequence;
    batch.logits[index] = token.logits_index ? 1 : 0;
  }
  batch.n_tokens = static_cast<std::int32_t>(plan.tokens.size());
}

}  // namespace inferscale
