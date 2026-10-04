#include "inferscale/runtime/kv_budget.hpp"
#include "inferscale/scheduler/fifo.hpp"
#include "inferscale/scheduler/sawds.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace inferscale;

KvBudgetConfig budget_config(std::uint64_t hard) {
  return {hard, 7000, 8500, 0};
}

TEST(KvBudgetTest, ReservationGrowthAndReleaseConserveCapacity) {
  KvBudgetManager budget(budget_config(100));
  KvLease lease;
  ASSERT_TRUE(budget.reserve(lease, {1, 1}, 40));
  EXPECT_EQ(budget.charged(), 40U);
  ASSERT_TRUE(budget.authorize_growth(lease, 32));
  EXPECT_EQ(lease.soft_reserved(), 8U);
  EXPECT_EQ(lease.authorized(), 32U);
  ASSERT_TRUE(budget.commit_growth(lease, 32, 30));
  EXPECT_EQ(lease.committed(), 30U);
  EXPECT_EQ(lease.soft_reserved(), 10U);
  EXPECT_EQ(budget.charged(), 40U);
  EXPECT_TRUE(budget.release(lease));
  EXPECT_TRUE(lease.released());
  EXPECT_EQ(budget.charged(), 0U);
  EXPECT_EQ(budget.released_tokens(), 40U);
  EXPECT_FALSE(budget.release(lease));
}

TEST(KvBudgetTest, ReserveCommitAndPressureBoundariesAreExact) {
  KvBudgetManager budget(budget_config(100));
  KvLease lease;
  ASSERT_TRUE(budget.reserve(lease, {10, 1}, 70));
  EXPECT_EQ(budget.pressure_state(), KvPressureState::SoftPressure);
  ASSERT_TRUE(budget.authorize_growth(lease, 70));
  ASSERT_TRUE(budget.commit_growth(lease, 70, 70));
  EXPECT_EQ(budget.committed(), 70U);
  ASSERT_TRUE(budget.authorize_growth(lease, 15));
  EXPECT_EQ(budget.pressure_state(), KvPressureState::HighPressure);
  ASSERT_TRUE(budget.commit_growth(lease, 15, 15));
  ASSERT_TRUE(budget.authorize_growth(lease, 15));
  EXPECT_EQ(budget.charged(), 100U);
  EXPECT_EQ(budget.pressure_state(), KvPressureState::HardLimit);
  EXPECT_FALSE(budget.can_authorize(lease, 1));
  ASSERT_TRUE(budget.commit_growth(lease, 15, 15));
  EXPECT_EQ(budget.committed(), 100U);
  EXPECT_TRUE(budget.release(lease));

  KvLease full;
  KvLease over;
  ASSERT_TRUE(budget.reserve(full, {11, 1}, 100));
  EXPECT_FALSE(budget.reserve(over, {12, 1}, 1));
  EXPECT_EQ(budget.charged(), 100U);
  EXPECT_TRUE(budget.release(full));
}

TEST(KvBudgetTest, OversizedCommitAndReleasedLeaseGrowthDoNotMutateAccounting) {
  KvBudgetManager budget(budget_config(100));
  KvLease lease;
  ASSERT_TRUE(budget.reserve(lease, {13, 1}, 40));
  ASSERT_TRUE(budget.authorize_growth(lease, 20));
  EXPECT_FALSE(budget.commit_growth(lease, 20, 21));
  EXPECT_EQ(lease.committed(), 0U);
  EXPECT_EQ(lease.authorized(), 20U);
  EXPECT_EQ(budget.charged(), 40U);
  ASSERT_TRUE(budget.commit_growth(lease, 20, 20));
  ASSERT_TRUE(budget.release(lease));
  EXPECT_FALSE(budget.authorize_growth(lease, 1));
  EXPECT_FALSE(budget.commit_growth(lease, 0, 0));
  EXPECT_FALSE(budget.release(lease));
  EXPECT_EQ(budget.charged(), 0U);
}

