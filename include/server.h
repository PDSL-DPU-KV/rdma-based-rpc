#ifndef __RDMA_EXAMPLE_SERVER__
#define __RDMA_EXAMPLE_SERVER__

#include "common.h"

namespace rdma {

class Server {
public:
  constexpr static uint32_t default_back_log = 8;

public:
  Server(const char *host, const char *port);
  ~Server();

public:
  auto run() -> int;

private:
  static auto onConnEvent(int fd, short what, void *arg) -> void;
  static auto onExit(int fd, short what, void *arg) -> void;

private:
  addrinfo *addr_{nullptr};
  rdma_cm_id *cm_id_{nullptr};
  rdma_event_channel *ec_{nullptr};

  ::event_base *base_{nullptr};
  ::event *conn_event_{nullptr};
  ::event *exit_event_{nullptr};
};

} // namespace rdma

#endif