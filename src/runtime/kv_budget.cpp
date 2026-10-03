#include "inferscale/runtime/kv_budget.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace inferscale {

KvLease::KvLease(KvLease&& other) noexcept
    : key_(other.key_), owner_(other.owner_), lease_id_(other.lease_id_),
      soft_reserved_(other.soft_reserved_), committed_(other.committed_),
      authorized_(other.authorized_), peak_(other.peak_), released_(other.released_) {
  other.key_ = {};
  other.owner_ = nullptr;
  other.lease_id_ = 0;
  other.soft_reserved_ = 0;
  other.committed_ = 0;
  other.authorized_ = 0;
  other.peak_ = 0;
  other.released_ = true;
}

KvBudgetManager::KvBudgetManager(KvBudgetConfig config) : config_(config) {
  if (config_.hard_usable_capacity == 0 ||
      config_.soft_pressure_basis_points == 0 ||
      config_.soft_pressure_basis_points >= config_.high_pressure_basis_points ||
      config_.high_pressure_basis_points >= 10000) {
    throw std::invalid_argument("invalid KV budget configuration");
  }
}

std::uint64_t KvBudgetManager::conservative_capacity(
    std::uint64_t resolved_context, std::uint16_t reserve_basis_points) {
  if (resolved_context == 0 || reserve_basis_points >= 10000) {
    throw std::invalid_argument("invalid resolved KV context or reserve");
  }
  const auto retained_basis_points = static_cast<std::uint16_t>(10000 - reserve_basis_points);
  return watermark(resolved_context, retained_basis_points);
}

std::uint64_t KvBudgetManager::reservation_for(
    std::uint32_t prompt_tokens, std::uint32_t generation_reserve) const {
  const auto prompt = static_cast<std::uint64_t>(prompt_tokens);
  const auto generation = static_cast<std::uint64_t>(generation_reserve);
  if (generation > std::numeric_limits<std::uint64_t>::max() - prompt) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const auto requested = prompt + generation;
  if (config_.safety_margin_tokens >
      std::numeric_limits<std::uint64_t>::max() - requested) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return requested + config_.safety_margin_tokens;
}

bool KvBudgetManager::add_fits(std::uint64_t left, std::uint64_t right,
                               std::uint64_t limit) noexcept {
  return left <= limit && right <= limit - left;
}

std::uint64_t KvBudgetManager::watermark(
    std::uint64_t capacity, std::uint16_t basis_points) noexcept {
  return (capacity / 10000U) * basis_points +
         ((capacity % 10000U) * basis_points) / 10000U;
}

bool KvBudgetManager::can_reserve(std::uint64_t tokens) const noexcept {
  const auto used = charged();
  return used <= config_.hard_usable_capacity &&
         add_fits(used, tokens, config_.hard_usable_capacity);
}

bool KvBudgetManager::can_reserve_below_soft_watermark(
    std::uint64_t tokens) const noexcept {
  const auto used = charged();
  if (used == 0) return can_reserve(tokens);
  return used <= soft_capacity() && add_fits(used, tokens, soft_capacity());
}

bool KvBudgetManager::reserve(KvLease& lease, RequestKey key,
                              std::uint64_t tokens) noexcept {
  if (key.id == 0 || key.generation == 0 || lease.owner_ != nullptr ||
      lease.released_ || leases_.contains(key) || !can_reserve(tokens) ||
      next_lease_id_ == std::numeric_limits<std::uint64_t>::max()) {
    ++rejected_reservations_;
    return false;
  }
  lease.key_ = key;
  lease.owner_ = this;
  lease.lease_id_ = next_lease_id_++;
  lease.soft_reserved_ = tokens;
  lease.peak_ = tokens;
  if (!register_lease(lease, key)) {
    lease.key_ = {};
    lease.owner_ = nullptr;
    lease.lease_id_ = 0;
    lease.soft_reserved_ = 0;
    lease.peak_ = 0;
    ++rejected_reservations_;
    return false;
  }
  soft_reserved_ += tokens;
  update_peak();
  return true;
}

