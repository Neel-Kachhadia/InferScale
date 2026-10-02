#include "inferscale/engine/llama_batch_builder.hpp"
#include "inferscale/engine/llama_engine.hpp"
#include "inferscale/runtime/clock.hpp"
#include "inferscale/runtime/runtime_reactor.hpp"

#include <llama.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace inferscale;

struct RetireTicket {
  BatchTicket& ticket;
  ~RetireTicket() { (void)ticket.retire(); }
};

bool valid_utf8(std::string_view value) {
  for (std::size_t offset = 0; offset < value.size();) {
    const auto lead = static_cast<unsigned char>(value[offset]);
    const std::size_t count = lead <= 0x7fU ? 1U : lead >= 0xc2U && lead <= 0xdfU ? 2U
                               : lead >= 0xe0U && lead <= 0xefU                ? 3U
                               : lead >= 0xf0U && lead <= 0xf4U                ? 4U : 0U;
    if (count == 0 || !Utf8StreamDecoder::is_valid(value, offset, count)) return false;
    offset += count;
  }
  return true;
}

TEST(LlamaBatchBuilderTest, PopulatesPinnedBatchWithFlattenedLogitIndicesAndPositions) {
  auto first_prompt = std::make_shared<const std::vector<TokenId>>(
      std::vector<TokenId>{10, 11, 12, 13, 14, 15, 16, 17});
  auto second_prompt = std::make_shared<const std::vector<TokenId>>(
      std::vector<TokenId>{20, 21, 22, 23});
  std::vector<TicketEntry> entries{
      {{{1, 1}, WorkKind::Decode, 8, 1, 1}, 2, first_prompt, 4},
      {{{2, 1}, WorkKind::Prefill, 2, 2, 2}, 7, second_prompt, 4},
  };
  BatchTicket ticket(1, std::move(entries));
  RetireTicket retire{ticket};
  const std::vector<std::optional<TokenId>> decode_inputs{TokenId{31}, std::nullopt};
  const auto plan = LlamaBatchBuilder::build(ticket, decode_inputs);

  ASSERT_EQ(plan.tokens.size(), 3U);
  EXPECT_EQ(plan.tokens[0].token, 31);
  EXPECT_EQ(plan.tokens[0].position, 8);
  EXPECT_EQ(plan.tokens[0].sequence, 2);
  EXPECT_EQ(plan.tokens[0].flat_index, 0U);
  EXPECT_EQ(plan.tokens[0].logits_index, 0U);
  EXPECT_EQ(plan.tokens[1].token, 22);
  EXPECT_EQ(plan.tokens[1].position, 2);
  EXPECT_EQ(plan.tokens[1].sequence, 7);
  EXPECT_FALSE(plan.tokens[1].logits_index.has_value());
  EXPECT_EQ(plan.tokens[2].token, 23);
  EXPECT_EQ(plan.tokens[2].position, 3);
  EXPECT_EQ(plan.tokens[2].sequence, 7);
  EXPECT_EQ(plan.tokens[2].flat_index, 2U);
  EXPECT_EQ(plan.tokens[2].logits_index, 2U);
  ASSERT_EQ(plan.logits_index_by_entry.size(), 2U);
  EXPECT_EQ(plan.logits_index_by_entry[0], 0U);
  EXPECT_EQ(plan.logits_index_by_entry[1], 2U);
  EXPECT_EQ(plan.logits_count, 2U);

  llama_batch batch = llama_batch_init(3, 0, 1);
  ASSERT_NE(batch.token, nullptr);
  LlamaBatchBuilder::populate_llama_batch(batch, plan, 3);
  EXPECT_EQ(batch.n_tokens, 3);
  EXPECT_EQ(batch.token[0], 31);
  EXPECT_EQ(batch.pos[0], 8);
  EXPECT_EQ(batch.n_seq_id[0], 1);
  EXPECT_EQ(batch.seq_id[0][0], 2);
  EXPECT_EQ(batch.logits[0], 1);
  EXPECT_EQ(batch.token[1], 22);
  EXPECT_EQ(batch.pos[1], 2);
  EXPECT_EQ(batch.seq_id[1][0], 7);
  EXPECT_EQ(batch.logits[1], 0);
  EXPECT_EQ(batch.token[2], 23);
  EXPECT_EQ(batch.pos[2], 3);
  EXPECT_EQ(batch.seq_id[2][0], 7);
  EXPECT_EQ(batch.logits[2], 1);
  llama_batch_free(batch);
}

