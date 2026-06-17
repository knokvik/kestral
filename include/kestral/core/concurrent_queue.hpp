#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

namespace kestral {

/// A thread-safe bounded queue for producer/consumer handoff.
/// Blocks producers when full and consumers when empty.
/// Supports a "close" operation to signal no more items will arrive.
template <typename T>
class ConcurrentQueue {
public:
  explicit ConcurrentQueue(std::size_t capacity) : capacity_(capacity) {}

  ConcurrentQueue(const ConcurrentQueue &) = delete;
  ConcurrentQueue &operator=(const ConcurrentQueue &) = delete;

  /// Push an item into the queue. Blocks if the queue is full.
  /// Returns false if the queue has been closed.
  bool push(T item) {
    std::unique_lock lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
    if (closed_) {
      return false;
    }
    queue_.push(std::move(item));
    lock.unlock();
    not_empty_.notify_one();
    return true;
  }

  /// Pop an item from the queue. Blocks if the queue is empty.
  /// Returns std::nullopt if the queue is empty and has been closed.
  std::optional<T> pop() {
    std::unique_lock lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) {
      return std::nullopt;
    }
    T item = std::move(queue_.front());
    queue_.pop();
    lock.unlock();
    not_full_.notify_one();
    return item;
  }

  /// Signal that no more items will be pushed.
  /// All waiting consumers will be woken and will eventually drain.
  void close() {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  [[nodiscard]] bool is_closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
  }

private:
  std::size_t capacity_;
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  bool closed_ = false;
};

} // namespace kestral