TEST(KvBudgetTest, ReservationArithmeticSaturatesAtIntegerLimit) {
  KvBudgetConfig config{std::numeric_limits<std::uint64_t>::max(), 7000, 8500,
                        std::numeric_limits<std::uint64_t>::max()};
  KvBudgetManager budget(config);
  EXPECT_EQ(budget.reservation_for(std::numeric_limits<std::uint32_t>::max(),
                                   std::numeric_limits<std::uint32_t>::max()),
            std::numeric_limits<std::uint64_t>::max());
  KvLease lease;
  ASSERT_TRUE(budget.reserve(lease, {14, 1}, std::numeric_limits<std::uint64_t>::max()));
  EXPECT_EQ(budget.charged(), std::numeric_limits<std::uint64_t>::max());
  ASSERT_TRUE(budget.authorize_growth(lease, std::numeric_limits<std::uint64_t>::max()));
  ASSERT_TRUE(budget.commit_growth(lease, std::numeric_limits<std::uint64_t>::max(),
                                   std::numeric_limits<std::uint64_t>::max()));
  EXPECT_FALSE(budget.authorize_growth(lease, 1));
  EXPECT_EQ(lease.committed(), std::numeric_limits<std::uint64_t>::max());
  EXPECT_TRUE(budget.release(lease));
  EXPECT_EQ(budget.charged(), 0U);
}

TEST(KvBudgetTest, GrowthCanUseOnlyActuallyUnchargedHardHeadroom) {
  KvBudgetManager budget(budget_config(100));
  KvLease growing;
  KvLease other;
  ASSERT_TRUE(budget.reserve(growing, {2, 1}, 30));
  ASSERT_TRUE(budget.reserve(other, {3, 1}, 50));
  ASSERT_TRUE(budget.can_authorize(growing, 35));
  ASSERT_TRUE(budget.authorize_growth(growing, 35));
  EXPECT_EQ(budget.charged(), 85U);
  EXPECT_EQ(growing.soft_reserved(), 0U);
  EXPECT_EQ(growing.authorized(), 35U);
  EXPECT_EQ(growing.peak(), 35U);

  KvLease full;
  ASSERT_TRUE(budget.reserve(full, {4, 1}, 15));
  EXPECT_FALSE(budget.can_authorize(growing, 1));
  EXPECT_EQ(budget.charged(), 100U);
  ASSERT_TRUE(budget.rollback_growth(growing, 35));
  ASSERT_TRUE(budget.reclaim_soft_reservation(other, 1));
  ASSERT_TRUE(budget.can_authorize(growing, 1));
}

TEST(KvBudgetTest, DuplicateIdentityAndMoveCannotDuplicateAReservation) {
  KvBudgetManager budget(budget_config(100));
  KvLease original;
  KvLease duplicate;
  ASSERT_TRUE(budget.reserve(original, {5, 7}, 25));
  EXPECT_FALSE(budget.reserve(duplicate, {5, 7}, 25));
  KvLease moved(std::move(original));
  EXPECT_FALSE(budget.authorize_growth(original, 1));
  ASSERT_TRUE(budget.authorize_growth(moved, 1));
  EXPECT_FALSE(budget.release(duplicate));
  EXPECT_TRUE(budget.rollback_growth(moved, 1));
  EXPECT_TRUE(budget.release(moved));
  EXPECT_EQ(budget.charged(), 0U);
}

TEST(KvBudgetTest, OldGenerationLeaseCannotMutateAReusedRequestId) {
  KvBudgetManager budget(budget_config(100));
  KvLease old_generation;
  KvLease new_generation;
  ASSERT_TRUE(budget.reserve(old_generation, {15, 1}, 20));
  ASSERT_TRUE(budget.release(old_generation));
  ASSERT_TRUE(budget.reserve(new_generation, {15, 2}, 30));
  EXPECT_FALSE(budget.authorize_growth(old_generation, 1));
  EXPECT_FALSE(budget.release(old_generation));
  EXPECT_EQ(new_generation.key(), (RequestKey{15, 2}));
  EXPECT_EQ(new_generation.soft_reserved(), 30U);
  EXPECT_TRUE(budget.release(new_generation));
  EXPECT_EQ(budget.charged(), 0U);
}