bool KvBudgetManager::abandon(KvLease& lease) noexcept {
  if (!owns(lease) || lease.committed_ != 0 || lease.authorized_ != 0 ||
      lease.soft_reserved_ > soft_reserved_ || !unregister_lease(lease)) return false;
  soft_reserved_ -= lease.soft_reserved_;
  released_tokens_ = saturating_add(released_tokens_, lease.soft_reserved_);
  lease.soft_reserved_ = 0;
  lease.released_ = true;
  return true;
}

bool KvBudgetManager::can_authorize(const KvLease& lease,
                                    std::uint64_t growth) const noexcept {
  if (!owns(lease) ||
      committed_ > config_.hard_usable_capacity ||
      authorized_ > config_.hard_usable_capacity - committed_) return false;
  const auto committed_and_authorized = committed_ + authorized_;
  if (!add_fits(committed_and_authorized, growth,
                config_.hard_usable_capacity)) return false;
  const auto from_lease = std::min(growth, lease.soft_reserved_);
  const auto outgrowth = growth - from_lease;
  const auto used = charged();
  return used <= config_.hard_usable_capacity &&
         add_fits(used, outgrowth, config_.hard_usable_capacity);
}

std::uint64_t KvBudgetManager::max_authorizable_growth(
    const KvLease& lease, std::uint64_t upper_bound) const noexcept {
  if (!owns(lease) || committed_ > config_.hard_usable_capacity ||
      authorized_ > config_.hard_usable_capacity - committed_) return 0;
  const auto charged_now = charged();
  if (charged_now > config_.hard_usable_capacity) return 0;
  const auto hard_headroom = config_.hard_usable_capacity - charged_now;
  const auto lease_capacity = saturating_add(lease.soft_reserved_, hard_headroom);
  const auto growth_capacity = config_.hard_usable_capacity - (committed_ + authorized_);
  return std::min({upper_bound, lease_capacity, growth_capacity});
}

bool KvBudgetManager::authorize_growth(KvLease& lease,
                                       std::uint64_t growth) noexcept {
  if (!can_authorize(lease, growth)) return false;
  const auto from_lease = std::min(growth, lease.soft_reserved_);
  const auto outgrowth = growth - from_lease;
  if (from_lease > soft_reserved_ ||
      outgrowth > std::numeric_limits<std::uint64_t>::max() - lease.authorized_ ||
      !add_fits(authorized_, growth, config_.hard_usable_capacity)) return false;
  lease.soft_reserved_ -= from_lease;
  lease.authorized_ += growth;
  soft_reserved_ -= from_lease;
  authorized_ += growth;
  lease.peak_ = std::max(lease.peak_, saturating_add(
      saturating_add(lease.committed_, lease.soft_reserved_), lease.authorized_));
  update_peak();
  return true;
}

bool KvBudgetManager::commit_growth(KvLease& lease, std::uint64_t authorized,
                                    std::uint64_t actual) noexcept {
  if (!owns(lease) || actual > authorized || authorized > lease.authorized_ ||
      authorized > authorized_ || actual >
          std::numeric_limits<std::uint64_t>::max() - committed_ ||
      actual > std::numeric_limits<std::uint64_t>::max() - lease.committed_ ||
      authorized - actual > std::numeric_limits<std::uint64_t>::max() - soft_reserved_ ||
      authorized - actual >
          std::numeric_limits<std::uint64_t>::max() - lease.soft_reserved_) return false;
  const auto returned = authorized - actual;
  lease.authorized_ -= authorized;
  lease.committed_ += actual;
  lease.soft_reserved_ += returned;
  authorized_ -= authorized;
  committed_ += actual;
  soft_reserved_ += returned;
  lease.peak_ = std::max(lease.peak_, saturating_add(
      saturating_add(lease.committed_, lease.soft_reserved_), lease.authorized_));
  update_peak();
  return true;
}

bool KvBudgetManager::rollback_growth(KvLease& lease,
                                      std::uint64_t growth) noexcept {
  if (!owns(lease) || growth > lease.authorized_ || growth > authorized_ ||
      growth > std::numeric_limits<std::uint64_t>::max() - lease.soft_reserved_ ||
      growth > std::numeric_limits<std::uint64_t>::max() - soft_reserved_) return false;
  lease.authorized_ -= growth;
  lease.soft_reserved_ += growth;
  authorized_ -= growth;
  soft_reserved_ += growth;
  return true;
}

