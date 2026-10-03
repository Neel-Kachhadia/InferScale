#include "inferscale/engine/llama_engine.hpp"
#include "inferscale/runtime/nvtx.hpp"

#include "inferscale/engine/llama_batch_builder.hpp"
#include "inferscale/runtime/stream_buffer.hpp"

#include <ggml-backend.h>
#include <llama.h>

#include <algorithm>
#include <climits>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace inferscale {
namespace {

std::mutex g_backend_mutex;
std::size_t g_backend_users{};

void acquire_backend() {
  std::lock_guard lock(g_backend_mutex);
  if (g_backend_users == 0) {
    ggml_backend_load_all();
    llama_backend_init();
  }
  ++g_backend_users;
}

void release_backend() noexcept {
  std::lock_guard lock(g_backend_mutex);
  if (g_backend_users == 0) return;
  if (--g_backend_users == 0) llama_backend_free();
}

bool cuda_backend_available() {
  auto registry = ggml_backend_reg_by_name("CUDA");
  if (registry == nullptr) return false;
  for (std::size_t index = 0; index < ggml_backend_reg_dev_count(registry); ++index) {
    auto device = ggml_backend_reg_dev_get(registry, index);
    if (device != nullptr && ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
  }
  return false;
}

std::uint32_t checked_total_context(const LlamaEngineConfig& config) {
  if (config.context_per_sequence == 0 || config.max_sequences == 0 ||
      config.batch_tokens == 0 || config.ubatch_tokens == 0 ||
      config.ubatch_tokens > config.batch_tokens || config.threads <= 0 ||
      config.batch_threads <= 0 || config.batch_tokens > static_cast<std::uint32_t>(INT_MAX) ||
      config.ubatch_tokens > static_cast<std::uint32_t>(INT_MAX)) {
    throw std::invalid_argument("invalid llama engine configuration");
  }
  const auto total = static_cast<std::uint64_t>(config.context_per_sequence) * config.max_sequences;
  if (total > std::numeric_limits<std::uint32_t>::max() ||
      total > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument("total context exceeds llama.cpp position range");
  }
  return static_cast<std::uint32_t>(total);
}

EngineResult failure(FailureClass kind, std::string message,
                     GrowthResolution growth = GrowthResolution::Unknown) {
  return {false, kind, {}, std::move(message), growth, {}};
}

}  // namespace

struct LlamaEngine::Impl {
  struct SequenceState {
    RequestKey key;
    std::shared_ptr<const std::vector<TokenId>> prompt;
    std::uint32_t prefetched{};
    std::uint32_t emitted{};
    TokenId pending_token{};
    bool has_pending{false};
    bool pending_eog{false};
    Utf8StreamDecoder utf8;
  };

  explicit Impl(LlamaEngineConfig value) : config(std::move(value)) {
    try {
      const auto total_context = checked_total_context(config);
      if (config.model_path.empty() || !std::filesystem::is_regular_file(config.model_path)) {
        throw std::invalid_argument("--model must name an existing GGUF file");
      }
      acquire_backend();
      backend_acquired = true;

      auto model_params = llama_model_default_params();
      model_params.n_gpu_layers = config.gpu_layers;
      model = llama_model_load_from_file(config.model_path.c_str(), model_params);
      if (model == nullptr) throw std::runtime_error("llama.cpp could not load the model");
      if (!llama_model_has_decoder(model) || llama_model_has_encoder(model) ||
          llama_model_is_diffusion(model)) {
        throw std::invalid_argument("only decoder-only causal GGUF models are supported");
      }
      vocab = llama_model_get_vocab(model);
      if (vocab == nullptr) throw std::runtime_error("llama.cpp model has no vocabulary");

      auto context_params = llama_context_default_params();
      context_params.n_ctx = total_context;
      context_params.n_seq_max = config.max_sequences;
      context_params.n_batch = config.batch_tokens;
      context_params.n_ubatch = config.ubatch_tokens;
      context_params.n_threads = config.threads;
      context_params.n_threads_batch = config.batch_threads;
      context_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
      context_params.kv_unified = false;
      context = llama_init_from_model(model, context_params);
      if (context == nullptr) throw std::runtime_error("llama.cpp could not create a context");

      const auto resolved_per_sequence = llama_n_ctx_seq(context);
      const auto resolved_sequences = llama_n_seq_max(context);
      const auto resolved_batch = llama_n_batch(context);
      const auto resolved_ubatch = llama_n_ubatch(context);
      if (resolved_per_sequence == 0 || resolved_sequences == 0 || resolved_batch == 0 || resolved_ubatch == 0) {
        throw std::runtime_error("llama.cpp resolved an empty context or batch setting");
      }
      caps = {resolved_sequences, resolved_batch, resolved_per_sequence,
              config.gpu_layers != 0 && cuda_backend_available(), output_piece_bound(),
              resolved_ubatch, llama_n_ctx(context)};

      sampler = llama_sampler_init_greedy();
      if (sampler == nullptr) throw std::runtime_error("llama.cpp could not create greedy sampler");
      batch_tokens.resize(caps.max_batch_tokens);
      batch_positions.resize(caps.max_batch_tokens);
      batch_sequence_counts.resize(caps.max_batch_tokens);
      batch_sequence_ids.resize(caps.max_batch_tokens);
      batch_sequence_ptrs.resize(caps.max_batch_tokens);
      batch_logits.resize(caps.max_batch_tokens);
      for (std::size_t index = 0; index < batch_sequence_ids.size(); ++index) {
        batch_sequence_ptrs[index] = &batch_sequence_ids[index];
      }
      batch = {0, batch_tokens.data(), nullptr, batch_positions.data(),
               batch_sequence_counts.data(), batch_sequence_ptrs.data(), batch_logits.data()};
    } catch (...) {
      release_resources();
      throw;
    }
  }