TEST(KvBudgetTest, TelemetrySaturatesWithoutBlockingCapacityRelease) {
  KvBudgetManager budget(budget_config(std::numeric_limits<std::uint64_t>::max()));
  KvLease first;
  KvLease second;
  ASSERT_TRUE(budget.reserve(first, {6, 1}, std::numeric_limits<std::uint64_t>::max()));
  ASSERT_TRUE(budget.release(first));
  ASSERT_TRUE(budget.reserve(second, {7, 1}, 1));
  EXPECT_TRUE(budget.release(second));
  EXPECT_EQ(budget.charged(), 0U);
  EXPECT_EQ(second.soft_reserved(), 0U);
  EXPECT_TRUE(second.released());
  EXPECT_EQ(budget.released_tokens(), std::numeric_limits<std::uint64_t>::max());
}

TEST(KvBudgetTest, ConservativeCapacityLeavesTheDeclaredReserve) {
  EXPECT_EQ(KvBudgetManager::conservative_capacity(1000, 1000), 900U);
  EXPECT_EQ(KvBudgetManager::conservative_capacity(1000, 0), 1000U);
  EXPECT_THROW(KvBudgetManager::conservative_capacity(1000, 10000), std::invalid_argument);
}

TEST(KvBudgetTest, SoftWatermarkRestrictsNewAdmissionWhileExistingGrowthCanUseHardHeadroom) {
  KvBudgetManager budget(budget_config(100));
  KvLease lease;
  ASSERT_TRUE(budget.reserve(lease, {8, 1}, 70));
  EXPECT_EQ(budget.pressure_state(), KvPressureState::SoftPressure);
  EXPECT_FALSE(budget.can_reserve_below_soft_watermark(1));
  EXPECT_TRUE(budget.can_authorize(lease, 1));
}

TEST(SawdsTest, WeightedCreditsAreClassLevelAndDeterministic) {
  const auto now = ClockTime{} + std::chrono::seconds(10);
  std::vector<SchedulerCandidate> ready;
  for (std::uint64_t id = 1; id <= 4; ++id) {
    ready.push_back({{id, 1}, RequestClass::Interactive, WorkKind::Decode,
                     0, 1, now, std::nullopt, id});
  }
  for (std::uint64_t id = 10; id <= 13; ++id) {
    ready.push_back({{id, 1}, RequestClass::Batch, WorkKind::Decode,
                     0, 1, now, std::nullopt, id});
  }
  SawdsConfig config;
  config.class_weights = {3, 1};
  config.credit_cap_tokens = 8;
  config.minimum_decode_share_basis_points = 10000;
  const auto plan = sawds_plan(ready, {}, now, 4, 4, 4, 4, config);
  const auto repeat = sawds_plan(ready, {}, now, 4, 4, 4, 4, config);
  EXPECT_EQ(plan.work.items, repeat.work.items);
  EXPECT_EQ(plan.credits_after, repeat.credits_after);
  EXPECT_TRUE(valid_work_plan(plan.work));
  EXPECT_EQ(plan.work.items.size(), 4U);
  EXPECT_EQ(std::count_if(plan.work.items.begin(), plan.work.items.end(), [&](const WorkItem& item) {
    return item.key.id < 10;
  }), 3);
}

TEST(SawdsTest, BoundedCreditsCarryWeightedServiceAcrossSmallEpochs) {
  const auto now = ClockTime{} + std::chrono::seconds(10);
  const std::vector<SchedulerCandidate> ready{
      {{1, 1}, RequestClass::Interactive, WorkKind::Prefill, 0, 100000,
       now, std::nullopt, 1},
      {{2, 1}, RequestClass::Batch, WorkKind::Prefill, 0, 100000,
       now, std::nullopt, 2},
  };
  SawdsConfig config;
  config.starvation_after = std::chrono::hours(1);
  std::array<std::uint64_t, 2> credits{};
  std::uint64_t interactive_tokens = 0;
  std::uint64_t batch_tokens = 0;
  for (int epoch = 0; epoch < 1000; ++epoch) {
    const auto plan = sawds_plan(ready, credits, now, 128, 2, 128, 128, config);
    ASSERT_FALSE(plan.work.items.empty());
    credits = plan.credits_after;
    for (const auto& item : plan.work.items) {
      if (item.key.id == 1) interactive_tokens += item.token_count;
      if (item.key.id == 2) batch_tokens += item.token_count;
    }
  }
  EXPECT_GT(batch_tokens, 0U);
  EXPECT_EQ(interactive_tokens, 4U * batch_tokens);
}

