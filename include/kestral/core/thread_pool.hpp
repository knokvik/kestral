#pragma once

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "kestral/core/concurrent_queue.hpp"

namespace kestral {

/// A simple fixed-size thread pool that processes tasks from a bounded queue.
/// Workers pull tasks until the queue is closed and drained.
class ThreadPool {
public:
  explicit ThreadPool(std::size_t num_threads,
                      std::size_t queue_capacity = 256)
      : tasks_(queue_capacity) {
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
      workers_.emplace_back([this] { worker_loop(); });
    }
  }

  ~ThreadPool() {
    shutdown();
  }

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  /// Submit a task to be executed by a worker thread.
  /// Blocks if the internal queue is full.
  /// Returns false if the pool has been shut down.
  bool submit(std::function<void()> task) {
    return tasks_.push(std::move(task));
  }

  /// Signal that no more tasks will be submitted and wait for all
  /// currently queued tasks to finish.
  void shutdown() {
    tasks_.close();
    for (auto &worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();
  }

  [[nodiscard]] std::size_t thread_count() const { return workers_.size(); }

private:
  void worker_loop() {
    while (auto task = tasks_.pop()) {
      (*task)();
    }
  }

  ConcurrentQueue<std::function<void()>> tasks_;
  std::vector<std::thread> workers_;
};

} // namespace kestral
