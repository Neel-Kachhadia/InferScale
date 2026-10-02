#pragma once

#include "inferscale/execution/batch_ticket.hpp"
#include "inferscale/runtime/types.hpp"

#include <optional>
#include <string_view>

namespace inferscale {

class InferenceEngine {
 public:
  virtual ~InferenceEngine() = default;
  virtual EngineCapabilities capabilities() const = 0;
  virtual std::vector<TokenId> tokenize(std::string_view prompt) const = 0;
  virtual EngineResult execute(const BatchTicket& ticket) = 0;
  virtual BackendMemoryStats memory_stats() const = 0;
  virtual bool clear_sequence(SequenceId sequence) = 0;
};

}  // namespace inferscale
