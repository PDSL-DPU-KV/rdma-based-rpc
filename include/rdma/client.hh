#ifndef __RDMA_EXAMPLE_CLIENT__
#define __RDMA_EXAMPLE_CLIENT__

#include "common.hh"
#include "ring.hh"

namespace rdma {

class ClientSideCtx final : public ConnCtx {
  friend class Client;

public:
  enum State : int32_t {
    Vacant,
    SendingBufferMeta,
    WaitingForResponse,
  };

public:
  ClientSideCtx(Conn *conn, void *buffer, uint32_t size);
  ~ClientSideCtx();

public:
  auto advance(int32_t finished_op) -> void override;

  template <typename RequestType, typename ResponseType>
  auto call(uint32_t rpc_id, const RequestType *request, ResponseType *response)
      -> void {
    assert(state_ == Vacant);

    local_.rpc_id_ = rpc_id;
    request_ = request;
    request_length_ = sizeof(RequestType);
    response_ = response;
    response_length_ = sizeof(ResponseType);

    memcpy(buffer_, request_, request_length_);

    info("local memory region: address: %p, length: %d", buffer_, length_);
    state_ = SendingBufferMeta;

    conn_->postSend(this, meta_mr_->addr, meta_mr_->length, meta_mr_->lkey);
    // NOTICE: must pre-post at here
    conn_->postRecv(this, buffer_, length_, conn_->buffer_mr_->lkey);
  }

  auto wait() -> void;

private:
  State state_{Vacant};
  // register the local_ in ConnCtx, send to the server side
  ibv_mr *meta_mr_{nullptr};
  // user buffers
  const void *request_{nullptr};
  uint32_t request_length_{0};
  void *response_{nullptr};
  uint32_t response_length_{0};
  // sync
  std::mutex mu_{};
  std::condition_variable cv_{};
};

class Client {
public:
  constexpr static uint32_t default_connection_timeout = 3000;
  constexpr static uint32_t max_context_num = 8;

public:
  Client(const char *host, const char *port);
  ~Client();

public:
  template <typename RequestType, typename ResponseType>
  auto call(uint32_t rpc_id, const RequestType *request, ResponseType *response)
      -> void {
    ClientSideCtx *ctx = nullptr;
    while (not ctx_ring_.pop(ctx)) {
      pause();
    }
    ctx->call(rpc_id, request, response);
    ctx->wait();
    assert(ctx_ring_.push(ctx));
  }

private:
  auto waitEvent(rdma_cm_event_type expected) -> rdma_cm_event *;

private:
  addrinfo *addr_{nullptr};
  rdma_event_channel *ec_{nullptr};
  Conn *conn_{nullptr};
  Ring<ClientSideCtx *, max_context_num> ctx_ring_{};

#ifdef USE_NOTIFY
  event_base *base_{nullptr};
  std::thread *bg_poller_{nullptr};
#endif
};

} // namespace rdma

#endif