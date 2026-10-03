#include "inferscale/engine/error_classifier.hpp"
#include "inferscale/engine/fake_engine.hpp"
#include "inferscale/runtime/bounded_queue.hpp"
#include "inferscale/runtime/lifecycle.hpp"
#include "inferscale/runtime/runtime_reactor.hpp"
#include "inferscale/runtime/sequence_allocator.hpp"
#include "inferscale/runtime/stream_buffer.hpp"
#include "inferscale/scheduler/fifo.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<int> checks = 0;

void check(bool value, const char* expression, int line) {
  ++checks;
  if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

using namespace inferscale;

void lifecycle_transitions() {
  RequestState state = RequestState::Received;
  CHECK(transition(state, RequestState::PrefillReady));
  CHECK(transition(state, RequestState::PrefillInFlight));
  CHECK(transition(state, RequestState::CancelPendingRetire));
  CHECK(!transition(state, RequestState::DecodeReady));
  CHECK(transition(state, RequestState::Terminal));
  CHECK(!transition(state, RequestState::PrefillReady));
}

void bounded_queue_is_fifo_and_rejects_overflow() {
  BoundedQueue<int> queue(2);
  CHECK(queue.try_push(10));
  CHECK(queue.try_push(20));
  CHECK(!queue.try_push(30));
  CHECK(queue.size() == 2);
  CHECK(queue.try_pop() == 10);
  CHECK(queue.try_pop() == 20);
  CHECK(!queue.try_pop());
  queue.close();
  CHECK(!queue.try_push(40));
}

void sequence_allocator_waits_for_retirement_and_clear_ack() {
  SequenceAllocator allocator(1);
  const RequestKey old{42, 7};
  const RequestKey next{42, 8};
  const auto sequence = allocator.allocate(old);
  CHECK(sequence.has_value());
  CHECK(!allocator.allocate(next));
  CHECK(allocator.pin(old));
  CHECK(!allocator.begin_clear(old));
  CHECK(allocator.retire(old));
  CHECK(allocator.begin_clear(old));
  CHECK(allocator.clear_result(old, false));
  CHECK(!allocator.allocate(next));
  CHECK(allocator.begin_clear(old));
  CHECK(allocator.clear_result(old, true));
  CHECK(allocator.allocate(next) == sequence);
  CHECK(!allocator.allocate(old));
}

void impossible_context_is_rejected_before_sequence_allocation() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1, 16, 10);
  auto admission = std::make_shared<std::atomic<AdmissionResult>>(AdmissionResult::Pending);
  CHECK(reactor.post(NewRequestEvent{RequestSpec{8, 8, 3, std::nullopt, 64}, 0, {}, admission}));
  reactor.run_ready();
  CHECK(admission->load(std::memory_order_acquire) == AdmissionResult::Rejected);
  CHECK(reactor.live_sequences() == 0);
  CHECK(!reactor.latest_key(8));
}

void ticket_is_move_only_and_retires_once() {
  BatchTicket ticket(1, {{{{9, 1}, WorkKind::Decode, 0, 1, 1}, 3}});
  BatchTicket moved(std::move(ticket));
  CHECK(moved.id() == 1);
  CHECK(moved.retire());
  CHECK(!moved.retire());
}

void fifo_plan_is_stable_and_exact() {
  const std::vector<ReadyRequest> ready{
      {{1, 1}, WorkKind::Prefill, 0, 100},
      {{2, 1}, WorkKind::Decode, 4, 1},
      {{3, 1}, WorkKind::Prefill, 32, 8}};
  const auto actual = fifo_plan(ready, 6, 2, 4);
  CHECK(valid_work_plan(actual));
  CHECK(actual.total_tokens == 5);
  CHECK(actual.sequence_count == 2);
  CHECK(actual.items[0].key == (RequestKey{1, 1}));
  CHECK(actual.items[0].token_count == 4);
  CHECK(actual.items[1].key == (RequestKey{2, 1}));
}

void stream_buffer_is_bounded_and_partial_reads_keep_order() {
  StreamBuffer stream(5);
  CHECK(stream.try_push("abc"));
  CHECK(!stream.try_push("def"));
  CHECK(stream.consume(2) == "ab");
  CHECK(stream.bytes() == 1);
  CHECK(stream.try_push("de"));
  CHECK(stream.consume(5) == "cde");
  CHECK(stream.bytes() == 0);
  stream.close();
  CHECK(stream.closed());
  CHECK(!stream.try_push("x"));
}

void stream_close_preserves_final_data_in_atomic_drain() {
  StreamBuffer stream(16);
  CHECK(stream.try_push("last token "));
  stream.close(StreamEnd::Complete);
  const auto final = stream.consume_with_status(16);
  CHECK(final.data == "last token ");
  CHECK(final.closed);
  const auto empty = stream.consume_with_status(16);
  CHECK(empty.data.empty());
  CHECK(empty.closed);
}

void utf8_pieces_and_http_drains_preserve_codepoints() {
  Utf8StreamDecoder decoder;
  CHECK(decoder.feed("\xe2\x82").empty());
  CHECK(decoder.feed("\xac") == "\xe2\x82\xac");
  CHECK(decoder.finish().empty());

  StreamBuffer stream(1024);
  CHECK(stream.try_push(std::string(511, 'a')));
  CHECK(stream.try_push("\xe2"));
  CHECK(stream.try_push("\x82\xac"));
  stream.close(StreamEnd::Complete);
  const auto first = stream.consume_with_status(512);
  CHECK(first.data.size() == 511);
  CHECK(first.closed);
  const auto second = stream.consume_with_status(512);
  CHECK(second.data == "\xe2\x82\xac");
  CHECK(second.closed);
  CHECK(stream.bytes() == 0);
}

void error_classifier_is_conservative() {
  CHECK(classify_engine_error(-1, true) == FailureClass::RequestRecoverable);
  CHECK(classify_engine_error(1, false) == FailureClass::EngineRecoverable);
  CHECK(classify_engine_error(2, false) == FailureClass::EngineRestartRequired);
  CHECK(classify_engine_error(99, false) == FailureClass::ProcessFatal);
}

void finish(RuntimeReactor& reactor, FakeEngine& engine, BatchTicket ticket) {
  auto result = engine.execute(ticket);
  CHECK(reactor.post(BatchCompletedEvent{std::move(ticket), std::move(result)}));
  reactor.run_ready();
}

void ack_clears(RuntimeReactor& reactor, FakeEngine& engine) {
  const auto clears = reactor.take_clear_requests();
  for (const auto& clear : clears) {
    const bool result = engine.clear_sequence(clear.sequence);
    CHECK(reactor.post(SequenceClearedEvent{clear.key, result}));
  }
  reactor.run_ready();
}

RequestKey add_request(RuntimeReactor& reactor, std::uint64_t id, std::uint32_t prompt,
                       std::uint32_t output, std::uint32_t stream_bytes = 128,
                       std::uint32_t generation_hint = 0,
                       RequestClass service_class = RequestClass::Interactive) {
  RequestSpec spec{id, prompt, output, std::nullopt, stream_bytes};
  spec.service_class = service_class;
  CHECK(reactor.post(NewRequestEvent{std::move(spec), generation_hint, {}, {}}));
  reactor.run_ready();
  const auto key = reactor.latest_key(id);
  CHECK(key.has_value());
  return *key;
}

