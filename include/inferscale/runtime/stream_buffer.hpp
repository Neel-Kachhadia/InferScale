#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace inferscale {

enum class StreamEnd { Complete, Cancelled, Failed };

struct StreamRead {
  std::string data;
  bool closed{};
};

struct StreamUsage {
  std::uint32_t prompt_tokens{};
  std::uint32_t output_tokens{};
};

class StreamBuffer {
 public:
  explicit StreamBuffer(std::size_t capacity_bytes) : capacity_(capacity_bytes) {}

  bool try_push(std::string chunk) {
    std::lock_guard lock(mutex_);
    if (closed_) return false;
    if (chunk.empty()) return true;
    if (chunk.size() > capacity_ - bytes_) return false;
    bytes_ += chunk.size();
    chunks_.push_back(std::move(chunk));
    return true;
  }

  std::string consume(std::size_t max_bytes) {
    return consume_with_status(max_bytes).data;
  }

  StreamRead consume_with_status(std::size_t max_bytes) {
    std::lock_guard lock(mutex_);
    std::string result;
    result.reserve(max_bytes < bytes_ ? max_bytes : bytes_);
    while (!chunks_.empty() && result.size() < max_bytes) {
      auto& front = chunks_.front();
      const auto n = max_bytes - result.size() < front.size() ? max_bytes - result.size() : front.size();
      result.append(front.data(), n);
      bytes_ -= n;
      if (n == front.size()) {
        chunks_.pop_front();
      } else {
        front.erase(0, n);
      }
    }
    return {std::move(result), closed_};
  }

  std::size_t bytes() const {
    std::lock_guard lock(mutex_);
    return bytes_;
  }

  std::size_t capacity() const noexcept { return capacity_; }
  bool full() const { return bytes() == capacity_; }
  void close(StreamEnd reason = StreamEnd::Complete, StreamUsage usage = {}) {
    std::lock_guard lock(mutex_);
    closed_ = true;
    end_ = reason;
    usage_ = usage;
  }
  bool closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
  }
  StreamEnd end_reason() const {
    std::lock_guard lock(mutex_);
    return end_;
  }
  StreamUsage usage() const {
    std::lock_guard lock(mutex_);
    return usage_;
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<std::string> chunks_;
  std::size_t bytes_{};
  bool closed_{false};
  StreamEnd end_{StreamEnd::Complete};
  StreamUsage usage_{};
};

}  // namespace inferscale
