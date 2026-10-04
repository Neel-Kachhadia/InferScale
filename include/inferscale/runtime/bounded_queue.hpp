#pragma once

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace inferscale {

template <class T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) throw std::invalid_argument("bounded queue capacity must be positive");
  }
  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  bool try_push(T value) {
    std::lock_guard lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  bool push_wait(T value) {
    std::unique_lock lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
    if (closed_) return false;
    queue_.push_back(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  std::optional<T> try_pop() {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return value;
  }

  std::optional<T> wait_pop() {
    std::unique_lock lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return value;
  }

  template <class Rep, class Period>
  std::optional<T> wait_pop_for(const std::chrono::duration<Rep, Period>& timeout) {
    std::unique_lock lock(mutex_);
    not_empty_.wait_for(lock, timeout, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return value;
  }

  void close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
  }

  std::size_t capacity() const noexcept { return capacity_; }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<T> queue_;
  bool closed_{false};
};

}  // namespace inferscale