  ~Impl() { release_resources(); }

  void release_resources() noexcept {
    if (context != nullptr) llama_synchronize(context);
    if (sampler != nullptr) llama_sampler_free(sampler);
    if (context != nullptr) llama_free(context);
    if (model != nullptr) llama_model_free(model);
    sampler = nullptr;
    context = nullptr;
    model = nullptr;
    if (backend_acquired) {
      backend_acquired = false;
      release_backend();
    }
  }

  std::uint32_t output_piece_bound() const {
    std::vector<char> buffer(256);
    std::uint64_t largest_piece = 0;
    const auto token_count = llama_vocab_n_tokens(vocab);
    for (std::int32_t token = 0; token < token_count; ++token) {
      auto size = llama_token_to_piece(vocab, token, buffer.data(),
                                       static_cast<std::int32_t>(buffer.size()), 0, false);
      if (size < 0) {
        if (size == INT32_MIN) throw std::runtime_error("token piece is too large");
        buffer.resize(static_cast<std::size_t>(-size));
        size = llama_token_to_piece(vocab, token, buffer.data(),
                                    static_cast<std::int32_t>(buffer.size()), 0, false);
      }
      if (size < 0) throw std::runtime_error("could not measure a model token piece");
      largest_piece = std::max(largest_piece, static_cast<std::uint64_t>(size));
    }
    const auto bound = largest_piece * 3U + 12U;
    if (bound == 0 || bound > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("model token piece bound exceeds runtime limits");
    }
    return static_cast<std::uint32_t>(bound);
  }

  std::string token_piece(TokenId token) const {
    std::vector<char> buffer(64);
    auto size = llama_token_to_piece(vocab, token, buffer.data(),
                                     static_cast<std::int32_t>(buffer.size()), 0, false);
    if (size < 0) {
      if (size == INT32_MIN) throw std::runtime_error("sampled token piece is too large");
      buffer.resize(static_cast<std::size_t>(-size));
      size = llama_token_to_piece(vocab, token, buffer.data(),
                                  static_cast<std::int32_t>(buffer.size()), 0, false);
    }
    if (size < 0) throw std::runtime_error("could not decode a sampled token piece");
    return {buffer.data(), static_cast<std::size_t>(size)};
  }

  TokenId sample(std::size_t logits_index) {
    if (logits_index > static_cast<std::size_t>(INT_MAX)) {
      throw std::runtime_error("llama logits index exceeds API range");
    }
    const auto token = llama_sampler_sample(sampler, context, static_cast<std::int32_t>(logits_index));
    if (token < 0 || token >= llama_vocab_n_tokens(vocab)) {
      throw std::runtime_error("llama.cpp greedy sampler returned an invalid token");
    }
    return token;
  }

