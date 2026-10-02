#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace inferscale {

class Utf8StreamDecoder {
 public:
  std::string feed(std::string_view bytes) {
    pending_.append(bytes);
    std::string output;
    std::size_t offset = 0;
    while (offset < pending_.size()) {
      const auto count = sequence_length(static_cast<unsigned char>(pending_[offset]));
      if (count == 0) {
        output.append("\xef\xbf\xbd");
        ++offset;
        continue;
      }
      if (pending_.size() - offset < count) break;
      if (!valid_sequence(pending_, offset, count)) {
        output.append("\xef\xbf\xbd");
        ++offset;
        continue;
      }
      output.append(pending_, offset, count);
      offset += count;
    }
    pending_.erase(0, offset);
    return output;
  }

  std::string finish() {
    if (pending_.empty()) return {};
    pending_.clear();
    return "\xef\xbf\xbd";
  }

  static bool is_valid(std::string_view value, std::size_t offset,
                       std::size_t count) noexcept {
    return valid_sequence(value, offset, count);
  }

 private:
  static std::size_t sequence_length(unsigned char lead) noexcept {
    if (lead <= 0x7fU) return 1;
    if (lead >= 0xc2U && lead <= 0xdfU) return 2;
    if (lead >= 0xe0U && lead <= 0xefU) return 3;
    if (lead >= 0xf0U && lead <= 0xf4U) return 4;
    return 0;
  }

  static bool valid_sequence(std::string_view value, std::size_t offset,
                             std::size_t count) noexcept {
    if (offset > value.size() || count > value.size() - offset) return false;
    const auto first = static_cast<unsigned char>(value[offset]);
    for (std::size_t i = 1; i < count; ++i) {
      const auto byte = static_cast<unsigned char>(value[offset + i]);
      if ((byte & 0xc0U) != 0x80U) return false;
      if (i == 1 && ((first == 0xe0U && byte < 0xa0U) ||
                     (first == 0xedU && byte > 0x9fU) ||
                     (first == 0xf0U && byte < 0x90U) ||
                     (first == 0xf4U && byte > 0x8fU))) return false;
    }
    return true;
  }

  std::string pending_;
};

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
    auto remaining = max_bytes < bytes_ ? max_bytes : bytes_;
    for (const auto& chunk : chunks_) {
      const auto n = std::min(remaining, chunk.size());
      result.append(chunk.data(), n);
      remaining -= n;
      if (remaining == 0) break;
    }
    std::size_t safe_bytes = 0;
    while (safe_bytes < result.size()) {
      const auto lead = static_cast<unsigned char>(result[safe_bytes]);
      std::size_t count = 0;
      if (lead <= 0x7fU) count = 1;
      else if (lead >= 0xc2U && lead <= 0xdfU) count = 2;
      else if (lead >= 0xe0U && lead <= 0xefU) count = 3;
      else if (lead >= 0xf0U && lead <= 0xf4U) count = 4;
      else break;
      if (result.size() - safe_bytes < count ||
          !Utf8StreamDecoder::is_valid(result, safe_bytes, count)) break;
      safe_bytes += count;
    }
    result.resize(safe_bytes);
    auto consumed = result.size();
    while (!chunks_.empty() && consumed != 0) {
      auto& front = chunks_.front();
      const auto n = std::min(consumed, front.size());
      bytes_ -= n;
      consumed -= n;
      if (n == front.size()) chunks_.pop_front();
      else front.erase(0, n);
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
