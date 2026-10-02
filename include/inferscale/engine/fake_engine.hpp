#pragma once

#include "inferscale/execution/inference_engine.hpp"

#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace inferscale {

class FakeEngine final : public InferenceEngine {
 public:
  explicit FakeEngine(EngineCapabilities capabilities,
                      std::chrono::milliseconds prefill_delay = {},
                      std::chrono::milliseconds decode_delay = {});
  EngineCapabilities capabilities() const override;
  EngineResult execute(const BatchTicket& ticket) override;
  BackendMemoryStats memory_stats() const override;
  bool clear_sequence(SequenceId sequence) override;

  void pause_before_execute();
  bool wait_until_before_execute_paused(std::chrono::milliseconds timeout);
  void pause_after_execute();
  bool wait_until_after_execute_paused(std::chrono::milliseconds timeout);
  void resume();
  void fail_next(FailureClass failure);
  void fail_next_clear();
  std::uint32_t sequence_position(SequenceId sequence) const;
  std::size_t inflight_count() const;

 private:
  EngineCapabilities capabilities_;
  std::chrono::milliseconds prefill_delay_;
  std::chrono::milliseconds decode_delay_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::unordered_map<SequenceId, std::uint32_t> positions_;
  std::unordered_set<std::uint64_t> inflight_;
  std::optional<FailureClass> next_failure_;
  bool paused_{false};
  bool paused_at_before_{false};
  bool paused_after_{false};
  bool paused_at_after_{false};
  bool fail_clear_{false};
};

}  // namespace inferscale