  EngineResult execute(const BatchTicket& ticket) {
    INFERSCALE_NVTX_RANGE("LlamaEngine/BatchExecution");
    std::lock_guard lock(mutex);
    if (ticket.entries().empty() || ticket.entries().size() > caps.max_sequences) {
      return failure(FailureClass::RequestRecoverable, "batch sequence capacity exceeded",
                     GrowthResolution::NotExecuted);
    }
    std::vector<std::optional<TokenId>> decode_inputs(ticket.entries().size());
    for (std::size_t index = 0; index < ticket.entries().size(); ++index) {
      const auto& entry = ticket.entries()[index];
      if (!entry.prompt_token_ids || entry.prompt_token_ids->empty() || entry.max_output_tokens == 0) {
        return failure(FailureClass::RequestRecoverable, "ticket is missing prompt or output metadata",
                       GrowthResolution::NotExecuted);
      }
      if (entry.work.kind == WorkKind::Prefill) {
        auto it = sequences.find(entry.sequence);
        if (it == sequences.end()) {
          if (entry.work.token_begin != 0) {
            return failure(FailureClass::EngineRestartRequired, "prefill sequence began at a stale position",
                           GrowthResolution::NotExecuted);
          }
          SequenceState initial_state;
          initial_state.key = entry.work.key;
          initial_state.prompt = entry.prompt_token_ids;
          it = sequences.emplace(entry.sequence, std::move(initial_state)).first;
        }
        auto& state = it->second;
        if (state.key != entry.work.key || state.prompt != entry.prompt_token_ids ||
            state.prefetched != entry.work.token_begin || state.has_pending ||
            entry.work.token_count == 0 ||
            static_cast<std::uint64_t>(entry.work.token_begin) + entry.work.token_count >
                entry.prompt_token_ids->size()) {
          return failure(FailureClass::EngineRestartRequired, "prefill ticket does not match sequence state",
                         GrowthResolution::NotExecuted);
        }
      } else {
        auto it = sequences.find(entry.sequence);
        if (it == sequences.end()) {
          return failure(FailureClass::EngineRestartRequired, "decode sequence has no prefill state",
                         GrowthResolution::NotExecuted);
        }
        auto& state = it->second;
        const auto expected_begin = static_cast<std::uint64_t>(state.prompt->size()) + state.emitted;
        if (state.key != entry.work.key || state.prompt != entry.prompt_token_ids ||
            state.prefetched != state.prompt->size() || !state.has_pending ||
            entry.work.token_begin != expected_begin || entry.work.token_count != 1) {
          return failure(FailureClass::EngineRestartRequired, "decode ticket does not match sequence state",
                         GrowthResolution::NotExecuted);
        }
        if (!state.pending_eog) decode_inputs[index] = state.pending_token;
      }
    }

    const bool has_prefill = std::any_of(ticket.entries().begin(), ticket.entries().end(),
        [](const TicketEntry& entry) { return entry.work.kind == WorkKind::Prefill; });
    const bool has_decode = std::any_of(ticket.entries().begin(), ticket.entries().end(),
        [](const TicketEntry& entry) { return entry.work.kind == WorkKind::Decode; });
    const char* backend_range_name = has_prefill && has_decode
        ? "LlamaEngine/PrefillDecodeMixedBatch"
        : has_prefill ? "LlamaEngine/PrefillChunkBatch"
                      : "LlamaEngine/DecodeBatch";

    LlamaBatchPlan plan;
    try {
      INFERSCALE_NVTX_RANGE("LlamaEngine/BatchPlanBuild");
      plan = LlamaBatchBuilder::build(ticket, decode_inputs);
    } catch (const std::exception& error) {
      return failure(FailureClass::RequestRecoverable, error.what(),
                     GrowthResolution::NotExecuted);
    }
    if (plan.tokens.size() > caps.max_batch_tokens) {
      return failure(FailureClass::RequestRecoverable, "batch token capacity exceeded",
                     GrowthResolution::NotExecuted);
    }

    try {
      INFERSCALE_NVTX_RANGE("LlamaEngine/BatchPopulate");
      LlamaBatchBuilder::populate_llama_batch(batch, plan, batch_tokens.size());
    } catch (const std::exception& error) {
      return failure(FailureClass::RequestRecoverable, error.what(),
                     GrowthResolution::NotExecuted);
    }
    int status = 0;
    {
      INFERSCALE_NVTX_RANGE(backend_range_name);
      status = batch.n_tokens == 0 ? 0 : llama_decode(context, batch);
      llama_synchronize(context);
    }
    if (status != 0) {
      const auto kind = status == 1 || status == -1 ? FailureClass::RequestRecoverable
                                                    : FailureClass::EngineRestartRequired;
      return failure(kind, "llama_decode failed with status " + std::to_string(status),
                     GrowthResolution::Unknown);
    }

    EngineResult result;
    result.outputs.resize(ticket.entries().size());
    result.growth_resolution = GrowthResolution::Committed;
    result.committed_kv_growth.resize(ticket.entries().size());
    for (std::size_t index = 0; index < ticket.entries().size(); ++index) {
      const auto& entry = ticket.entries()[index];
      result.committed_kv_growth[index] = entry.work.kind == WorkKind::Prefill
          ? entry.work.token_count
          : (plan.logits_index_by_entry[index] ? 1U : 0U);
    }
    try {
      INFERSCALE_NVTX_RANGE("LlamaEngine/SamplingPostprocess");
      for (std::size_t index = 0; index < ticket.entries().size(); ++index) {
        const auto& entry = ticket.entries()[index];
        auto& state = sequences.at(entry.sequence);
        if (entry.work.kind == WorkKind::Prefill) {
          INFERSCALE_NVTX_RANGE("LlamaEngine/PrefillChunkPostprocess");
          state.prefetched += entry.work.token_count;
          if (plan.logits_index_by_entry[index]) {
            state.pending_token = sample(*plan.logits_index_by_entry[index]);
            state.has_pending = true;
            state.pending_eog = llama_vocab_is_eog(vocab, state.pending_token);
          }
          continue;
        }

        INFERSCALE_NVTX_RANGE("LlamaEngine/DecodePostprocess");
        if (state.pending_eog) {
          result.outputs[index] = EngineOutput{state.utf8.finish(), true};
          state.has_pending = false;
          state.pending_eog = false;
          continue;
        }

        const auto input_index = plan.logits_index_by_entry[index];
        if (!input_index) throw std::runtime_error("decode input did not request successor logits");
        auto piece = state.utf8.feed(token_piece(state.pending_token));
        ++state.emitted;
        if (state.emitted >= entry.max_output_tokens) piece += state.utf8.finish();
        result.outputs[index] = EngineOutput{std::move(piece), false};
        state.pending_token = sample(*input_index);
        state.pending_eog = llama_vocab_is_eog(vocab, state.pending_token);
      }
    } catch (const std::exception& error) {
      result.ok = false;
      result.failure = FailureClass::EngineRestartRequired;
      result.message = error.what();
      return result;
    }
    for (const auto& output : result.outputs) {
      if (output && output->piece.size() > caps.max_output_piece_bytes) {
        result.ok = false;
        result.failure = FailureClass::EngineRestartRequired;
        result.message = "decoded piece exceeded advertised bound";
        return result;
      }
    }
    return result;
  }

