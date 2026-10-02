#pragma once

#include "inferscale/runtime/types.hpp"

#include <atomic>
#include <chrono>

namespace inferscale {

class Clock {
 public:
  virtual ~Clock() = default;
  virtual ClockTime now() const noexcept = 0;
};

class SteadyClock final : public Clock {
 public:
  ClockTime now() const noexcept override { return std::chrono::steady_clock::now(); }
};

class ManualClock final : public Clock {
 public:
  explicit ManualClock(ClockTime start = ClockTime{}) : ticks_(start.time_since_epoch().count()) {}
  ClockTime now() const noexcept override { return ClockTime(ClockTime::duration(ticks_.load())); }
  void advance(ClockTime::duration amount) noexcept { ticks_.fetch_add(amount.count()); }

 private:
  std::atomic<ClockTime::duration::rep> ticks_;
};

}  // namespace inferscale
