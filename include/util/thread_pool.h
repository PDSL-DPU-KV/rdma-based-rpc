#ifndef __RDMA_EXAMPLE_THREAD_POOL__
#define __RDMA_EXAMPLE_THREAD_POOL__

#include "util.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace rdma {

class ThreadPool {
  using task_t = std::function<void()>;

public:
  ThreadPool(uint32_t n_thread) : running_(true) {
    for (uint32_t i = 0; i < n_thread; ++i) {
      workers_.emplace_back(&ThreadPool::work, this);
    }
  }
  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(mu_);
      running_ = false;
    }
    cv_.notify_all();
    for (auto &worker : workers_) {
      worker.join();
    }
  }

public:
  template <class F, class... Args>
  auto enqueue(F &&f, Args &&...args) -> void {
    check(not running_, "enqueue on stopped thread pool");
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    {
      std::unique_lock<std::mutex> lock(mu_);
      tasks_.emplace(task);
    }
    cv_.notify_one();
  }

  auto work() -> void {
    task_t task;
    while (running_) {
      {
        std::unique_lock<std::mutex> l(mu_);
        cv_.wait(l, [this] { return (not running_) or (not tasks_.empty()); });
        if (not running_ and tasks_.empty()) {
          return;
        }
        task = std::move(tasks_.front());
        tasks_.pop();
      }
      task();
    }
  }

  auto stop() -> void {
    running_ = false;
    cv_.notify_all();
  }

private:
  std::vector<std::thread> workers_{};
  std::queue<task_t> tasks_{};
  std::mutex mu_{};
  std::condition_variable cv_{};
  std::atomic_bool running_{false};
};

} // namespace rdma

#endif