#ifndef __RDMA_EXAMPLE_CLIENT__
#define __RDMA_EXAMPLE_CLIENT__

#include "connection.hh"
#include "context.hh"
#include "status.hh"
#include "util/ring.hh"

namespace rdma {

class Client {
  class Context final : public ConnCtx {
  public:
    enum State : uint32_t {
      Vacant,
      SendingBufferMeta,
      WaitingForResponse,
    };

  public:
    Context(uint32_t id, Conn *conn, void *buffer, uint32_t length);
    ~Context() = default;

  public:
    auto advance(const ibv_wc &wc) -> void override;
    auto call(uint32_t rpc_id, const message_t &request) -> void;
    auto wait(message_t &response) -> Status;

  public:
    State state_{Vacant};
    Spinlock l_{};
  };

  class ConnWithCtx final : public Conn {
    constexpr static uint32_t max_context_num = 8;

  public:
    ConnWithCtx(uint16_t conn_id, Client *c, rdma_cm_id *id, addrinfo *addr);
    ~ConnWithCtx();

  public:
    auto call(uint32_t rpc_id, const message_t &request, message_t &response)
        -> Status;

  public:
    addrinfo *addr_{nullptr};
    Client *c_{nullptr};
    std::array<Context *, max_context_num> senders_{};
    MPMCRing<Context *, max_context_num> ctx_ring_{};
  };

public:
  constexpr static uint32_t default_connection_timeout = 3000;

public:
  Client();
  ~Client();

public:
  auto call(uint32_t conn_id, uint32_t rpc_id, const message_t &request,
            message_t &response) -> Status;
  auto connect(const char *host, const char *port) -> uint32_t;

private:
  auto waitEvent(rdma_cm_event_type expected) -> rdma_cm_event *;
  auto findCtx(uint32_t ctx_id) -> Context *;

private:
  rdma_event_channel *ec_{nullptr};
  std::vector<ConnWithCtx *> conns_{};
  std::map<uint32_t, Context *> id2ctx_{};
  ConnPoller bg_poller_{};
};

} // namespace rdma

#endif