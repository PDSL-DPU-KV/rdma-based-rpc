#ifndef __RDMA_EXAMPLE_SERVER__
#define __RDMA_EXAMPLE_SERVER__

#include "common.h"
#include <atomic>

namespace rdma {

class Server {
public:
  constexpr static uint32_t defautl_back_log = 8;

public:
  Server(const char *host, const char *port);
  ~Server();

public:
  auto run() -> void;
  auto stop() -> void;

private:
  auto onEvent() -> void;

private:
  std::atomic_bool running_{false};
  addrinfo *addr_{nullptr};
  rdma_cm_id *cm_id_{nullptr};
  rdma_event_channel *ec_{nullptr};
};

} // namespace rdma

#endif