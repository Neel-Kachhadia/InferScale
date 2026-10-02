#pragma once

#include "inferscale/runtime/types.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace inferscale {

using SequenceId = std::uint32_t;

class SequenceAllocator {
 public:
  explicit SequenceAllocator(std::uint32_t capacity);
  std::optional<SequenceId> allocate(RequestKey key);
  std::optional<SequenceId> find(RequestKey key) const;
  bool pin(RequestKey key);
  bool retire(RequestKey key);
  bool begin_clear(RequestKey key);
  bool clear_result(RequestKey key, bool succeeded);
  std::uint32_t live_sequences() const noexcept;
  std::uint32_t inflight(RequestKey key) const noexcept;

 private:
  struct Slot {
    std::optional<RequestKey> owner;
    std::uint32_t inflight{};
    bool clearing{false};
  };
  std::vector<Slot> slots_;
  std::unordered_map<RequestKey, SequenceId, RequestKeyHash> by_key_;
  std::uint32_t live_{};
};

}  // namespace inferscale