TEST(SawdsTest, WeightedCreditRoundProgressesWithOneSequenceSlot) {
  const auto now = ClockTime{} + std::chrono::seconds(10);
  const std::vector<SchedulerCandidate> ready{
      {{1, 1}, RequestClass::Interactive, WorkKind::Decode, 0, 1,
       now, std::nullopt, 1},
      {{2, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1,
       now, std::nullopt, 2},
  };
  SawdsConfig config;
  config.starvation_after = std::chrono::hours(1);
  config.minimum_decode_share_basis_points = 10000;
  std::array<std::uint64_t, 2> credits{};
  std::uint64_t interactive_tokens = 0;
  std::uint64_t batch_tokens = 0;
  for (int epoch = 0; epoch < 1280; ++epoch) {
    const auto plan = sawds_plan(ready, credits, now, 1, 1, 1, 1, config);
    ASSERT_EQ(plan.work.items.size(), 1U);
    credits = plan.credits_after;
    if (plan.work.items.front().key.id == 1) ++interactive_tokens;
    if (plan.work.items.front().key.id == 2) ++batch_tokens;
  }
  EXPECT_EQ(interactive_tokens, 1024U);
  EXPECT_EQ(batch_tokens, 256U);
}

TEST(SawdsTest, IdleClassDropsStaleCreditsAndDoesNotStallActiveClass) {
  const auto now = ClockTime{} + std::chrono::seconds(10);
  const std::vector<SchedulerCandidate> ready{{
      {1, 1}, RequestClass::Interactive, WorkKind::Decode, 0, 1,
      now, std::nullopt, 1}};
  const auto plan = sawds_plan(ready, {0, 4096}, now, 1, 1, 1, 1);
  ASSERT_EQ(plan.work.items.size(), 1U);
  EXPECT_EQ(plan.work.items.front().key, (RequestKey{1, 1}));
  EXPECT_EQ(plan.credits_after[1], 0U);
}

TEST(SawdsTest, StarvationBoundCanOverrideExhaustedClassCredit) {
  const auto now = ClockTime{} + std::chrono::seconds(10);
  const std::vector<SchedulerCandidate> ready{
      {{1, 1}, RequestClass::Interactive, WorkKind::Decode, 0, 1,
       now, std::nullopt, 1},
      {{2, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1,
       now - std::chrono::seconds(6), std::nullopt, 2},
  };
  const auto plan = sawds_plan(ready, {4096, 0}, now, 1, 1, 1, 1);
  ASSERT_EQ(plan.work.items.size(), 1U);
  EXPECT_EQ(plan.work.items.front().key, (RequestKey{2, 1}));
}

TEST(SawdsTest, AgingAndDeadlineOrderingAreStableWithinAClass) {
  const auto now = ClockTime{} + std::chrono::seconds(10);
  const auto old = now - std::chrono::seconds(8);
  const auto young = now - std::chrono::milliseconds(100);
  const std::vector<SchedulerCandidate> ready{
      {{1, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1, young, std::nullopt, 1},
      {{2, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1, old, std::nullopt, 2},
  };
  SawdsConfig config;
  config.class_weights = {1, 2};
  config.minimum_decode_share_basis_points = 10000;
  const auto plan = sawds_plan(ready, {}, now, 1, 1, 1, 1, config);
  ASSERT_EQ(plan.work.items.size(), 1U);
  EXPECT_EQ(plan.work.items.front().key, (RequestKey{2, 1}));

  const std::vector<SchedulerCandidate> deadlines{
      {{3, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1, now,
       now + std::chrono::seconds(5), 1},
      {{4, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1, now,
       now + std::chrono::milliseconds(150), 2},
  };
  const auto deadline_plan = sawds_plan(deadlines, {}, now, 1, 1, 1, 1, config);
  ASSERT_EQ(deadline_plan.work.items.size(), 1U);
  EXPECT_EQ(deadline_plan.work.items.front().key, (RequestKey{4, 1}));
}

TEST(SawdsTest, AgeBucketsAreCappedBeforeStableTieBreakers) {
  const auto now = ClockTime{} + std::chrono::seconds(100);
  const std::vector<SchedulerCandidate> ready{
      {{1, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1,
       now - std::chrono::seconds(30), std::nullopt, 2},
      {{2, 1}, RequestClass::Batch, WorkKind::Decode, 0, 1,
       now - std::chrono::seconds(10), std::nullopt, 1},
  };
  SawdsConfig config;
  config.class_weights = {1, 1};
  config.minimum_decode_share_basis_points = 10000;
  config.starvation_after = std::chrono::seconds(1000);
  config.age_bucket = std::chrono::seconds(1);
  config.max_age_bucket = 2;
  const auto plan = sawds_plan(ready, {}, now, 1, 1, 1, 1, config);
  ASSERT_EQ(plan.work.items.size(), 1U);
  // Both ages clamp to bucket 2; arrival order then resolves the tie.
  EXPECT_EQ(plan.work.items.front().key, (RequestKey{2, 1}));
}

TEST(SawdsTest, DecodeProtectionLeavesFinitePrefillProgress) {
  const auto now = ClockTime{} + std::chrono::seconds(1);
  std::vector<SchedulerCandidate> ready;
  ready.push_back({{1, 1}, RequestClass::Interactive, WorkKind::Decode,
                   1, 1, now, std::nullopt, 1});
  ready.push_back({{2, 1}, RequestClass::Batch, WorkKind::Prefill,
                   0, 32, now, std::nullopt, 2});
  SawdsConfig config;
  config.class_weights = {2, 2};
  config.credit_cap_tokens = 16;
  config.minimum_decode_share_basis_points = 5000;
  const auto plan = sawds_plan(ready, {}, now, 4, 2, 2, 2, config);
  ASSERT_EQ(plan.work.items.size(), 2U);
  EXPECT_EQ(plan.work.items[0].kind, WorkKind::Decode);
  EXPECT_EQ(plan.work.items[1].kind, WorkKind::Prefill);
  EXPECT_LE(plan.work.items[1].token_count, 2U);
}

TEST(SawdsTest, StarvedPrefillGetsASequenceEvenUnderDecodeOnlyBacklog) {
  const auto now = ClockTime{} + std::chrono::seconds(10);
  const std::vector<SchedulerCandidate> ready{
      {{1, 1}, RequestClass::Interactive, WorkKind::Decode, 8, 1,
       now - std::chrono::seconds(6), std::nullopt, 1},
      {{2, 1}, RequestClass::Batch, WorkKind::Prefill, 0, 1000,
       now - std::chrono::seconds(6), std::nullopt, 2},
  };
  SawdsConfig config;
  config.minimum_decode_share_basis_points = 10000;
  const auto plan = sawds_plan(ready, {}, now, 1, 1, 1, 1, config);
  ASSERT_EQ(plan.work.items.size(), 1U);
  EXPECT_EQ(plan.work.items.front().key, (RequestKey{2, 1}));
  EXPECT_EQ(plan.work.items.front().kind, WorkKind::Prefill);
}

TEST(SawdsTest, InvalidServiceClassIsRejected) {
  const auto now = ClockTime{};
  const std::vector<SchedulerCandidate> ready{{
      {1, 1}, static_cast<RequestClass>(99), WorkKind::Decode, 0, 1,
      now, std::nullopt, 1}};
  EXPECT_THROW((void)sawds_plan(ready, {}, now, 1, 1, 1, 1), std::invalid_argument);
}

}  // namespace
