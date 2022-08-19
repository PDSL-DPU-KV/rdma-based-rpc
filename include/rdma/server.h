#ifndef __RDMA_EXAMPLE_SERVER__
#define __RDMA_EXAMPLE_SERVER__

#include "common.h"

namespace rdma {

class ServerSideCtx final : public ConnCtx {
public:
  enum State : int32_t {
    Vacant,
    WaitingForBufferMeta,
    ReadingRequest,
    FilledWithRequest,
    WritingResponse,
    FilledWithResponse,
  };

public:
  ServerSideCtx(Conn *conn, void *buffer, uint32_t length);
  ~ServerSideCtx();

public:
  auto advance(int32_t finished_op) -> void override;
  auto prepare() -> void;

public:
  // auto clone() -> ServerSideCtx *;

private:
  State state_{Vacant}; // trace the state of ConnCtx
  ibv_mr *meta_mr_{nullptr};
  Meta remote_meta_{};
};

class ConnWithCtx {
  friend class Server;

public:
  constexpr static uint32_t max_context_num = Conn::cq_capacity - 1;

public:
  ConnWithCtx(rdma_cm_id *id);
  ~ConnWithCtx();

private:
  Conn *conn_{nullptr};
  std::array<ServerSideCtx *, max_context_num> ctx_{};
};

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

public:
  auto handleConnEvent() -> void;

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