void fake_engine_runs_fifo_then_clears_before_reuse() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 2);
  FakeEngine engine({2, 8, 64, false});
  const auto first = add_request(reactor, 42, 2, 2);
  const auto second = add_request(reactor, 43, 1, 1);

  auto plan = reactor.make_fifo_plan(3, 2, 2);
  CHECK(plan.items.size() == 2);
  CHECK(plan.items[0].key == first);
  CHECK(plan.items[1].key == second);
  auto ticket = reactor.issue(plan);
  CHECK(ticket.has_value());
  finish(reactor, engine, std::move(*ticket));
  CHECK(reactor.snapshot(first)->state == RequestState::DecodeReady);
  CHECK(reactor.snapshot(second)->state == RequestState::DecodeReady);

  auto decode = reactor.make_fifo_plan(2, 2, 2);
  CHECK(decode.items[0].kind == WorkKind::Decode);
  auto decode_ticket = reactor.issue(decode);
  CHECK(decode_ticket.has_value());
  finish(reactor, engine, std::move(*decode_ticket));
  CHECK(reactor.snapshot(first)->generated_tokens == 1);
  CHECK(reactor.snapshot(second)->state == RequestState::CompletePendingRetire);
  CHECK(reactor.stream_for(second)->closed());
  CHECK(reactor.stream_for(second)->usage().prompt_tokens == 1);
  CHECK(reactor.stream_for(second)->usage().output_tokens == 1);
  ack_clears(reactor, engine);
  CHECK(reactor.live_sequences() == 1);
}

void chunked_prefill_ranges_are_contiguous_and_commit_after_backend_success() {
  for (const auto prompt_tokens : {3U, 4U, 5U, 10U}) {
    ManualClock clock;
    RuntimeConfig config;
    config.hard_usable_kv_tokens = 128;
    config.kv_safety_margin_tokens = 0;
    config.admission_generation_reserve_tokens = 0;
    RuntimeReactor reactor(clock, 1, 32, 128, 11, config);
    FakeEngine engine({1, 4, 128, false, 11});
    const auto key = add_request(reactor, 5000 + prompt_tokens, prompt_tokens, 1);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    std::uint32_t committed_cursor = 0;
    while (committed_cursor < prompt_tokens) {
      const auto plan = reactor.make_fifo_plan(4, 1, 4, 4);
      ASSERT_EQ(plan.items.size(), 1U);
      const auto& item = plan.items.front();
      EXPECT_EQ(item.kind, WorkKind::Prefill);
      EXPECT_EQ(item.token_begin, committed_cursor);
      const auto expected_count = std::min(4U, prompt_tokens - committed_cursor);
      EXPECT_EQ(item.token_count, expected_count);
      EXPECT_EQ(reactor.snapshot(key)->prefill_cursor, committed_cursor);
      ranges.emplace_back(item.token_begin, item.token_begin + item.token_count);

      auto ticket = reactor.issue(plan);
      ASSERT_TRUE(ticket.has_value());
      EXPECT_EQ(reactor.snapshot(key)->prefill_cursor, committed_cursor);
      finish(reactor, engine, std::move(*ticket));
      committed_cursor += expected_count;
      ASSERT_TRUE(reactor.snapshot(key).has_value());
      EXPECT_EQ(reactor.snapshot(key)->prefill_cursor, committed_cursor);
    }

    const auto expected_chunk_count = (prompt_tokens + 3U) / 4U;
    ASSERT_EQ(ranges.size(), expected_chunk_count);
    for (std::size_t index = 0; index < ranges.size(); ++index) {
      EXPECT_EQ(ranges[index].first, index * 4U);
      EXPECT_EQ(ranges[index].second,
                std::min<std::uint32_t>(prompt_tokens,
                    static_cast<std::uint32_t>((index + 1U) * 4U)));
    }
    EXPECT_EQ(reactor.snapshot(key)->state, RequestState::DecodeReady);
    EXPECT_TRUE(reactor.cancel(key));
    ack_clears(reactor, engine);
    EXPECT_EQ(reactor.live_sequences(), 0U);
    EXPECT_EQ(reactor.metrics().charged_kv_tokens, 0U);
  }
}

void failed_prefill_chunk_does_not_advance_cursor_or_reschedule() {
  ManualClock clock;
  RuntimeConfig config;
  config.hard_usable_kv_tokens = 128;
  config.kv_safety_margin_tokens = 0;
  config.admission_generation_reserve_tokens = 0;
  RuntimeReactor reactor(clock, 1, 32, 128, 11, config);
  FakeEngine engine({1, 4, 128, false, 11});
  const auto key = add_request(reactor, 5010, 10, 1);
  engine.fail_next(FailureClass::RequestRecoverable);
  const auto plan = reactor.make_fifo_plan(4, 1, 4, 4);
  ASSERT_EQ(plan.items.front().token_begin, 0U);
  ASSERT_EQ(plan.items.front().token_count, 4U);
  auto ticket = reactor.issue(plan);
  ASSERT_TRUE(ticket.has_value());
  finish(reactor, engine, std::move(*ticket));
  const auto failed = reactor.snapshot(key);
  ASSERT_TRUE(failed.has_value());
  EXPECT_EQ(failed->prefill_cursor, 0U);
  EXPECT_EQ(failed->state, RequestState::FailedPendingRetire);
  EXPECT_TRUE(reactor.make_fifo_plan(4, 1, 4, 4).items.empty());
  EXPECT_EQ(reactor.metrics().committed_kv_tokens, 0U);
  EXPECT_EQ(reactor.metrics().available_sequences, 0U);
  ack_clears(reactor, engine);
  EXPECT_EQ(reactor.metrics().charged_kv_tokens, 0U);
  EXPECT_EQ(reactor.metrics().available_sequences, 1U);
}

void cancellation_between_prefill_chunks_stops_later_ranges() {
  ManualClock clock;
  RuntimeConfig config;
  config.hard_usable_kv_tokens = 128;
  config.kv_safety_margin_tokens = 0;
  config.admission_generation_reserve_tokens = 0;
  RuntimeReactor reactor(clock, 1, 32, 128, 11, config);
  FakeEngine engine({1, 4, 128, false, 11});
  const auto key = add_request(reactor, 5011, 10, 1);
  auto first = reactor.issue(reactor.make_fifo_plan(4, 1, 4, 4));
  ASSERT_TRUE(first.has_value());
  finish(reactor, engine, std::move(*first));
  ASSERT_EQ(reactor.snapshot(key)->prefill_cursor, 4U);
  auto second_plan = reactor.make_fifo_plan(4, 1, 4, 4);
  ASSERT_EQ(second_plan.items.front().token_begin, 4U);
  ASSERT_TRUE(reactor.cancel(key));
  EXPECT_TRUE(reactor.make_fifo_plan(4, 1, 4, 4).items.empty());
  ack_clears(reactor, engine);
  EXPECT_EQ(reactor.live_sequences(), 0U);
  EXPECT_EQ(reactor.metrics().charged_kv_tokens, 0U);
}

