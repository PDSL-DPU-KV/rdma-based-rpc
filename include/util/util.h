#ifndef __RDMA_EXAMPLE_ERROR__
#define __RDMA_EXAMPLE_ERROR__

#include <stdexcept>

namespace rdma {

static inline auto info(const char *msg) -> void {
  fprintf(stderr, "%s\n", msg);
}

template <typename... Args>
static inline auto info(const char *fmt, Args... args) -> void {
  fprintf(stderr, fmt, args...);
  fprintf(stderr, "\n");
}

static inline auto die(const char *msg) -> void {
  throw std::runtime_error(msg);
}

constexpr static size_t err_msg_buf_size = 1024;

template <typename... Args>
static inline auto die(const char *fmt, Args... args) -> void {
  char temp_buffer[err_msg_buf_size];
  snprintf(temp_buffer, err_msg_buf_size, fmt, args...);
  throw std::runtime_error(temp_buffer);
}

static inline auto check(int rc, const char *msg) -> void {
  if (rc != 0) {
    die("%s: error code: %d, errno: %d", msg, rc, errno);
  }
}

static inline auto checkp(void *p, const char *msg) -> void {
  if (p == nullptr) {
    die("%s: error code: %d", msg, errno);
  }
}

static inline auto warn(int rc, const char *msg) -> void {
  if (rc != 0) {
    info(msg);
  }
}

static inline auto warnp(void *p, const char *msg) -> void {
  if (p == nullptr) {
    info(msg);
  }
}

#ifdef USE_TIMER
#define TIMER                                                                  \
  timespec start;                                                              \
  timespec end;

#define START_TIMER                                                            \
  do {                                                                         \
    clock_gettime(CLOCK_MONOTONIC, &start);                                    \
  } while (0)

#define END_TIMER                                                              \
  do {                                                                         \
    clock_gettime(CLOCK_MONOTONIC, &end);                                      \
    printf("time usage: %lu\n", (end.tv_sec - start.tv_sec) * 1000000000 +     \
                                    (end.tv_nsec - start.tv_nsec));            \
  } while (0)
#else
#define TIMER
#define START_TIMER
#define END_TIMER
#endif

inline static auto align(uint64_t x, uint64_t base) -> uint64_t {
  return (((x) + (base)-1) & ~(base - 1));
}

#define HUGE_PAGE_SIZE 2 * 1024 * 1024

auto alloc(uint32_t len) -> void *;
auto dealloc(void *p, uint32_t len) -> void;

} // namespace rdma

#endif