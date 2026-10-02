#pragma once

#include "inferscale/execution/inference_engine.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace inferscale {

struct LlamaEngineConfig {
  std::string model_path;
  std::uint32_t context_per_sequence{2048};
  std::uint32_t max_sequences{16};
  std::uint32_t batch_tokens{2048};
  std::uint32_t ubatch_tokens{512};
  std::int32_t gpu_layers{99};
  std::int32_t threads{8};
  std::int32_t batch_threads{8};
};

class LlamaEngine final : public InferenceEngine {
 public:
  explicit LlamaEngine(LlamaEngineConfig config);
  ~LlamaEngine() override;
  LlamaEngine(const LlamaEngine&) = delete;
  LlamaEngine& operator=(const LlamaEngine&) = delete;

  EngineCapabilities capabilities() const override;
  std::vector<TokenId> tokenize(std::string_view prompt) const override;
  EngineResult execute(const BatchTicket& ticket) override;
  BackendMemoryStats memory_stats() const override;
  bool clear_sequence(SequenceId sequence) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace inferscale
