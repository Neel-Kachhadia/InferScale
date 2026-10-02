#include "inferscale/runtime/sequence_allocator.hpp"

namespace inferscale {

SequenceAllocator::SequenceAllocator(std::uint32_t capacity) : slots_(capacity) {}

std::optional<SequenceId> SequenceAllocator::allocate(RequestKey key) {
  if (key.id == 0 || key.generation == 0 || by_key_.contains(key)) return std::nullopt;
  for (SequenceId seq = 0; seq < slots_.size(); ++seq) {
    auto& slot = slots_[seq];
    if (!slot.owner) {
      slot.owner = key;
      slot.inflight = 0;
      slot.clearing = false;
      by_key_.emplace(key, seq);
      ++live_;
      return seq;
    }
  }
  return std::nullopt;
}

std::optional<SequenceId> SequenceAllocator::find(RequestKey key) const {
  const auto it = by_key_.find(key);
  return it == by_key_.end() ? std::nullopt : std::optional<SequenceId>(it->second);
}

bool SequenceAllocator::pin(RequestKey key) {
  const auto seq = find(key);
  if (!seq || slots_[*seq].clearing) return false;
  ++slots_[*seq].inflight;
  return true;
}

bool SequenceAllocator::retire(RequestKey key) {
  const auto seq = find(key);
  if (!seq || slots_[*seq].inflight == 0) return false;
  --slots_[*seq].inflight;
  return true;
}

bool SequenceAllocator::begin_clear(RequestKey key) {
  const auto seq = find(key);
  if (!seq || slots_[*seq].inflight != 0 || slots_[*seq].clearing) return false;
  slots_[*seq].clearing = true;
  return true;
}

bool SequenceAllocator::clear_result(RequestKey key, bool succeeded) {
  const auto seq = find(key);
  if (!seq || !slots_[*seq].clearing) return false;
  auto& slot = slots_[*seq];
  if (!succeeded) {
    slot.clearing = false;
    return true;
  }
  slot.owner.reset();
  slot.inflight = 0;
  slot.clearing = false;
  by_key_.erase(key);
  --live_;
  return true;
}

std::uint32_t SequenceAllocator::live_sequences() const noexcept { return live_; }

std::uint32_t SequenceAllocator::inflight(RequestKey key) const noexcept {
  const auto seq = find(key);
  return seq ? slots_[*seq].inflight : 0;
}

}  // namespace inferscale
