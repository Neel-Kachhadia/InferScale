#pragma once

#include "inferscale/runtime/types.hpp"
#include "inferscale/runtime/sequence_allocator.hpp"

#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

namespace inferscale {

struct TicketEntry {
  WorkItem work;
  SequenceId sequence{};
  std::shared_ptr<const std::vector<TokenId>> prompt_token_ids{};
  std::uint32_t max_output_tokens{};
  auto operator<=>(const TicketEntry&) const = default;
};

class BatchTicket {
 public:
  BatchTicket(std::uint64_t id, std::vector<TicketEntry> entries)
      : id_(id), entries_(std::move(entries)) {}
  BatchTicket(const BatchTicket&) = delete;
  BatchTicket& operator=(const BatchTicket&) = delete;
  BatchTicket(BatchTicket&& other) noexcept
      : id_(other.id_), entries_(std::move(other.entries_)), armed_(other.armed_), retired_(other.retired_) {
    other.armed_ = false;
  }
  BatchTicket& operator=(BatchTicket&& other) noexcept {
    if (armed_ && !retired_) std::terminate();
    if (this != &other) {
      id_ = other.id_;
      entries_ = std::move(other.entries_);
      armed_ = other.armed_;
      retired_ = other.retired_;
      other.armed_ = false;
    }
    return *this;
  }
  ~BatchTicket() {
    if (armed_ && !retired_) std::terminate();
  }

  std::uint64_t id() const noexcept { return id_; }
  const std::vector<TicketEntry>& entries() const noexcept { return entries_; }
  bool retire() noexcept {
    if (!armed_ || retired_) return false;
    retired_ = true;
    return true;
  }

 private:
  std::uint64_t id_{};
  std::vector<TicketEntry> entries_;
  bool armed_{true};
  bool retired_{false};
};

}  // namespace inferscale
