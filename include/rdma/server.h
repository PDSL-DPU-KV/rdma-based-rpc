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
#ifdef USE_NOTIFY
  static auto onConnEvent(int fd, short what, void *arg) -> void;
  static auto onExit(int fd, short what, void *arg) -> void;
#endif

public:
  auto handleConnEvent() -> void;

private:
  addrinfo *addr_{nullptr};
  rdma_cm_id *cm_id_{nullptr};
  rdma_event_channel *ec_{nullptr};

#ifdef USE_NOTIFY
  ::event_base *base_{nullptr};
  ::event *conn_event_{nullptr};
  ::event *exit_event_{nullptr};
#endif
};

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
  ServerSideCtx(Conn *conn);
  ~ServerSideCtx();

public:
  auto advance(int32_t finished_op) -> void override;
  auto prepare() -> void;

public:
  // auto clone() -> ServerSideCtx *;

private:
  State state_{Vacant}; // trace the state of ConnCtx
  ibv_mr *meta_mr_{nullptr};
  ibv_mr remote_buffer_mr_{};
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

} // namespace rdma

#endif