void cancellation_inflight_prefill_keeps_ticket_pinned_without_cursor_commit() {
  ManualClock clock;
  RuntimeConfig config;
  config.hard_usable_kv_tokens = 128;
  config.kv_safety_margin_tokens = 0;
  config.admission_generation_reserve_tokens = 0;
  RuntimeReactor reactor(clock, 1, 32, 128, 11, config);
  FakeEngine engine({1, 4, 128, false, 11});
  const auto key = add_request(reactor, 5012, 10, 1);
  engine.pause_after_execute();
  auto chunk = reactor.issue(reactor.make_fifo_plan(4, 1, 4, 4));
  ASSERT_TRUE(chunk.has_value());
  std::atomic<bool> completion_posted{false};
  std::thread executor([&reactor, &engine, ticket = std::move(*chunk),
                        &completion_posted]() mutable {
    auto result = engine.execute(ticket);
    completion_posted = reactor.post_completion(
        BatchCompletedEvent{std::move(ticket), std::move(result)});
  });
  const auto reached_barrier = engine.wait_until_after_execute_paused(
      std::chrono::seconds(3));
  const auto cancellation_posted = reactor.post(CancelEvent{key});
  reactor.run_ready();
  const auto during_cancel = reactor.snapshot(key);
  const bool ticket_pinned = during_cancel &&
      during_cancel->state == RequestState::CancelPendingRetire &&
      during_cancel->prefill_cursor == 0U && during_cancel->inflight_tickets == 1U;
  const auto no_new_chunk = reactor.make_fifo_plan(4, 1, 4, 4).items.empty();
  const auto no_early_clear = reactor.take_clear_requests().empty();
  engine.resume();
  executor.join();
  ASSERT_TRUE(reached_barrier);
  ASSERT_TRUE(cancellation_posted);
  ASSERT_TRUE(ticket_pinned);
  ASSERT_TRUE(no_new_chunk);
  ASSERT_TRUE(no_early_clear);
  ASSERT_TRUE(completion_posted.load());
  reactor.run_ready();
  EXPECT_EQ(reactor.snapshot(key)->prefill_cursor, 0U);
  EXPECT_EQ(reactor.snapshot(key)->inflight_tickets, 0U);
  const auto clears = reactor.take_clear_requests();
  ASSERT_EQ(clears.size(), 1U);
  EXPECT_EQ(reactor.live_sequences(), 1U);
  EXPECT_GT(reactor.metrics().charged_kv_tokens, 0U);
  EXPECT_TRUE(reactor.post(SequenceClearedEvent{
      clears.front().key, engine.clear_sequence(clears.front().sequence)}));
  reactor.run_ready();
  EXPECT_EQ(reactor.live_sequences(), 0U);
  EXPECT_EQ(reactor.metrics().charged_kv_tokens, 0U);
}

void cancellation_inflight_fences_stale_generation_and_discards_output() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto old = add_request(reactor, 42, 1, 3, 22, 7);
  auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(prefill.has_value());
  finish(reactor, engine, std::move(*prefill));
  auto decode = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(decode.has_value());
  CHECK(reactor.post(CancelEvent{old}));
  reactor.run_ready();
  CHECK(reactor.snapshot(old)->state == RequestState::CancelPendingRetire);
  CHECK(reactor.take_clear_requests().empty());
  finish(reactor, engine, std::move(*decode));
  CHECK(reactor.stream_for(old)->bytes() == 0);
  ack_clears(reactor, engine);

  const auto fresh = add_request(reactor, 42, 1, 1, 22, 8);
  CHECK(fresh == (RequestKey{42, 8}));
  const auto before = reactor.snapshot(fresh);
  CHECK(reactor.post(BatchCompletedEvent{
      BatchTicket(99, {{{old, WorkKind::Decode, 2, 1, 1}, 0}}),
      EngineResult{true, FailureClass::RequestRecoverable,
                   {EngineOutput{"777", false}}, {}}}));
  reactor.run_ready();
  CHECK(reactor.stale_event_count() == 1);
  CHECK(reactor.snapshot(fresh)->state == before->state);
  CHECK(reactor.stream_for(fresh)->bytes() == 0);
}

void duplicate_completion_cannot_retire_a_newer_ticket() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 61, 1, 3);
  auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(prefill.has_value());
  const auto stale_id = prefill->id();
  const auto stale_entries = prefill->entries();
  finish(reactor, engine, std::move(*prefill));

  auto current = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(current.has_value());
  CHECK(reactor.snapshot(key)->state == RequestState::DecodeInFlight);
  CHECK(reactor.post_completion(BatchCompletedEvent{
      BatchTicket(stale_id, stale_entries),
      EngineResult{true, FailureClass::RequestRecoverable,
                   {std::nullopt}, {}}}));
  reactor.run_ready();
  CHECK(reactor.stale_event_count() == 1);
  CHECK(reactor.snapshot(key)->state == RequestState::DecodeInFlight);
  CHECK(reactor.snapshot(key)->inflight_tickets == 1);
  finish(reactor, engine, std::move(*current));
  ack_clears(reactor, engine);
}

void mixed_eos_completes_only_its_own_request() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 2);
  FakeEngine engine({2, 8, 64, false});
  const auto first = add_request(reactor, 62, 1, 3);
  const auto second = add_request(reactor, 63, 1, 3);
  auto prefill = reactor.issue(reactor.make_fifo_plan(2, 2, 1));
  CHECK(prefill.has_value());
  finish(reactor, engine, std::move(*prefill));

  auto decode = reactor.issue(reactor.make_fifo_plan(2, 2, 1));
  CHECK(decode.has_value());
  auto result = engine.execute(*decode);
  CHECK(result.outputs.size() == 2);
  result.outputs[1] = EngineOutput{"", true};
  CHECK(reactor.post_completion(BatchCompletedEvent{std::move(*decode), std::move(result)}));
  reactor.run_ready();
  CHECK(reactor.snapshot(first)->state == RequestState::DecodeReady);
  CHECK(reactor.snapshot(first)->generated_tokens == 1);
  CHECK(reactor.snapshot(second)->state == RequestState::CompletePendingRetire);
  CHECK(reactor.stream_for(second)->closed());
  CHECK(reactor.stream_for(second)->usage().output_tokens == 0);
  CHECK(reactor.stream_for(second)->bytes() == 0);
  CHECK(!reactor.stream_for(first)->closed());
  ack_clears(reactor, engine);
  CHECK(reactor.live_sequences() == 1);
}

void deadlines_are_rechecked_at_ticket_issue_and_completion() {
  {
    ManualClock clock;
    RuntimeReactor reactor(clock, 1);
    const RequestKey expected{64, 1};
    CHECK(reactor.post(NewRequestEvent{
        RequestSpec{expected.id, 1, 1, clock.now() + std::chrono::seconds(1), 64}, 0, {}, {}}));
    reactor.run_ready();
    const auto key = reactor.latest_key(expected.id);
    CHECK(key == expected);
    const auto plan = reactor.make_fifo_plan(1, 1, 1);
    CHECK(plan.items.size() == 1);
    clock.advance(std::chrono::seconds(2));
    CHECK(!reactor.issue(plan));
    CHECK(reactor.snapshot(expected)->state == RequestState::FailedPendingRetire);
    CHECK(reactor.take_clear_requests().size() == 1);
  }

  {
    ManualClock clock;
    RuntimeReactor reactor(clock, 1);
    FakeEngine engine({1, 8, 64, false});
    const RequestKey expected{65, 1};
    CHECK(reactor.post(NewRequestEvent{
        RequestSpec{expected.id, 1, 1, clock.now() + std::chrono::seconds(1), 64}, 0, {}, {}}));
    reactor.run_ready();
    auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
    CHECK(prefill.has_value());
    auto result = engine.execute(*prefill);
    clock.advance(std::chrono::seconds(2));
    CHECK(reactor.post_completion(BatchCompletedEvent{std::move(*prefill), std::move(result)}));
    reactor.run_ready();
    CHECK(reactor.snapshot(expected)->state == RequestState::FailedPendingRetire);
    CHECK(reactor.stream_for(expected)->end_reason() == StreamEnd::Failed);
    ack_clears(reactor, engine);
  }
}

