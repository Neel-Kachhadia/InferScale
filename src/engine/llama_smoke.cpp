#include "llama.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct BackendGuard {
  ~BackendGuard() { llama_backend_free(); }
};

struct BatchGuard {
  llama_batch value{};
  ~BatchGuard() {
    if (value.token != nullptr || value.embd != nullptr) llama_batch_free(value);
  }
};

int fail(const char* message) {
  std::fprintf(stderr, "inferscale_llama_smoke: %s\n", message);
  return 1;
}

bool set_batch_token(llama_batch& batch, std::int32_t index, llama_token token,
                     llama_pos position, llama_seq_id sequence, bool want_logits) {
  if (index < 0 || index >= batch.n_tokens) return false;
  batch.token[index] = token;
  batch.pos[index] = position;
  batch.n_seq_id[index] = 1;
  batch.seq_id[index][0] = sequence;
  batch.logits[index] = want_logits ? 1 : 0;
  return true;
}

bool print_piece(const llama_vocab* vocab, llama_token token) {
  std::vector<char> buffer(128);
  for (;;) {
    const auto written = llama_token_to_piece(vocab, token, buffer.data(),
                                               static_cast<std::int32_t>(buffer.size()), 0, true);
    if (written >= 0) {
      std::fwrite(buffer.data(), 1, static_cast<std::size_t>(written), stdout);
      return true;
    }
    if (written == std::numeric_limits<std::int32_t>::min() || buffer.size() >= 1024 * 1024) return false;
    buffer.resize(static_cast<std::size_t>(-written));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    std::fprintf(stderr, "usage: %s MODEL.gguf [PROMPT] [GPU_LAYERS]\n", argv[0]);
    return 2;
  }
  const std::string path = argv[1];
  const std::string prompt = argc >= 3 ? argv[2] : "A short direct libllama smoke test.";
  int gpu_layers = 0;
  if (argc == 4) {
    try {
      gpu_layers = std::stoi(argv[3]);
    } catch (...) {
      return fail("GPU_LAYERS must be an integer");
    }
    if (gpu_layers < 0) return fail("GPU_LAYERS must be non-negative");
  }
  if (prompt.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    return fail("prompt exceeds llama.cpp's int32 tokenizer limit");
  }

  ggml_backend_load_all();
  llama_backend_init();
  BackendGuard backend_guard;

  auto model_params = llama_model_default_params();
  model_params.n_gpu_layers = gpu_layers;
  std::unique_ptr<llama_model, decltype(&llama_model_free)> model(
      llama_model_load_from_file(path.c_str(), model_params), &llama_model_free);
  if (!model) return fail("could not load GGUF model");

  const auto* vocab = llama_model_get_vocab(model.get());
  if (vocab == nullptr) return fail("model has no vocabulary");
  const auto text_size = static_cast<std::int32_t>(prompt.size());
  const auto token_probe = llama_tokenize(vocab, prompt.data(), text_size, nullptr, 0, true, true);
  if (token_probe == std::numeric_limits<std::int32_t>::min() || token_probe >= 0) {
    return fail("prompt tokenization failed or overflowed");
  }
  const auto token_count = -token_probe;
  if (token_count <= 0 || token_count > 32768) return fail("invalid or excessive tokenized prompt length");
  std::vector<llama_token> tokens(static_cast<std::size_t>(token_count));
  const auto tokenized = llama_tokenize(vocab, prompt.data(), text_size, tokens.data(), token_count, true, true);
  if (tokenized != token_count) return fail("tokenization result changed between sizing and fill");

  constexpr std::int32_t max_new_tokens = 4;
  auto context_params = llama_context_default_params();
  context_params.n_ctx = static_cast<std::uint32_t>(token_count + max_new_tokens + 1);
  context_params.n_batch = static_cast<std::uint32_t>(token_count);
  context_params.n_ubatch = std::min(context_params.n_ubatch, context_params.n_batch);
  context_params.n_seq_max = 1;
  std::unique_ptr<llama_context, decltype(&llama_free)> context(
      llama_init_from_model(model.get(), context_params), &llama_free);
  if (!context) return fail("could not create llama context");
  if (llama_n_ctx_seq(context.get()) < static_cast<std::uint32_t>(token_count + max_new_tokens) ||
      llama_n_seq_max(context.get()) < 1 || llama_n_batch(context.get()) < tokens.size()) {
    return fail("backend did not provide the requested context/batch capacity");
  }

  auto sampler_params = llama_sampler_chain_default_params();
  auto* raw_sampler = llama_sampler_chain_init(sampler_params);
  if (raw_sampler == nullptr) return fail("could not create sampler");
  std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler(raw_sampler, &llama_sampler_free);
  llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());

  BatchGuard batch;
  batch.value = llama_batch_init(token_count, 0, 1);
  if (batch.value.token == nullptr || batch.value.pos == nullptr || batch.value.n_seq_id == nullptr ||
      batch.value.seq_id == nullptr || batch.value.logits == nullptr) {
    return fail("could not allocate backend batch");
  }
  batch.value.n_tokens = token_count;
  for (std::int32_t i = 0; i < token_count; ++i) {
    if (!set_batch_token(batch.value, i, tokens[static_cast<std::size_t>(i)], i, 0, i == token_count - 1)) {
      return fail("could not build prefill batch");
    }
  }
  const auto prefill_status = llama_decode(context.get(), batch.value);
  if (prefill_status != 0) {
    std::fprintf(stderr, "prefill decode returned %d\n", prefill_status);
    return 1;
  }

  std::uint32_t produced = 0;
  for (std::int32_t i = 0; i < max_new_tokens; ++i) {
    const auto token = llama_sampler_sample(sampler.get(), context.get(), -1);
    if (llama_vocab_is_eog(vocab, token)) break;
    if (!print_piece(vocab, token)) return fail("could not render sampled token");
    std::putchar(' ');
    ++produced;
    batch.value.n_tokens = 1;
    if (!set_batch_token(batch.value, 0, token, token_count + i, 0, true)) {
      return fail("could not build decode batch");
    }
    const auto decode_status = llama_decode(context.get(), batch.value);
    if (decode_status != 0) {
      std::fprintf(stderr, "decode returned %d\n", decode_status);
      return 1;
    }
  }
  if (produced == 0) return fail("model returned no printable output");
  std::putchar('\n');

  llama_synchronize(context.get());
  const auto memory = llama_get_memory(context.get());
  if (memory == nullptr || !llama_memory_seq_rm(memory, 0, 0, -1)) {
    return fail("could not clear backend sequence");
  }
  if (llama_memory_seq_pos_max(memory, 0) >= 0) return fail("backend sequence remained populated after clear");
  std::fprintf(stderr, "direct libllama smoke passed: prompt_tokens=%d output_tokens=%u\n",
               token_count, produced);
  return 0;
}
