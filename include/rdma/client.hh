#ifndef __RDMA_EXAMPLE_CLIENT__
#define __RDMA_EXAMPLE_CLIENT__

#include "connection.hh"
#include "context.hh"
#include "ring.hh"

namespace rdma {

class Client {
  class Context final : public ConnCtx {
  public:
    enum State : int32_t {
      Vacant,
      SendingBufferMeta,
      WaitingForResponse,
    };

  public:
    Context(Conn *conn, void *buffer, uint32_t size);
    ~Context();

  public:
    auto advance(const ibv_wc &wc) -> void override;

    auto call(uint32_t rpc_id, const message_t &request) -> void;

    auto wait(message_t &response) -> void;

  private:
    State state_{Vacant};
    // register the local_ in ConnCtx, send to the server side
    ibv_mr *meta_mr_{nullptr};
    // sync
    std::mutex mu_{};
    std::condition_variable cv_{};
  };

public:
  constexpr static uint32_t default_connection_timeout = 3000;
  constexpr static uint32_t max_context_num = 8;

public:
  Client(const char *host, const char *port);
  ~Client();

public:
  auto call(uint32_t rpc_id, const message_t &request, message_t &response)
      -> void;

private:
  auto waitEvent(rdma_cm_event_type expected) -> rdma_cm_event *;

private:
  addrinfo *addr_{nullptr};
  rdma_event_channel *ec_{nullptr};
  Conn *conn_{nullptr};
  Ring<Context *, max_context_num> ctx_ring_{};

#ifdef USE_NOTIFY
  event_base *base_{nullptr};
  std::thread *bg_poller_{nullptr};
#endif
};

} // namespace rdma

#endif