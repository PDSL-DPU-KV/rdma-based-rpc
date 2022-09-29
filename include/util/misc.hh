#ifndef __RDMA_EXAMPLE_MISC__
#define __RDMA_EXAMPLE_MISC__

#include <atomic>

#if defined(__x86_64__)

#include <emmintrin.h>
#define PAUSE _mm_pause()

#elif defined(__aarch64__)

#define PAUSE asm volatile("yield" ::: "memory")

#else

#define PAUSE

#endif

namespace rdma {

inline auto pause() -> void { PAUSE; }

// TTAS Lock
class Spinlock {
public:
  Spinlock() = default;
  ~Spinlock() = default;

public:
  auto lock() -> void {
    for (;;) {
      if (not b_.exchange(true, std::memory_order_acquire)) {
        return;
      }
      while (b_.load(std::memory_order_relaxed)) {
        PAUSE;
      }
    }
  }

  auto tryLock() -> bool {
    return not b_.load(std::memory_order_relaxed) and
           not b_.exchange(true, std::memory_order_acquire);
  }

  auto unlock() -> void { b_.store(false, std::memory_order_release); }

private:
  std::atomic_bool b_{false};
};

} // namespace rdma

#undef PAUSE

#endif