void long_pieces_use_the_engine_bound_for_reservation_and_backpressure() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1, 32, 128, 64);
  FakeEngine engine({1, 64, 128, false, 64, 64});
  const RequestKey key{66, 1};
  auto output = std::make_shared<StreamBuffer>(80);
  CHECK(reactor.post(NewRequestEvent{
      RequestSpec{key.id, 1, 3, std::nullopt, 80}, 0, output, {}}));
  reactor.run_ready();
  auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(prefill.has_value());
  finish(reactor, engine, std::move(*prefill));
  auto decode = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(decode.has_value());
  auto result = engine.execute(*decode);
  result.outputs[0] = EngineOutput{std::string(60, 'x'), false};
  CHECK(reactor.post_completion(BatchCompletedEvent{std::move(*decode), std::move(result)}));
  reactor.run_ready();
  CHECK(output->bytes() == 60);
  CHECK(reactor.make_fifo_plan(1, 1, 1).items.empty());
  CHECK(reactor.snapshot(key)->stream_backpressured);
  CHECK(output->consume_with_status(512).data == std::string(60, 'x'));
  CHECK(reactor.post(StreamWritableEvent{key}));
  reactor.run_ready();
  CHECK(reactor.make_fifo_plan(1, 1, 1).items.size() == 1);
  CHECK(reactor.cancel(key));
  ack_clears(reactor, engine);
}

void fatal_backend_status_is_not_hidden_by_cancellation() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 67, 1, 2);
  auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(prefill.has_value());
  CHECK(reactor.post(CancelEvent{key}));
  reactor.run_ready();
  CHECK(reactor.post_completion(BatchCompletedEvent{
      std::move(*prefill),
      EngineResult{false, FailureClass::EngineRestartRequired, {}, "fatal after cancellation"}}));
  reactor.run_ready();
  CHECK(reactor.fatal_error());
  CHECK(reactor.stream_for(key)->end_reason() == StreamEnd::Cancelled);
  ack_clears(reactor, engine);
  CHECK(reactor.live_sequences() == 0);
}

void deadline_prevents_submission_and_shutdown_releases_waiters() {
  ManualClock clock;
  RuntimeReactor reactor(clock, 2);
  FakeEngine engine({2, 8, 64, false});
  const auto deadline = clock.now() + std::chrono::seconds(2);
  CHECK(reactor.post(NewRequestEvent{RequestSpec{5, 8, 3, deadline, 64}, 0, {}, {}}));
  reactor.run_ready();
  const auto key = reactor.latest_key(5);
  CHECK(key.has_value());
  clock.advance(std::chrono::seconds(3));
  CHECK(reactor.expire_deadlines() == 1);
  CHECK(reactor.make_fifo_plan(8, 2, 4).items.empty());
  ack_clears(reactor, engine);
  CHECK(!reactor.snapshot(*key));

  const auto waiting = add_request(reactor, 6, 2, 1);
  reactor.begin_shutdown();
  CHECK(reactor.shutting_down());
  ack_clears(reactor, engine);
  CHECK(reactor.shutdown_complete());
  CHECK(!reactor.snapshot(waiting));
  reactor.seal_events();
}

void cancel_before_submit_never_enters_fifo() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 70, 2, 2);
  const bool cancellation_posted = reactor.post(CancelEvent{key});
  reactor.run_ready();
  CHECK(cancellation_posted);
  CHECK(reactor.make_fifo_plan(8, 1, 4).items.empty());
  ack_clears(reactor, engine);
  CHECK(!reactor.snapshot(key));
  CHECK(engine.memory_stats().live_sequences == 0);
}

void stream_backpressure_suppresses_decode_until_writable_event() {
  SteadyClock clock;
  for (const auto mode : {SchedulerMode::FIFO, SchedulerMode::SAWDS}) {
    RuntimeConfig config;
    config.scheduler = mode;
    config.sawds.minimum_decode_share_basis_points = 10000;
    RuntimeReactor reactor(clock, 1, 256, 32768, 11, config);
    FakeEngine engine({1, 8, 64, false});
    const auto key = add_request(reactor, mode == SchedulerMode::FIFO ? 71 : 171,
                                 1, 3, 11);
    auto plan = reactor.make_configured_plan(1, 1, 1);
    auto prefill = reactor.issue(plan);
    CHECK(prefill.has_value());
    finish(reactor, engine, std::move(*prefill));
    plan = reactor.make_configured_plan(1, 1, 1);
    auto decode = reactor.issue(plan);
    CHECK(decode.has_value());
    finish(reactor, engine, std::move(*decode));
    plan = reactor.make_configured_plan(1, 1, 1);
    CHECK(plan.items.empty());
    CHECK(reactor.snapshot(key)->stream_backpressured);
    auto stream = reactor.stream_for(key);
    CHECK(!stream->consume(11).empty());
    CHECK(reactor.post(StreamWritableEvent{key}));
    reactor.run_ready();
    CHECK(reactor.make_configured_plan(1, 1, 1).items.size() == 1);
    CHECK(reactor.cancel(key));
    ack_clears(reactor, engine);
  }
}

void invalid_sawds_config_fails_at_runtime_construction() {
  SteadyClock clock;
  RuntimeConfig config;
  config.sawds.class_weights[1] = 0;
  bool rejected = false;
  try {
    (void)RuntimeReactor(clock, 1, 256, 32768, 11, config);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);
}

void shutdown_waits_for_paused_engine_ticket_before_clear() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 72, 1, 2);
  auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(prefill.has_value());
  finish(reactor, engine, std::move(*prefill));
  auto decode = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(decode.has_value());
  engine.pause_before_execute();
  std::thread executor([&reactor, &engine, ticket = std::move(*decode)]() mutable {
    auto result = engine.execute(ticket);
    CHECK(reactor.post_completion(BatchCompletedEvent{std::move(ticket), std::move(result)}));
  });
  CHECK(reactor.post(ShutdownEvent{}));
  reactor.run_ready();
  const bool pending = reactor.snapshot(key)->state == RequestState::CancelPendingRetire;
  const bool pinned = reactor.snapshot(key)->inflight_tickets == 1;
  const bool no_clear = reactor.take_clear_requests().empty();
  engine.resume();
  executor.join();
  CHECK(pending);
  CHECK(pinned);
  CHECK(no_clear);
  reactor.run_ready();
  CHECK(reactor.stream_for(key)->bytes() == 0);
  ack_clears(reactor, engine);
  CHECK(reactor.shutdown_complete());
  reactor.seal_events();
  CHECK(!reactor.post(NewRequestEvent{RequestSpec{73, 1, 1, std::nullopt, 64}, 0, {}, {}}));
  CHECK(!reactor.latest_key(73));
}

void cancellation_during_backend_retirement_keeps_sequence_pinned() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 76, 1, 3);
  auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(prefill.has_value());
  finish(reactor, engine, std::move(*prefill));

  engine.pause_after_execute();
  auto decode = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(decode.has_value());
  std::atomic<bool> completion_posted{false};
  std::thread executor([&reactor, &engine, ticket = std::move(*decode), &completion_posted]() mutable {
    auto result = engine.execute(ticket);
    completion_posted = reactor.post_completion(BatchCompletedEvent{std::move(ticket), std::move(result)});
  });
  const bool reached_barrier = engine.wait_until_after_execute_paused(std::chrono::seconds(3));
  const bool engine_inflight = engine.inflight_count() == 1;
  const bool backend_advanced = engine.sequence_position(0) > 1;
  const bool cancellation_posted = reactor.post(CancelEvent{key});
  reactor.run_ready();
  const auto cancelled = reactor.snapshot(key);
  const bool still_pinned = cancelled && cancelled->state == RequestState::CancelPendingRetire &&
                            cancelled->inflight_tickets == 1;
  const bool no_clear_early = reactor.take_clear_requests().empty();
  engine.resume();
  executor.join();
  CHECK(reached_barrier);
  CHECK(engine_inflight);
  CHECK(backend_advanced);
  CHECK(cancellation_posted);
  CHECK(still_pinned);
  CHECK(no_clear_early);
  CHECK(completion_posted.load());
  reactor.run_ready();
  CHECK(reactor.stream_for(key)->bytes() == 0);
  ack_clears(reactor, engine);
  CHECK(reactor.live_sequences() == 0);
}