TEST(LlamaBatchBuilderTest, DecodePositionIsPromptLengthAndDuplicateEntriesAreRejected) {
  auto prompt = std::make_shared<const std::vector<TokenId>>(std::vector<TokenId>{5, 6, 7});
  BatchTicket ticket(2, {{{{9, 1}, WorkKind::Decode, 3, 1, 1}, 0, prompt, 4}});
  RetireTicket retire{ticket};
  const std::vector<std::optional<TokenId>> input{TokenId{8}};
  const auto plan = LlamaBatchBuilder::build(ticket, input);
  ASSERT_EQ(plan.tokens.size(), 1U);
  EXPECT_EQ(plan.tokens[0].position, 3);

  std::vector<TicketEntry> duplicate{
      {{{9, 1}, WorkKind::Prefill, 0, 1, 1}, 0, prompt, 4},
      {{{10, 1}, WorkKind::Prefill, 0, 1, 1}, 0, prompt, 4},
  };
  BatchTicket duplicate_ticket(3, std::move(duplicate));
  RetireTicket retire_duplicate{duplicate_ticket};
  EXPECT_THROW((void)LlamaBatchBuilder::build(duplicate_ticket), std::invalid_argument);
}

TEST(LlamaEngineIntegration, RealModelBackpressureCancellationAndSequenceReuse) {
  const auto* model_path = std::getenv("INFERSCALE_TEST_MODEL");
  if (model_path == nullptr || *model_path == '\0') {
    GTEST_SKIP() << "set INFERSCALE_TEST_MODEL to run the real-model integration check";
  }

  LlamaEngineConfig config;
  config.model_path = model_path;
  if (const auto* layers = std::getenv("INFERSCALE_TEST_GPU_LAYERS")) config.gpu_layers = std::stoi(layers);
  config.context_per_sequence = 2048;
  config.max_sequences = 2;
  config.batch_tokens = 512;
  config.ubatch_tokens = 128;
  config.threads = 4;
  config.batch_threads = 4;
  LlamaEngine engine(config);
  const auto caps = engine.capabilities();
  ASSERT_TRUE(caps.cuda_enabled || config.gpu_layers == 0);
  SteadyClock clock;
  RuntimeReactor reactor(clock, caps.max_sequences, 64, caps.context_limit,
                         caps.max_output_piece_bytes);
  const auto capacity = caps.max_output_piece_bytes * 2U;

  const auto admit = [&](std::uint64_t id, std::string_view prompt,
                         std::uint32_t output_tokens, std::uint32_t generation_hint) {
    auto token_ids = std::make_shared<const std::vector<TokenId>>(engine.tokenize(prompt));
    auto stream = std::make_shared<StreamBuffer>(capacity);
    RequestSpec spec{id, static_cast<std::uint32_t>(token_ids->size()), output_tokens,
                     std::nullopt, capacity, token_ids};
    if (!reactor.post(NewRequestEvent{std::move(spec), generation_hint, stream, {}})) {
      throw std::runtime_error("real-model integration admission event was rejected");
    }
    reactor.run_ready();
    const auto key = reactor.latest_key(id);
    if (!key) throw std::runtime_error("real-model integration request was not admitted");
    return std::pair{*key, std::move(stream)};
  };
  const auto clear_retired = [&] {
    const auto clears = reactor.take_clear_requests();
    for (const auto& clear : clears) {
      const bool cleared = engine.clear_sequence(clear.sequence);
      EXPECT_TRUE(cleared);
      EXPECT_TRUE(reactor.post_completion(SequenceClearedEvent{clear.key, cleared}));
      reactor.run_ready();
    }
    return clears;
  };

  const std::string prompt = "<|system|>\nYou are a helpful assistant.\n<|user|>\nSay exactly: Paris.\n<|assistant|>\n";
  const auto [key, stream] = admit(9001, prompt, 128, 1);
  std::string rendered;
  bool saw_backpressure = false;
  bool saw_variable_pieces = false;
  std::uint32_t backpressure_pauses = 0;
  std::uint32_t backpressure_resumes = 0;
  std::size_t max_buffered_bytes = 0;
  std::optional<std::size_t> first_piece_size;
  std::uint64_t steps = 0;
  for (; steps < 1024 && !stream->closed(); ++steps) {
    reactor.run_ready();
    auto plan = reactor.make_fifo_plan(caps.max_batch_tokens, caps.max_sequences,
                                       caps.max_prefill_chunk_tokens);
    if (plan.items.empty()) {
      const auto snapshot = reactor.snapshot(key);
      if (!snapshot || !snapshot->stream_backpressured) break;
      saw_backpressure = true;
      ++backpressure_pauses;
      const auto buffered = stream->bytes();
      max_buffered_bytes = std::max(max_buffered_bytes, buffered);
      EXPECT_LE(buffered, stream->capacity());
      EXPECT_LT(stream->capacity() - buffered, caps.max_output_piece_bytes);
      const auto consumed = stream->consume(512);
      ASSERT_FALSE(consumed.empty());
      rendered += consumed;
      EXPECT_TRUE(reactor.post(StreamWritableEvent{key}));
      reactor.run_ready();
      EXPECT_FALSE(reactor.make_fifo_plan(caps.max_batch_tokens, caps.max_sequences,
                                          caps.max_prefill_chunk_tokens).items.empty());
      ++backpressure_resumes;
      continue;
    }
    auto ticket = reactor.issue(plan);
    ASSERT_TRUE(ticket.has_value());
    auto result = engine.execute(*ticket);
    ASSERT_TRUE(result.ok) << result.message;
    for (const auto& output : result.outputs) {
      if (!output || output->piece.empty()) continue;
      EXPECT_TRUE(valid_utf8(output->piece));
      if (first_piece_size && *first_piece_size != output->piece.size()) saw_variable_pieces = true;
      else if (!first_piece_size) first_piece_size = output->piece.size();
    }
    EXPECT_TRUE(reactor.post_completion(BatchCompletedEvent{std::move(*ticket), std::move(result)}));
    reactor.run_ready();
    EXPECT_LE(stream->bytes(), stream->capacity());
    clear_retired();
  }
  ASSERT_TRUE(stream->closed());
  rendered += stream->consume(4096);
  EXPECT_EQ(stream->end_reason(), StreamEnd::Complete);
  EXPECT_TRUE(saw_backpressure);
  EXPECT_TRUE(saw_variable_pieces);
  EXPECT_FALSE(rendered.empty());
  EXPECT_LE(stream->usage().output_tokens, 128U);
  EXPECT_EQ(reactor.live_sequences(), 0U);
  EXPECT_EQ(reactor.take_clear_requests().size(), 0U);
  std::cout << "REAL MODEL BUFFER METRICS: capacity_bytes=" << capacity
            << " high_water_bytes=" << max_buffered_bytes
            << " pauses=" << backpressure_pauses << " resumes=" << backpressure_resumes
            << " output_tokens=" << stream->usage().output_tokens
            << " variable_token_pieces=" << (saw_variable_pieces ? "true" : "false") << '\n';

  const auto [cancel_key, cancel_stream] = admit(9001, prompt, 128, 2);
  ASSERT_EQ(cancel_key.generation, key.generation + 1U);
  auto cancelled_ticket = reactor.issue(reactor.make_fifo_plan(caps.max_batch_tokens, 1,
                                                                 caps.max_prefill_chunk_tokens));
  ASSERT_TRUE(cancelled_ticket.has_value());
  const auto original_sequence = cancelled_ticket->entries().front().sequence;
  ASSERT_TRUE(reactor.cancel(cancel_key));
  EXPECT_EQ(reactor.live_sequences(), 1U);
  EXPECT_TRUE(reactor.take_clear_requests().empty());
  EXPECT_TRUE(reactor.make_fifo_plan(caps.max_batch_tokens, 1,
                                     caps.max_prefill_chunk_tokens).items.empty());
  auto canceled_result = engine.execute(*cancelled_ticket);
  ASSERT_TRUE(canceled_result.ok) << canceled_result.message;
  ASSERT_TRUE(reactor.post_completion(BatchCompletedEvent{std::move(*cancelled_ticket),
                                                            std::move(canceled_result)}));
  reactor.run_ready();
  EXPECT_EQ(cancel_stream->end_reason(), StreamEnd::Cancelled);
  EXPECT_EQ(cancel_stream->bytes(), 0U);
  const auto cancelled_clears = clear_retired();
  ASSERT_EQ(cancelled_clears.size(), 1U);
  EXPECT_EQ(cancelled_clears.front().sequence, original_sequence);
  EXPECT_EQ(reactor.live_sequences(), 0U);

  const std::string short_prompt =
      "<|system|>\nYou are a helpful assistant.\n<|user|>\nReply with one word: hello.\n<|assistant|>\n";
  const auto [reused_key, reused_stream] = admit(9001, short_prompt, 128, 3);
  ASSERT_EQ(reused_key.generation, cancel_key.generation + 1U);
  auto reused_ticket = reactor.issue(reactor.make_fifo_plan(caps.max_batch_tokens, 1,
                                                            caps.max_prefill_chunk_tokens));
  ASSERT_TRUE(reused_ticket.has_value());
  EXPECT_EQ(reused_ticket->entries().front().sequence, original_sequence);
  auto prefill_result = engine.execute(*reused_ticket);
  ASSERT_TRUE(prefill_result.ok) << prefill_result.message;
  ASSERT_TRUE(reactor.post_completion(BatchCompletedEvent{std::move(*reused_ticket),
                                                            std::move(prefill_result)}));
  reactor.run_ready();
  clear_retired();
  for (std::uint32_t count = 0; count < 64 && !reused_stream->closed(); ++count) {
    auto decode = reactor.issue(reactor.make_fifo_plan(caps.max_batch_tokens, 1,
                                                       caps.max_prefill_chunk_tokens));
    ASSERT_TRUE(decode.has_value());
    auto decode_result = engine.execute(*decode);
    ASSERT_TRUE(decode_result.ok) << decode_result.message;
    ASSERT_TRUE(reactor.post_completion(BatchCompletedEvent{std::move(*decode),
                                                              std::move(decode_result)}));
    reactor.run_ready();
    (void)reused_stream->consume(512);
    clear_retired();
  }
  EXPECT_TRUE(reused_stream->closed());
  EXPECT_EQ(reused_stream->end_reason(), StreamEnd::Complete);
  EXPECT_EQ(reused_stream->usage().output_tokens, 3U);
  EXPECT_EQ(reactor.live_sequences(), 0U);
}

}  // namespace