bool KvBudgetManager::reclaim_soft_reservation(KvLease& lease,
                                               std::uint64_t tokens) noexcept {
  if (!owns(lease) || tokens > lease.soft_reserved_ || tokens > soft_reserved_) return false;
  lease.soft_reserved_ -= tokens;
  soft_reserved_ -= tokens;
  return true;
}

bool KvBudgetManager::reconcile_committed(KvLease& lease,
                                          std::uint64_t observed) noexcept {
  if (!owns(lease) || lease.authorized_ != 0) return false;
  const auto allocation = saturating_add(lease.committed_, lease.soft_reserved_);
  if (observed > allocation) return false;
  if (committed_ < lease.committed_ ||
      soft_reserved_ < lease.soft_reserved_) return false;
  const auto committed_without_lease = committed_ - lease.committed_;
  const auto reserved_without_lease = soft_reserved_ - lease.soft_reserved_;
  const auto reconciled_reserved = allocation - observed;
  if (!add_fits(committed_without_lease, observed,
                std::numeric_limits<std::uint64_t>::max()) ||
      !add_fits(reserved_without_lease, reconciled_reserved,
                std::numeric_limits<std::uint64_t>::max())) return false;
  committed_ = committed_without_lease;
  soft_reserved_ = reserved_without_lease;
  lease.committed_ = observed;
  lease.soft_reserved_ = reconciled_reserved;
  committed_ += lease.committed_;
  soft_reserved_ += lease.soft_reserved_;
  lease.peak_ = std::max(lease.peak_, allocation);
  return true;
}

bool KvBudgetManager::release(KvLease& lease) noexcept {
  if (!owns(lease) || lease.committed_ > committed_ ||
      lease.soft_reserved_ > soft_reserved_ || lease.authorized_ > authorized_) return false;
  if (!add_fits(lease.committed_, lease.soft_reserved_,
                std::numeric_limits<std::uint64_t>::max())) return false;
  const auto committed_and_soft = lease.committed_ + lease.soft_reserved_;
  if (!add_fits(committed_and_soft, lease.authorized_,
                std::numeric_limits<std::uint64_t>::max())) return false;
  const auto total = committed_and_soft + lease.authorized_;
  if (!unregister_lease(lease)) return false;
  committed_ -= lease.committed_;
  soft_reserved_ -= lease.soft_reserved_;
  authorized_ -= lease.authorized_;
  released_tokens_ = saturating_add(released_tokens_, total);
  lease.committed_ = 0;
  lease.soft_reserved_ = 0;
  lease.authorized_ = 0;
  lease.released_ = true;
  return true;
}

KvPressureState KvBudgetManager::pressure_state() const noexcept {
  const auto used = charged();
  if (used >= config_.hard_usable_capacity) return KvPressureState::HardLimit;
  if (used >= watermark(config_.hard_usable_capacity,
                        config_.high_pressure_basis_points)) {
    return KvPressureState::HighPressure;
  }
  if (used >= watermark(config_.hard_usable_capacity,
                        config_.soft_pressure_basis_points)) {
    return KvPressureState::SoftPressure;
  }
  return KvPressureState::Normal;
}

void KvBudgetManager::update_peak() noexcept {
  peak_charged_ = std::max(peak_charged_, charged());
}

std::uint64_t KvBudgetManager::charged() const noexcept {
  return saturating_add(saturating_add(committed_, soft_reserved_), authorized_);
}

std::uint64_t KvBudgetManager::saturating_add(std::uint64_t left,
                                             std::uint64_t right) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return left + right;
}

bool KvBudgetManager::owns(const KvLease& lease) const noexcept {
  if (lease.owner_ != this || lease.released_ || lease.lease_id_ == 0) return false;
  const auto it = leases_.find(lease.key_);
  return it != leases_.end() && it->second == lease.lease_id_;
}

bool KvBudgetManager::register_lease(KvLease& lease, RequestKey key) noexcept {
  if (leases_.contains(key)) return false;
  try {
    return leases_.emplace(key, lease.lease_id_).second;
  } catch (...) {
    return false;
  }
}

bool KvBudgetManager::unregister_lease(KvLease& lease) noexcept {
  const auto it = leases_.find(lease.key_);
  if (it == leases_.end() || it->second != lease.lease_id_) return false;
  leases_.erase(it);
  return true;
}

}  // namespace inferscale