void completion_post_waits_for_bounded_queue_space_without_dropping_ticket() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 75, 1, 1);
  auto ticket = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(ticket.has_value());
  auto result = engine.execute(*ticket);
  CHECK(reactor.post(StreamWritableEvent{{999, 1}}));
  std::atomic<bool> delivered{false};
  std::thread completion([&reactor, &ticket, &result, &delivered] {
    delivered = reactor.post(BatchCompletedEvent{std::move(*ticket), std::move(result)});
  });
  std::this_thread::yield();
  CHECK(reactor.run_one());
  completion.join();
  CHECK(delivered.load());
  reactor.run_ready();
  CHECK(reactor.snapshot(key)->state == RequestState::DecodeReady);
  ack_clears(reactor, engine);
}

void recoverable_engine_failure_retires_and_clears() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 74, 1, 1);
  engine.fail_next(FailureClass::EngineRecoverable);
  auto ticket = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(ticket.has_value());
  finish(reactor, engine, std::move(*ticket));
  CHECK(reactor.snapshot(key)->state == RequestState::FailedPendingRetire);
  CHECK(!reactor.fatal_error());
  ack_clears(reactor, engine);
  CHECK(!reactor.snapshot(key));
}

void unknown_growth_stays_charged_until_successful_sequence_clear() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 73, 4, 2);
  const auto plan = reactor.make_fifo_plan(4, 1, 4);
  auto ticket = reactor.issue(plan);
  CHECK(ticket.has_value());
  const auto expected_charge = plan.items.front().expected_kv_growth;
  CHECK(reactor.metrics().authorized_kv_tokens == expected_charge);

  EngineResult ambiguous;
  ambiguous.ok = false;
  ambiguous.failure = FailureClass::RequestRecoverable;
  ambiguous.message = "backend may have partially mutated sequence memory";
  ambiguous.growth_resolution = GrowthResolution::Unknown;
  CHECK(reactor.post(BatchCompletedEvent{std::move(*ticket), std::move(ambiguous)}));
  reactor.run_ready();

  CHECK(reactor.snapshot(key)->state == RequestState::FailedPendingRetire);
  CHECK(!reactor.fatal_error());
  CHECK(reactor.metrics().authorized_kv_tokens == expected_charge);
  CHECK(reactor.metrics().charged_kv_tokens >= expected_charge);
  const auto clear_requests = reactor.take_clear_requests();
  CHECK(clear_requests.size() == 1U);
  CHECK(reactor.post(SequenceClearedEvent{
      clear_requests.front().key, engine.clear_sequence(clear_requests.front().sequence)}));
  reactor.run_ready();
  CHECK(reactor.metrics().authorized_kv_tokens == 0U);
  CHECK(reactor.metrics().charged_kv_tokens == 0U);
  CHECK(!reactor.snapshot(key));
}

void sequence_clear_failure_quarantines_resource_and_stops_admission() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 78, 1, 1);
  CHECK(reactor.cancel(key));
  const auto clears = reactor.take_clear_requests();
  CHECK(clears.size() == 1U);
  engine.fail_next_clear();
  const bool cleared = engine.clear_sequence(clears.front().sequence);
  CHECK(!cleared);
  CHECK(reactor.post(SequenceClearedEvent{clears.front().key, cleared}));
  reactor.run_ready();
  CHECK(reactor.fatal_error());
  CHECK(reactor.live_sequences() == 1U);
  CHECK(reactor.metrics().available_sequences == 0U);
  CHECK(reactor.latest_key(78) == key);

  auto admission = std::make_shared<std::atomic<AdmissionResult>>(AdmissionResult::Pending);
  CHECK(reactor.post(NewRequestEvent{RequestSpec{79, 1, 1, std::nullopt, 64},
                                     0, {}, admission}));
  reactor.run_ready();
  CHECK(admission->load(std::memory_order_acquire) == AdmissionResult::Rejected);
  CHECK(reactor.live_sequences() == 1U);
}

void fatal_engine_failure_closes_waiting_requests_before_shutdown() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 2);
  FakeEngine engine({2, 8, 64, false});
  const auto first = add_request(reactor, 80, 1, 1);
  const auto waiting = add_request(reactor, 81, 1, 1);
  engine.fail_next(FailureClass::EngineRestartRequired);
  auto ticket = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
  CHECK(ticket.has_value());
  finish(reactor, engine, std::move(*ticket));
  CHECK(reactor.fatal_error());
  CHECK(reactor.snapshot(waiting)->state == RequestState::PrefillReady);

  reactor.fail_all();
  CHECK(reactor.shutting_down());
  CHECK(reactor.snapshot(first)->state == RequestState::FailedPendingRetire);
  CHECK(reactor.snapshot(waiting)->state == RequestState::FailedPendingRetire);
  CHECK(reactor.stream_for(first)->closed());
  CHECK(reactor.stream_for(waiting)->closed());
  ack_clears(reactor, engine);
  CHECK(reactor.shutdown_complete());
}

void retired_requests_are_removed_from_fifo() {
  SteadyClock clock;
  RuntimeReactor reactor(clock, 1);
  FakeEngine engine({1, 8, 64, false});
  for (std::uint32_t i = 0; i < 10000; ++i) {
    (void)add_request(reactor, 100U + i, 1, 1);
    CHECK(reactor.queued_request_count() == 1);
    auto prefill = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
    CHECK(prefill.has_value());
    finish(reactor, engine, std::move(*prefill));
    auto decode = reactor.issue(reactor.make_fifo_plan(1, 1, 1));
    CHECK(decode.has_value());
    finish(reactor, engine, std::move(*decode));
    ack_clears(reactor, engine);
    CHECK(reactor.queued_request_count() == 0);
  }
  CHECK(reactor.live_sequences() == 0);
}

void concurrent_event_producers_stay_bounded() {
  BoundedQueue<std::uint32_t> queue(8);
  std::atomic<bool> producer_ok{true};
  std::thread producer([&queue, &producer_ok] {
    for (std::uint32_t i = 0; i < 100; ++i) {
      if (!queue.push_wait(i)) {
        producer_ok = false;
        break;
      }
    }
    queue.close();
  });
  std::uint32_t count = 0;
  std::uint32_t expected = 0;
  while (auto value = queue.wait_pop()) {
    CHECK(*value == expected++);
    ++count;
  }
  producer.join();
  CHECK(count == 100);
  CHECK(producer_ok.load());
}

