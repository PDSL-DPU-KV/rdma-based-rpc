#ifndef __RDMA_EXAMPLE_THREAD_POOL__
#define __RDMA_EXAMPLE_THREAD_POOL__

#include "misc.hh"
#include "ring.hh"
#include "util.hh"
#include <functional>
#include <thread>

namespace rdma {

template <uint32_t QueueSize, uint32_t NThread> class ThreadPool {
  using task_t = std::function<void()>;

public:
  ThreadPool() : running_(false) {}
  ~ThreadPool() { stop(); }

public:
  auto run() -> void {
    running_.store(true, std::memory_order_release);
    for (uint32_t i = 0; i < NThread; ++i) {
      workers_[i] = std::thread(&ThreadPool::work, this);
    }
    info("background handlers are running");
  }

  template <class F, class... Args>
  auto enqueue(F &&f, Args &&...args) -> void {
    check(not running_, "enqueue on stopped thread pool");
    tasks_.push(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
  }

  auto work() -> void {
    task_t task;
    while (running_.load(std::memory_order_acquire)) {
      if (tasks_.tryPop(task)) {
        task();
      } else {
        pause();
      }
    }
  }

  auto stop() -> void {
    if (running_.load(std::memory_order_acquire)) {
      running_.store(false, std::memory_order_release);
      for (auto &worker : workers_) {
        worker.join();
      }
      info("background handlers stopped");
    }
  }

private:
  std::array<std::thread, NThread> workers_{};
  MPMCRing<task_t, QueueSize> tasks_{};
  std::atomic_bool running_{false};
};

} // namespace rdma

#endif