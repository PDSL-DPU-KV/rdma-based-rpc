#ifndef __RDMA_EXAMPLE_SERVER__
#define __RDMA_EXAMPLE_SERVER__

#include "common.h"
#include <atomic>
#include <thread>

namespace rdma {

class Server {
public:
  constexpr static uint32_t defautl_back_log = 8;

public:
  Server(const char *host, const char *port);
  ~Server();

public:
  auto registerHandle(Conn::Handle fn) -> void;

public:
  auto run() -> void;
  auto stop() -> void;

private:
  auto connManage() -> void;
  auto wcManage() -> void;

  auto onEvent() -> void;
  auto onConnEstablished(Conn *conn) -> void;

private:
  std::atomic_bool running_{false};
  addrinfo *addr_{nullptr};
  rdma_cm_id *cm_id_{nullptr};
  rdma_event_channel *ec_{nullptr};
  Conn::Handle fn_{nullptr};
  ibv_comp_channel *cc_{nullptr};
  std::thread *bg_t_{nullptr}; // polling cc, fetch wc and trigger callbacks
};

} // namespace rdma

#endif