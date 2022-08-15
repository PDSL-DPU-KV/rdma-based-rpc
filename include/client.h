#ifndef __RDMA_EXAMPLE_CLIENT__
#define __RDMA_EXAMPLE_CLIENT__

#include "common.h"

namespace rdma {

class Client {
public:
  constexpr static uint32_t default_connection_timeout = 3000;

public:
  Client(const char *host, const char *port);
  ~Client();

public:
  auto call() -> void;

private:
  auto waitEvent(rdma_cm_event_type expected) -> rdma_cm_event *;

private:
  addrinfo *addr_{nullptr};
  rdma_event_channel *ec_{nullptr};
  Conn *conn_{nullptr};
};

} // namespace rdma

#endif