void deterministic_randomized_lifecycle_preserves_ownership_invariants() {
  for (std::uint32_t seed = 1; seed <= 256; ++seed) {
    ManualClock clock;
    RuntimeConfig config;
    config.hard_usable_kv_tokens = 128;
    config.kv_safety_margin_tokens = 1;
    config.admission_generation_reserve_tokens = 2;
    config.scheduler = seed % 2 == 0 ? SchedulerMode::FIFO : SchedulerMode::SAWDS;
    config.max_prefill_tokens_per_epoch = 8;
    config.adaptive_prefill.enabled = seed % 4 == 0;
    config.adaptive_prefill.minimum_tokens = 2;
    config.adaptive_prefill.maximum_tokens = 8;
    config.adaptive_prefill.initial_tokens = 4;
    config.adaptive_prefill.step_tokens = 2;
    config.adaptive_prefill.windows_before_change = 1;
    config.adaptive_prefill.cooldown_windows = 1;
    RuntimeReactor reactor(clock, 8, 16, 128, 11, config);
    FakeEngine engine({8, 16, 64, false});
    std::mt19937 random(seed);
    std::vector<RequestKey> keys;
    for (std::uint64_t id = 1; id <= 8; ++id) {
      const auto prompt = 1U + static_cast<std::uint32_t>(random() % 5);
      const auto output = 1U + static_cast<std::uint32_t>(random() % 4);
      const auto service_class = id % 2 == 0 ? RequestClass::Batch
                                               : RequestClass::Interactive;
      keys.push_back(add_request(reactor, id, prompt, output, 128, 1, service_class));
      const auto metrics = reactor.metrics();
      CHECK(metrics.charged_kv_tokens <= metrics.hard_usable_kv_tokens);
    }

    for (int step = 0; step < 64 && reactor.live_sequences() != 0; ++step) {
      for (const auto key : keys) {
        const auto request = reactor.snapshot(key);
        if (request && !terminal_intent(request->state) && random() % 7 == 0) {
          CHECK(reactor.post(CancelEvent{key}));
        }
      }
      reactor.run_ready();
      const auto plan = reactor.make_configured_plan(8, 4, 2);
      CHECK(valid_work_plan(plan));
      if (config.scheduler == SchedulerMode::SAWDS) {
        const auto repeat = reactor.make_configured_plan(8, 4, 2);
        CHECK(plan.items == repeat.items);
        CHECK(plan.total_tokens == repeat.total_tokens);
      }
      for (const auto& item : plan.items) {
        CHECK(!terminal_intent(reactor.snapshot(item.key)->state));
      }
      if (!plan.items.empty()) {
        auto ticket = reactor.issue(plan);
        CHECK(ticket.has_value());
        const auto issued_metrics = reactor.metrics();
        CHECK(issued_metrics.charged_kv_tokens <= issued_metrics.hard_usable_kv_tokens);
        CHECK(issued_metrics.authorized_kv_tokens == plan.total_tokens);
        auto result = engine.execute(*ticket);
        CHECK(reactor.post(BatchCompletedEvent{std::move(*ticket), std::move(result)}));
        reactor.run_ready();
      }
      ack_clears(reactor, engine);

      std::uint32_t live = 0;
      std::uint64_t committed = 0;
      std::uint64_t soft_reserved = 0;
      for (const auto key : keys) {
        const auto request = reactor.snapshot(key);
        if (!request) continue;
        ++live;
        committed += request->estimated_committed_tokens;
        soft_reserved += request->soft_reserved_tokens;
        CHECK(request->inflight_tickets == 0);
        CHECK(reactor.stream_for(key)->bytes() <= reactor.stream_for(key)->capacity());
        CHECK(request->prefill_cursor <= 5U);
      }
      CHECK(live == reactor.live_sequences());
      CHECK(committed == reactor.estimated_committed_tokens());
      const auto metrics = reactor.metrics();
      CHECK(metrics.committed_kv_tokens == committed);
      CHECK(metrics.soft_reserved_kv_tokens == soft_reserved);
      CHECK(metrics.charged_kv_tokens == committed + soft_reserved);
      CHECK(metrics.charged_kv_tokens <= metrics.hard_usable_kv_tokens);
      CHECK(metrics.authorized_kv_tokens == 0U);
      CHECK(metrics.prefill_epoch_budget >= config.adaptive_prefill.minimum_tokens ||
            !config.adaptive_prefill.enabled);
      CHECK(metrics.prefill_epoch_budget <= config.adaptive_prefill.maximum_tokens ||
            !config.adaptive_prefill.enabled);
      CHECK(engine.inflight_count() == 0);
    }
    CHECK(reactor.live_sequences() == 0);
    CHECK(reactor.estimated_committed_tokens() == 0);
  }
}

void hard_kv_stall_retires_one_victim_and_waits_for_clear_ack() {
  SteadyClock clock;
  RuntimeConfig config;
  config.scheduler = SchedulerMode::FIFO;
  config.hard_usable_kv_tokens = 170;
  config.kv_safety_margin_tokens = 0;
  config.admission_generation_reserve_tokens = 30;
  RuntimeReactor reactor(clock, 2, 128, 200, 11, config);
  FakeEngine engine({2, 128, 200, false, 11});
  auto first_stream = std::make_shared<StreamBuffer>(4096);
  auto second_stream = std::make_shared<StreamBuffer>(4096);
  CHECK(reactor.post(NewRequestEvent{RequestSpec{901, 20, 90, std::nullopt, 4096}, 0,
                                     first_stream, {}}));
  CHECK(reactor.post(NewRequestEvent{RequestSpec{902, 20, 90, std::nullopt, 4096}, 0,
                                     second_stream, {}}));
  reactor.run_ready();
  CHECK(reactor.live_sequences() == 2);

  bool saw_resource_stall = false;
  for (int step = 0; step < 256 && reactor.live_sequences() != 0; ++step) {
    const auto plan = reactor.make_fifo_plan(128, 2, 64, 128);
    if (!plan.items.empty()) {
      auto ticket = reactor.issue(plan);
      CHECK(ticket.has_value());
      finish(reactor, engine, std::move(*ticket));
      ack_clears(reactor, engine);
      continue;
    }
    if (reactor.resolve_resource_stall()) {
      saw_resource_stall = true;
      const auto clears = reactor.take_clear_requests();
      CHECK(clears.size() == 1);
      // The pending clear owns the physical sequence and the resource decision;
      // another loop cannot cascade retirement before its acknowledgement.
      CHECK(!reactor.resolve_resource_stall());
      CHECK(reactor.live_sequences() == 2);
      const auto& clear = clears.front();
      CHECK(reactor.post(SequenceClearedEvent{clear.key, engine.clear_sequence(clear.sequence)}));
      reactor.run_ready();
      continue;
    }
    ack_clears(reactor, engine);
  }
  CHECK(saw_resource_stall);
  CHECK(reactor.live_sequences() == 0);
  CHECK(first_stream->closed());
  CHECK(second_stream->closed());
  CHECK(first_stream->end_reason() == StreamEnd::Failed ||
        second_stream->end_reason() == StreamEnd::Failed);
  CHECK(first_stream->end_reason() == StreamEnd::Complete ||
        second_stream->end_reason() == StreamEnd::Complete);
  CHECK(reactor.estimated_committed_tokens() == 0);
  const auto metrics = reactor.metrics();
  CHECK(metrics.by_class[0].hard_limit_aborted == 1);
  CHECK(metrics.by_class[0].resource_deferred >= 1);
}

void readiness_tracks_admission_capacity_and_recovers_after_clear() {
  SteadyClock clock;
  RuntimeConfig config;
  config.hard_usable_kv_tokens = 64;
  config.soft_pressure_basis_points = 9900;
  config.high_pressure_basis_points = 9950;
  config.kv_safety_margin_tokens = 0;
  config.admission_generation_reserve_tokens = 0;
  RuntimeReactor reactor(clock, 1, 16, 128, 11, config);
  FakeEngine engine({1, 16, 128, false, 11});
  CHECK(reactor.metrics().admission_ready);
  const auto key = add_request(reactor, 902, 63, 1);
  CHECK(!reactor.metrics().admission_ready);
  CHECK(reactor.metrics().available_sequences == 0);
  CHECK(!reactor.fatal_error());
  CHECK(reactor.cancel(key));
  ack_clears(reactor, engine);
  CHECK(reactor.metrics().admission_ready);
  CHECK(reactor.metrics().available_sequences == 1);
}

