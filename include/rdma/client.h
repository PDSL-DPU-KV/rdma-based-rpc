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
  auto call(int id, int n) -> void;

private:
  auto waitEvent(rdma_cm_event_type expected) -> rdma_cm_event *;

private:
  addrinfo *addr_{nullptr};
  rdma_event_channel *ec_{nullptr};
  Conn *conn_{nullptr};

#ifdef USE_NOTIFY
  ::event_base *base_{nullptr};
  std::thread *bg_poller_{nullptr};
#endif
};

class ClientSideCtx final : public ConnCtx {
  friend class Client;

public:
  enum State : int32_t {
    Vacant,
    SendingBufferMeta,
    FilledWithRequest,
    WaitingForResponse,
    FilledWithResponse,
  };

public:
  ClientSideCtx(Conn *conn);
  ~ClientSideCtx();

public:
  auto advance(int32_t finished_op) -> void override;
  auto trigger() -> void;

public:
  auto wait() -> void;

private:
  State state_{Vacant};
  ibv_mr *meta_mr_{nullptr};
  std::mutex mu_{};
  std::condition_variable cv_{};
};

} // namespace rdma

#endif