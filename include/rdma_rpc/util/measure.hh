#ifndef __RDMA_EXAMPLE_MEASURE__
#define __RDMA_EXAMPLE_MEASURE__

#include <chrono>
#include <string>
#include <vector>

namespace rdma {

/// Suggestion:
///     (2 ^ N - 1) * scale >= upper bound of the data
/// Notice:
///     The results are always smaller or equal to the actual statistics.
template <uint32_t N, uint32_t scale> class Statistics {
  static_assert(N >= 1, "N is too small");
  static_assert(scale > 1, "scale is too small");

public:
  Statistics() { reset(); }
  ~Statistics() = default;

public:
  auto reset() -> void { memset(this, 0, sizeof(Statistics)); }

public:
  auto record(uint64_t x) -> void {
    uint32_t n = 0;
    uint32_t y = 0;
    for (uint32_t i = 0; i < N; i++) {
      n = (1 << i);
      y = x / n;
      if (y < scale) {
        s[i][y]++;
        return;
      }
      x -= scale * n;
    }
    s_inf_++;
  }

public:
  // sample number
  auto count() -> uint64_t {
    uint64_t cnt = s_inf_;
    for (uint32_t i = 0; i < N; i++) {
      for (uint32_t j = 0; j < scale; j++) {
        cnt += s[i][j];
      }
    }
    return cnt;
  }

  // approximate sum
  auto sum() -> double {
    double sum = 0;
    uint32_t base = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < N; i++) {
      n = (1 << i);
      for (uint32_t j = 0; j < scale; j++) {
        sum += s[i][j] * (double)(n * j + base);
      }
      base += n * scale;
    }
    sum += (double)s_inf_ * base;
    return sum;
  }

  // approximate avg
  auto avg() -> double { return sum() / std::max(1UL, count()); }

  // approximate max
  auto max() -> uint64_t {
    uint64_t base = ((1 << N) - 1) * scale;
    if (s_inf_ > 0) {
      return base;
    }
    uint32_t n = 0;
    for (int32_t i = N - 1; i >= 0; i--) {
      n = (1 << i);
      base -= n * scale;
      for (int32_t j = scale - 1; j >= 0; j--) {
        if (s[i][j] > 0) {
          return base + n * j;
        }
      }
    }
    return 0;
  }

  // approximate min
  auto min() -> uint64_t {
    uint64_t base = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < N; i++) {
      n = (1 << i);
      for (uint32_t j = 0; j < scale; j++) {
        if (s[i][j] > 0) {
          return base + n * j;
        }
      }
      base += n * scale;
    }
    return base;
  }

  // approximate p-th percentile sample
  auto percentile(double p) -> uint64_t {
    int64_t threshold = (int64_t)(p * (double)count());
    uint64_t base = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < N; i++) {
      n = (1 << i);
      for (uint32_t j = 0; j < scale; j++) {
        threshold -= s[i][j];
        if (threshold < 0)
          return base + j * n;
      }
      base += n * scale;
    }
    return base;
  }

public:
  auto dump() -> std::string {
    char buffer[1024];
    snprintf(buffer, 1024,
             "count: %ld\n"
             "sum:   %f\n"
             "avg:   %f\n"
             "min:   %ld\n"
             "max:   %ld\n"
             "p50:   %ld\n"
             "p90:   %ld\n"
             "p95:   %ld\n"
             "p99:   %ld\n"
             "p999:  %ld\n",
             count(), sum(), avg(), min(), max(), percentile(0.5),
             percentile(0.9), percentile(0.95), percentile(0.99),
             percentile(0.999));
    return buffer;
  }

public:
  auto operator+=(const Statistics<N, scale> &r) -> Statistics<N, scale> & {
    for (uint32_t i = 0; i < N; i++) {
      for (uint32_t j = 0; j < scale; j++) {
        s[i][j] += r.s[i][j];
      }
    }
    s_inf_ += r.s_inf_;
    return *this;
  }

private:
  uint64_t s[N][scale];
  uint64_t s_inf_;
};

template <uint32_t upperbound> using AccStatistics = Statistics<1, upperbound>;

using seconds = std::chrono::seconds;
using milliseconds = std::chrono::milliseconds;
using microseconds = std::chrono::microseconds;
using nanoseconds = std::chrono::nanoseconds;
using clock = std::chrono::steady_clock; // monotonic clock
using time_point = std::chrono::time_point<clock>;
template <typename T> using duration = std::chrono::duration<T>;

class Timer {
public:
  Timer() = default;
  ~Timer() = default;

public:
  auto begin() -> void { begin_ = clock::now(); }
  auto end() -> void { end_ = clock::now(); }
  auto reset() -> void { begin_ = end_ = {}; }

  template <typename T> auto elapsed() -> uint64_t {
    return std::chrono::duration_cast<T>(end_ - begin_).count();
  }

private:
  time_point begin_{};
  time_point end_{};
};

inline auto tsc() -> uint64_t {
  uint64_t a, d;
  asm volatile("rdtsc" : "=a"(a), "=d"(d));
  return (d << 32) | a;
}

inline auto tscp() -> uint64_t {
  uint64_t a, d;
  asm volatile("rdtscp" : "=a"(a), "=d"(d));
  return (d << 32) | a;
}

/// Notice:
///    Before you use this timer, you shall check your cpu have constant_tsc
///    flag by using command: `cat /proc/cpuinfo | grep constant_tsc`.
class ClockTimer {
public:
  ClockTimer() = default;
  ~ClockTimer() = default;

public:
  auto begin() -> void { begin_ = tscp(); }
  auto end() -> void { end_ = tscp(); }
  auto reset() -> void { begin_ = end_ = 0; }
  auto elapsed() -> uint64_t { return end_ - begin_; }

private:
  uint64_t begin_{};
  uint64_t end_{};
};

class TraceTimer {
public:
  TraceTimer() = default;
  ~TraceTimer() = default;

private:
  auto tick() -> void { trace_.emplace_back(clock::now()); }

  auto reset() -> void { trace_.resize(0); }

  auto nPeriod() -> size_t { return trace_.size(); }

  template <typename T> auto period(uint32_t i) -> uint64_t {
    return std::chrono::duration_cast<T>(trace_.at(i + 1) - trace_.at(i))
        .count();
  }

  template <typename T> auto elapsed() -> uint64_t {
    return std::chrono::duration_cast<T>(trace_.back() - trace_.front())
        .count();
  }

private:
  std::vector<time_point> trace_;
};

} // namespace rdma

#endif