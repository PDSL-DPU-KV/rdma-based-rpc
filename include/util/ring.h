#ifndef __RDMA_EXAMPLE_RING__
#define __RDMA_EXAMPLE_RING__

#include <array>
#include <atomic>
#include <cstdint>

namespace rdma {

template <typename T, uint32_t N> class Ring {
public:
  Ring() : head_(0), tail_(0), count_(0) {}
  ~Ring() = default;

public:
  auto push(const T &value) -> void {
    while (not tryPush(value))
      ;
  }

  auto pop() -> T {
    T value;
    while (not tryPop(value))
      ;
    return value;
  }

public:
  auto tryPush(const T &value) -> bool {
    auto h = head_.load(std::memory_order_relaxed);
    if (h == tail_.load(std::memory_order_acquire) and
        count_.load(std::memory_order_relaxed)) {
      return false;
    }
    elements_[h] = value;
    head_.store(nextHead(h), std::memory_order_release);
    return true;
  }

  auto tryPop(T &value) -> bool {
    auto t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire) and
        not count_.load(std::memory_order_relaxed)) {
      return false;
    }
    value = elements_[t];
    tail_.store(nextTail(t), std::memory_order_release);
    return true;
  }

private:
  auto nextHead(uint64_t n) -> int {
    if (n == N - 1) {
      count_++;
    }
    return (n + 1) % N;
  }
  auto nextTail(uint64_t n) -> int {
    if (n == N - 1) {
      count_--;
    }
    return (n + 1) % N;
  }

private:
  std::array<T, N> elements_{};
  std::atomic_uint64_t head_{0};
  std::atomic_uint64_t tail_{0};
  std::atomic_uint64_t count_{0};
};

} // namespace rdma

#endif