void adaptive_prefill_uses_current_interactive_pressure_and_static_fallback() {
  ManualClock clock;
  RuntimeConfig config;
  config.scheduler = SchedulerMode::FIFO;
  config.max_prefill_tokens_per_epoch = 256;
  config.adaptive_prefill.enabled = true;
  config.adaptive_prefill.minimum_tokens = 128;
  config.adaptive_prefill.maximum_tokens = 512;
  config.adaptive_prefill.initial_tokens = 256;
  config.adaptive_prefill.step_tokens = 64;
  config.adaptive_prefill.high_interactive_wait_ms = 100;
  config.adaptive_prefill.low_interactive_wait_ms = 50;
  config.adaptive_prefill.high_decode_backlog = 100;
  config.adaptive_prefill.low_decode_backlog = 0;
  config.adaptive_prefill.high_executor_idle_basis_points = 10000;
  config.adaptive_prefill.windows_before_change = 1;
  config.adaptive_prefill.cooldown_windows = 1;
  RuntimeReactor reactor(clock, 4, 128, 128, 11, config);
  FakeEngine engine({4, 16, 128, false, 11});

  RequestSpec pressured{990, 1, 1, std::nullopt, 128};
  pressured.ttft_slo = std::chrono::milliseconds(1000);
  CHECK(reactor.post(NewRequestEvent{std::move(pressured), 0, {}, {}}));
  reactor.run_ready();
  clock.advance(std::chrono::milliseconds(100));
  auto prefill = reactor.make_configured_plan(16, 4, 8);
  CHECK(!prefill.items.empty());
  auto prefill_ticket = reactor.issue(prefill);
  CHECK(prefill_ticket.has_value());
  finish(reactor, engine, std::move(*prefill_ticket));
  clock.advance(std::chrono::milliseconds(100));
  auto decode = reactor.make_configured_plan(16, 4, 8);
  CHECK(!decode.items.empty());
  auto decode_ticket = reactor.issue(decode);
  CHECK(decode_ticket.has_value());
  finish(reactor, engine, std::move(*decode_ticket));
  ack_clears(reactor, engine);
  CHECK(reactor.prefill_epoch_budget() == 192U);

  for (std::uint64_t id = 991; id < 999; ++id) {
    RequestSpec batch{ id, 1, 1, std::nullopt, 128 };
    batch.service_class = RequestClass::Batch;
    CHECK(reactor.post(NewRequestEvent{std::move(batch), 0, {}, {}}));
    reactor.run_ready();
    clock.advance(std::chrono::milliseconds(100));
    auto next_prefill = reactor.make_configured_plan(16, 4, 8);
    auto next_prefill_ticket = reactor.issue(next_prefill);
    CHECK(next_prefill_ticket.has_value());
    finish(reactor, engine, std::move(*next_prefill_ticket));
    clock.advance(std::chrono::milliseconds(100));
    auto next_decode = reactor.make_configured_plan(16, 4, 8);
    auto next_decode_ticket = reactor.issue(next_decode);
    CHECK(next_decode_ticket.has_value());
    finish(reactor, engine, std::move(*next_decode_ticket));
    ack_clears(reactor, engine);
  }
  const auto trace = reactor.adaptive_prefill_trace();
  const auto reductions = std::count_if(trace.begin(), trace.end(), [](const auto& entry) {
    return entry.reason == AdaptivePrefillReason::ReduceForInteractivePressure;
  });
  CHECK(reductions == 1);
  CHECK(reactor.prefill_epoch_budget() >= 192U);
  CHECK(reactor.prefill_epoch_budget() <= config.adaptive_prefill.maximum_tokens);
  const auto metrics = reactor.metrics();
  CHECK(metrics.adaptive_prefill_enabled);
  CHECK(metrics.prefill_epoch_budget == reactor.prefill_epoch_budget());
  CHECK(metrics.adaptive_prefill_reason == AdaptivePrefillReason::StaticFallback);
  CHECK(metrics.by_class[1].completed == 8U);
}

void adaptive_prefill_cooldown_counts_the_full_configured_windows() {
  ManualClock clock;
  RuntimeConfig config;
  config.scheduler = SchedulerMode::FIFO;
  config.max_prefill_tokens_per_epoch = 8;
  config.adaptive_prefill.enabled = true;
  config.adaptive_prefill.minimum_tokens = 1;
  config.adaptive_prefill.maximum_tokens = 16;
  config.adaptive_prefill.initial_tokens = 8;
  config.adaptive_prefill.step_tokens = 1;
  config.adaptive_prefill.high_interactive_wait_ms = 100;
  config.adaptive_prefill.low_interactive_wait_ms = 50;
  config.adaptive_prefill.high_decode_backlog = 1;
  config.adaptive_prefill.low_decode_backlog = 0;
  config.adaptive_prefill.windows_before_change = 1;
  config.adaptive_prefill.cooldown_windows = 1;
  RuntimeReactor reactor(clock, 4, 128, 128, 11, config);
  FakeEngine engine({4, 16, 128, false, 11});
  CHECK(reactor.post(NewRequestEvent{RequestSpec{999, 1, 16, std::nullopt, 128}, 0, {}, {}}));
  reactor.run_ready();

  auto prefill = reactor.make_configured_plan(16, 4, 8);
  auto prefill_ticket = reactor.issue(prefill);
  CHECK(prefill_ticket.has_value());
  finish(reactor, engine, std::move(*prefill_ticket));
  auto first_decode = reactor.make_configured_plan(16, 4, 8);
  auto first_decode_ticket = reactor.issue(first_decode);
  CHECK(first_decode_ticket.has_value());
  finish(reactor, engine, std::move(*first_decode_ticket));
  auto second_decode = reactor.make_configured_plan(16, 4, 8);
  auto second_decode_ticket = reactor.issue(second_decode);
  CHECK(second_decode_ticket.has_value());
  finish(reactor, engine, std::move(*second_decode_ticket));

  const auto trace = reactor.adaptive_prefill_trace();
  CHECK(trace.size() == 3U);
  CHECK(trace[0].budget_before == 8U && trace[0].budget_after == 7U);
  CHECK(trace[0].reason == AdaptivePrefillReason::ReduceForInteractivePressure);
  CHECK(trace[1].budget_before == 7U && trace[1].budget_after == 7U);
  CHECK(trace[1].reason == AdaptivePrefillReason::Cooldown);
  CHECK(trace[2].budget_before == 7U && trace[2].budget_after == 6U);
  CHECK(trace[2].reason == AdaptivePrefillReason::ReduceForInteractivePressure);
}

