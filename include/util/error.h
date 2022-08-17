#ifndef __RDMA_EXAMPLE_ERROR__
#define __RDMA_EXAMPLE_ERROR__

#include <stdexcept>

namespace rdma {

static inline auto info(const char *msg) -> void {
  ::fprintf(stderr, "%s\n", msg);
}

template <typename... Args>
static inline auto info(const char *fmt, Args... args) -> void {
  ::fprintf(stderr, fmt, args...);
  ::putchar('\n');
}

static inline auto die(const char *msg) -> void {
  throw std::runtime_error(msg);
}

constexpr static size_t err_msg_buf_size = 1024;

template <typename... Args>
static inline auto die(const char *fmt, Args... args) -> void {
  char temp_buffer[err_msg_buf_size];
  ::snprintf(temp_buffer, err_msg_buf_size, fmt, args...);
  throw std::runtime_error(temp_buffer);
}

static inline auto check(int rc, const char *msg) -> void {
  if (rc != 0) {
    die("%s: error code: %d", msg, rc);
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

} // namespace rdma

#endif