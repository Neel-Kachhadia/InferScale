#include "inferscale/engine/fake_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <thread>

namespace inferscale {

FakeEngine::FakeEngine(EngineCapabilities capabilities, std::chrono::milliseconds prefill_delay,
                       std::chrono::milliseconds decode_delay)
    : capabilities_(capabilities), prefill_delay_(prefill_delay), decode_delay_(decode_delay) {}

EngineCapabilities FakeEngine::capabilities() const { return capabilities_; }

std::vector<TokenId> FakeEngine::tokenize(std::string_view prompt) const {
  std::vector<TokenId> tokens;
  bool in_word = false;
  for (const unsigned char character : prompt) {
    if (std::isspace(character) != 0) {
      in_word = false;
    } else if (!in_word) {
      tokens.push_back(1);
      in_word = true;
    }
  }
  if (tokens.empty()) tokens.push_back(1);
  return tokens;
}

EngineResult FakeEngine::execute(const BatchTicket& ticket) {
  {
    std::unique_lock lock(mutex_);
    if (paused_) {
      paused_at_before_ = true;
      condition_.notify_all();
    }
    condition_.wait(lock, [this] { return !paused_; });
    paused_at_before_ = false;
    inflight_.insert(ticket.id());
    if (next_failure_) {
      const auto failure = *next_failure_;
      next_failure_.reset();
      inflight_.erase(ticket.id());
      return {false, failure, {}, "injected fake-engine failure"};
    }
    if (ticket.entries().size() > capabilities_.max_sequences) {
      inflight_.erase(ticket.id());
      return {false, FailureClass::RequestRecoverable, {}, "sequence capacity exceeded"};
    }
    std::uint64_t token_count = 0;
    for (const auto& entry : ticket.entries()) token_count += entry.work.token_count;
    if (token_count > capabilities_.max_batch_tokens) {
      inflight_.erase(ticket.id());
      return {false, FailureClass::RequestRecoverable, {}, "batch token capacity exceeded"};
    }

    EngineResult result;
    result.outputs.reserve(ticket.entries().size());
    for (const auto& entry : ticket.entries()) {
      const auto delay = entry.work.kind == WorkKind::Prefill ? prefill_delay_ : decode_delay_;
      if (delay.count() > 0) {
        lock.unlock();
        std::this_thread::sleep_for(delay);
        lock.lock();
      }
      auto& position = positions_[entry.sequence];
      if (entry.work.kind == WorkKind::Prefill) {
        if (position != entry.work.token_begin) {
          inflight_.erase(ticket.id());
          return {false, FailureClass::EngineRestartRequired, {}, "prefill position mismatch"};
        }
        position += entry.work.token_count;
        result.outputs.emplace_back(std::nullopt);
      } else {
        if (position != entry.work.token_begin) {
          inflight_.erase(ticket.id());
          return {false, FailureClass::EngineRestartRequired, {}, "decode position mismatch"};
        }
        const auto token = 1000U + entry.sequence * 17U + position;
        result.outputs.emplace_back(EngineOutput{std::to_string(token) + " ", false});
        position += 1U;
      }
    }
    if (paused_after_) {
      paused_at_after_ = true;
      condition_.notify_all();
      condition_.wait(lock, [this] { return !paused_after_; });
      paused_at_after_ = false;
    }
    inflight_.erase(ticket.id());
    return result;
  }
}

BackendMemoryStats FakeEngine::memory_stats() const {
  std::lock_guard lock(mutex_);
  std::uint64_t total = 0;
  for (const auto& [sequence, position] : positions_) {
    (void)sequence;
    total += position;
  }
  return {total, static_cast<std::uint32_t>(positions_.size())};
}

bool FakeEngine::clear_sequence(SequenceId sequence) {
  std::lock_guard lock(mutex_);
  if (!inflight_.empty()) return false;
  if (fail_clear_) {
    fail_clear_ = false;
    return false;
  }
  positions_.erase(sequence);
  return true;
}

void FakeEngine::pause_before_execute() {
  std::lock_guard lock(mutex_);
  paused_ = true;
}

bool FakeEngine::wait_until_before_execute_paused(std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  return condition_.wait_for(lock, timeout, [this] { return paused_at_before_; });
}

void FakeEngine::resume() {
  std::lock_guard lock(mutex_);
  paused_ = false;
  paused_after_ = false;
  condition_.notify_all();
}

void FakeEngine::pause_after_execute() {
  std::lock_guard lock(mutex_);
  paused_after_ = true;
}

bool FakeEngine::wait_until_after_execute_paused(std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  return condition_.wait_for(lock, timeout, [this] { return paused_at_after_; });
}

void FakeEngine::fail_next(FailureClass failure) {
  std::lock_guard lock(mutex_);
  next_failure_ = failure;
}

void FakeEngine::fail_next_clear() {
  std::lock_guard lock(mutex_);
  fail_clear_ = true;
}

std::uint32_t FakeEngine::sequence_position(SequenceId sequence) const {
  std::lock_guard lock(mutex_);
  const auto it = positions_.find(sequence);
  return it == positions_.end() ? 0 : it->second;
}

std::size_t FakeEngine::inflight_count() const {
  std::lock_guard lock(mutex_);
  return inflight_.size();
}

}  // namespace inferscale
