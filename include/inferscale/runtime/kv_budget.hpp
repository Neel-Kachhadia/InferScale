#pragma once

#include "inferscale/runtime/types.hpp"

#include <cstdint>
#include <unordered_map>

namespace inferscale {

// A lease is owned by one RequestRecord. It deliberately has no releasing
// destructor: logical KV charge and the sequence slot are reclaimed only
// after the matching backend clear acknowledgement reaches the reactor.
class KvLease {
 public:
  KvLease() = default;
  KvLease(const KvLease&) = delete;
  KvLease& operator=(const KvLease&) = delete;
  KvLease(KvLease&& other) noexcept;
  KvLease& operator=(KvLease&&) = delete;

  RequestKey key() const noexcept { return key_; }
  std::uint64_t soft_reserved() const noexcept { return soft_reserved_; }
  std::uint64_t committed() const noexcept { return committed_; }
  std::uint64_t authorized() const noexcept { return authorized_; }
  std::uint64_t peak() const noexcept { return peak_; }
  bool released() const noexcept { return released_; }

 private:
  friend class KvBudgetManager;
  RequestKey key_{};
  const void* owner_{};
  std::uint64_t lease_id_{};
  std::uint64_t soft_reserved_{};
  std::uint64_t committed_{};
  std::uint64_t authorized_{};
  std::uint64_t peak_{};
  bool released_{false};
};

struct KvBudgetConfig {
  std::uint64_t hard_usable_capacity{};
  std::uint16_t soft_pressure_basis_points{7000};
  std::uint16_t high_pressure_basis_points{8500};
  std::uint64_t safety_margin_tokens{16};
};

class KvBudgetManager {
 public:
  explicit KvBudgetManager(KvBudgetConfig config);

  static std::uint64_t conservative_capacity(std::uint64_t resolved_context,
                                             std::uint16_t reserve_basis_points = 1000);
  std::uint64_t reservation_for(std::uint32_t prompt_tokens,
                                std::uint32_t generation_reserve) const;
  bool can_reserve(std::uint64_t tokens) const noexcept;
  bool can_reserve_below_soft_watermark(std::uint64_t tokens) const noexcept;
  std::uint64_t soft_capacity() const noexcept {
    return watermark(config_.hard_usable_capacity, config_.soft_pressure_basis_points);
  }
  bool reserve(KvLease& lease, RequestKey key, std::uint64_t tokens) noexcept;
  bool abandon(KvLease& lease) noexcept;
  bool can_authorize(const KvLease& lease, std::uint64_t growth) const noexcept;
  std::uint64_t max_authorizable_growth(const KvLease& lease,
                                       std::uint64_t upper_bound) const noexcept;
  bool authorize_growth(KvLease& lease, std::uint64_t growth) noexcept;
  bool commit_growth(KvLease& lease, std::uint64_t authorized,
                     std::uint64_t actual) noexcept;
  bool rollback_growth(KvLease& lease, std::uint64_t growth) noexcept;
  bool reclaim_soft_reservation(KvLease& lease, std::uint64_t tokens) noexcept;
  bool reconcile_committed(KvLease& lease, std::uint64_t observed) noexcept;
  bool release(KvLease& lease) noexcept;

  KvPressureState pressure_state() const noexcept;
  std::uint64_t hard_capacity() const noexcept { return config_.hard_usable_capacity; }
  std::uint64_t safety_margin_tokens() const noexcept { return config_.safety_margin_tokens; }
  std::uint64_t committed() const noexcept { return committed_; }
  std::uint64_t soft_reserved() const noexcept { return soft_reserved_; }
  std::uint64_t authorized() const noexcept { return authorized_; }
  std::uint64_t charged() const noexcept;
  std::uint64_t peak_charged() const noexcept { return peak_charged_; }
  std::uint64_t released_tokens() const noexcept { return released_tokens_; }
  std::uint64_t rejected_reservations() const noexcept { return rejected_reservations_; }

 private:
  static bool add_fits(std::uint64_t left, std::uint64_t right,
                       std::uint64_t limit) noexcept;
  static std::uint64_t saturating_add(std::uint64_t left,
                                      std::uint64_t right) noexcept;
  bool owns(const KvLease& lease) const noexcept;
  bool register_lease(KvLease& lease, RequestKey key) noexcept;
  bool unregister_lease(KvLease& lease) noexcept;
  static std::uint64_t watermark(std::uint64_t capacity,
                                 std::uint16_t basis_points) noexcept;
  void update_peak() noexcept;

  KvBudgetConfig config_;
  std::uint64_t committed_{};
  std::uint64_t soft_reserved_{};
  std::uint64_t authorized_{};
  std::uint64_t peak_charged_{};
  std::uint64_t released_tokens_{};
  std::uint64_t rejected_reservations_{};
  std::uint64_t next_lease_id_{1};
  std::unordered_map<RequestKey, std::uint64_t, RequestKeyHash> leases_;
};

}  // namespace inferscale