void adaptive_prefill_returns_to_static_budget_when_signals_are_absent() {
  ManualClock clock;
  RuntimeConfig config;
  config.scheduler = SchedulerMode::SAWDS;
  config.max_prefill_tokens_per_epoch = 8;
  config.adaptive_prefill.enabled = true;
  config.adaptive_prefill.minimum_tokens = 1;
  config.adaptive_prefill.maximum_tokens = 16;
  config.adaptive_prefill.initial_tokens = 4;
  config.adaptive_prefill.step_tokens = 1;
  config.adaptive_prefill.high_executor_idle_basis_points = 0;
  config.adaptive_prefill.windows_before_change = 1;
  config.adaptive_prefill.cooldown_windows = 0;
  RuntimeReactor reactor(clock, 1, 256, 32768, 11, config);
  FakeEngine engine({1, 8, 64, false});
  const auto key = add_request(reactor, 81, 1, 1, 128, 0, RequestClass::Batch);
  auto ticket = reactor.issue(reactor.make_configured_plan(1, 1, 1));
  CHECK(ticket.has_value());
  auto result = engine.execute(*ticket);
  CHECK(reactor.post(BatchCompletedEvent{std::move(*ticket), std::move(result),
                                         std::chrono::nanoseconds::zero()}));
  reactor.run_ready();
  CHECK(reactor.prefill_epoch_budget() == 8);
  CHECK(reactor.metrics().adaptive_prefill_reason == AdaptivePrefillReason::StaticFallback);
  CHECK(reactor.cancel(key));
  ack_clears(reactor, engine);
}

}  // namespace

TEST(RuntimeUnit, lifecycle_transitions) { lifecycle_transitions(); }
TEST(RuntimeUnit, bounded_queue_is_fifo_and_rejects_overflow) { bounded_queue_is_fifo_and_rejects_overflow(); }
TEST(RuntimeUnit, sequence_allocator_waits_for_retirement_and_clear_ack) { sequence_allocator_waits_for_retirement_and_clear_ack(); }
TEST(RuntimeUnit, impossible_context_is_rejected_before_sequence_allocation) { impossible_context_is_rejected_before_sequence_allocation(); }
TEST(RuntimeUnit, ticket_is_move_only_and_retires_once) { ticket_is_move_only_and_retires_once(); }
TEST(RuntimeUnit, fifo_plan_is_stable_and_exact) { fifo_plan_is_stable_and_exact(); }
TEST(RuntimeUnit, stream_buffer_is_bounded_and_partial_reads_keep_order) { stream_buffer_is_bounded_and_partial_reads_keep_order(); }
TEST(RuntimeUnit, stream_close_preserves_final_data_in_atomic_drain) { stream_close_preserves_final_data_in_atomic_drain(); }
TEST(RuntimeUnit, utf8_pieces_and_http_drains_preserve_codepoints) { utf8_pieces_and_http_drains_preserve_codepoints(); }
TEST(RuntimeUnit, error_classifier_is_conservative) { error_classifier_is_conservative(); }
TEST(RuntimeIntegration, fake_engine_runs_fifo_then_clears_before_reuse) { fake_engine_runs_fifo_then_clears_before_reuse(); }
TEST(RuntimeIntegration, chunked_prefill_ranges_are_contiguous_and_commit_after_backend_success) { chunked_prefill_ranges_are_contiguous_and_commit_after_backend_success(); }
TEST(RuntimeIntegration, failed_prefill_chunk_does_not_advance_cursor_or_reschedule) { failed_prefill_chunk_does_not_advance_cursor_or_reschedule(); }
TEST(RuntimeIntegration, cancellation_between_prefill_chunks_stops_later_ranges) { cancellation_between_prefill_chunks_stops_later_ranges(); }
TEST(RuntimeIntegration, cancellation_inflight_prefill_keeps_ticket_pinned_without_cursor_commit) { cancellation_inflight_prefill_keeps_ticket_pinned_without_cursor_commit(); }
TEST(RuntimeIntegration, cancellation_fences_stale_generation_and_discards_output) { cancellation_inflight_fences_stale_generation_and_discards_output(); }
TEST(RuntimeIntegration, duplicate_completion_cannot_retire_a_newer_ticket) { duplicate_completion_cannot_retire_a_newer_ticket(); }
TEST(RuntimeIntegration, mixed_eos_completes_only_its_own_request) { mixed_eos_completes_only_its_own_request(); }
TEST(RuntimeIntegration, deadlines_are_rechecked_at_ticket_issue_and_completion) { deadlines_are_rechecked_at_ticket_issue_and_completion(); }
TEST(RuntimeIntegration, long_pieces_use_engine_bound_for_reservation_and_backpressure) { long_pieces_use_the_engine_bound_for_reservation_and_backpressure(); }
TEST(RuntimeIntegration, fatal_backend_status_is_not_hidden_by_cancellation) { fatal_backend_status_is_not_hidden_by_cancellation(); }
TEST(RuntimeIntegration, deadline_prevents_submission_and_shutdown_releases_waiters) { deadline_prevents_submission_and_shutdown_releases_waiters(); }
TEST(RuntimeIntegration, cancel_before_submit_never_enters_fifo) { cancel_before_submit_never_enters_fifo(); }
TEST(RuntimeIntegration, stream_backpressure_suppresses_decode_until_writable_event) { stream_backpressure_suppresses_decode_until_writable_event(); }
TEST(RuntimeIntegration, invalid_sawds_config_fails_at_runtime_construction) { invalid_sawds_config_fails_at_runtime_construction(); }
TEST(RuntimeIntegration, shutdown_waits_for_paused_engine_ticket_before_clear) { shutdown_waits_for_paused_engine_ticket_before_clear(); }
TEST(RuntimeIntegration, cancellation_during_backend_retirement_keeps_sequence_pinned) { cancellation_during_backend_retirement_keeps_sequence_pinned(); }
TEST(RuntimeIntegration, completion_post_waits_for_bounded_queue_space) { completion_post_waits_for_bounded_queue_space_without_dropping_ticket(); }
TEST(RuntimeIntegration, recoverable_engine_failure_retires_and_clears) { recoverable_engine_failure_retires_and_clears(); }
TEST(RuntimeIntegration, unknown_growth_stays_charged_until_successful_sequence_clear) { unknown_growth_stays_charged_until_successful_sequence_clear(); }
TEST(RuntimeIntegration, sequence_clear_failure_quarantines_resource_and_stops_admission) { sequence_clear_failure_quarantines_resource_and_stops_admission(); }
TEST(RuntimeIntegration, fatal_engine_failure_closes_waiting_requests_before_shutdown) { fatal_engine_failure_closes_waiting_requests_before_shutdown(); }
TEST(RuntimeIntegration, retired_requests_are_removed_from_fifo) { retired_requests_are_removed_from_fifo(); }
TEST(RuntimeConcurrency, concurrent_event_producers_stay_bounded) { concurrent_event_producers_stay_bounded(); }
TEST(RuntimeModel, deterministic_randomized_lifecycle_preserves_ownership_invariants) { deterministic_randomized_lifecycle_preserves_ownership_invariants(); }
TEST(RuntimeIntegration, hard_kv_stall_retires_one_victim_and_waits_for_clear_ack) { hard_kv_stall_retires_one_victim_and_waits_for_clear_ack(); }
TEST(RuntimeIntegration, readiness_tracks_admission_capacity_and_recovers_after_clear) { readiness_tracks_admission_capacity_and_recovers_after_clear(); }
TEST(RuntimeIntegration, adaptive_prefill_uses_current_interactive_pressure_and_static_fallback) { adaptive_prefill_uses_current_interactive_pressure_and_static_fallback(); }
TEST(RuntimeIntegration, adaptive_prefill_cooldown_counts_the_full_configured_windows) { adaptive_prefill_cooldown_counts_the_full_configured_windows(); }
TEST(RuntimeIntegration, adaptive_prefill_returns_to_static_budget_when_signals_are_absent) { adaptive_prefill_returns_to_static_budget_when_signals_are_absent(); }