  LlamaEngineConfig config;
  EngineCapabilities caps{};
  llama_model* model{};
  const llama_vocab* vocab{};
  llama_context* context{};
  llama_sampler* sampler{};
  llama_batch batch{};
  bool backend_acquired{false};
  std::vector<llama_token> batch_tokens;
  std::vector<llama_pos> batch_positions;
  std::vector<std::int32_t> batch_sequence_counts;
  std::vector<llama_seq_id> batch_sequence_ids;
  std::vector<llama_seq_id*> batch_sequence_ptrs;
  std::vector<std::int8_t> batch_logits;
  mutable std::mutex mutex;
  std::unordered_map<SequenceId, SequenceState> sequences;
};

LlamaEngine::LlamaEngine(LlamaEngineConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
LlamaEngine::~LlamaEngine() = default;

EngineCapabilities LlamaEngine::capabilities() const { return impl_->caps; }

std::vector<TokenId> LlamaEngine::tokenize(std::string_view prompt) const {
  std::lock_guard lock(impl_->mutex);
  if (prompt.size() > static_cast<std::size_t>(INT_MAX)) {
    throw std::invalid_argument("prompt exceeds llama.cpp tokenizer input range");
  }
  const std::string text(prompt);
  auto count = llama_tokenize(impl_->vocab, text.data(), static_cast<std::int32_t>(text.size()),
                              nullptr, 0, true, true);
  if (count == INT32_MIN) throw std::runtime_error("prompt tokenization overflow");
  if (count >= 0) {
    if (count == 0) return {};
  } else {
    count = -count;
  }
  std::vector<TokenId> tokens(static_cast<std::size_t>(count));
  const auto actual = llama_tokenize(impl_->vocab, text.data(), static_cast<std::int32_t>(text.size()),
                                     tokens.data(), count, true, true);
  if (actual < 0) throw std::runtime_error("llama.cpp tokenizer capacity changed unexpectedly");
  tokens.resize(static_cast<std::size_t>(actual));
  return tokens;
}

EngineResult LlamaEngine::execute(const BatchTicket& ticket) { return impl_->execute(ticket); }

BackendMemoryStats LlamaEngine::memory_stats() const {
  std::lock_guard lock(impl_->mutex);
  std::uint64_t total = 0;
  for (const auto& [sequence, state] : impl_->sequences) {
    (void)sequence;
    total += state.prefetched + state.emitted;
  }
  return {total, static_cast<std::uint32_t>(impl_->sequences.size())};
}

bool LlamaEngine::clear_sequence(SequenceId sequence) {
  std::lock_guard lock(impl_->mutex);
  llama_synchronize(impl_->context);
  auto memory = llama_get_memory(impl_->context);
  const bool removed = llama_memory_seq_rm(memory, static_cast<llama_seq_id>(sequence), 0, -1);
  if (!removed || llama_memory_seq_pos_max(memory, static_cast<llama_seq_id>(sequence)) >= 0) {
    return false;
  }
  impl_->sequences.erase(sequence);
  return true;
}

}  // namespace inferscale
