#ifndef __RDMA_EXAMPLE_COMMON__
#define __RDMA_EXAMPLE_COMMON__

#include <array>
#include <atomic>
#include <event.h>
#include <functional>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <thread>

namespace rdma {

class ConnCtx;

class Conn {
  friend class Client;
  friend class Server;
  friend class ConnCtx;

public:
  constexpr static uint32_t max_context_num = 8;

public:
  enum Side : int {
    ClientSide,
    ServerSide,
  };

private:
  constexpr static auto defaultQpInitAttr() -> ibv_qp_init_attr {
    return {
        .cap =
            {
                .max_send_wr = max_context_num * 2,
                .max_recv_wr = max_context_num * 2,
                .max_send_sge = 1,
                .max_recv_sge = 1,
            },
        .qp_type = IBV_QPT_RC,
        .sq_sig_all = 0,
    };
  }
  constexpr static uint32_t cq_capacity = max_context_num * 2;

public:
  Conn(Side t, rdma_cm_id *id, bool use_comp_channel = false);
  ~Conn();

public:
  // called at server side
  auto registerCompEvent(::event_base *base, ::event_callback_fn fn) -> int;
  // called at client side
  auto fetchVacantBuffer() -> ConnCtx *;

public:
  auto postRecv(void *ctx, ibv_mr *mr) -> void;
  auto postSend(void *ctx, ibv_mr *mr) -> void;
  auto postRead(void *ctx, ibv_mr *mr, ibv_mr *remote_mr) -> void;
  auto postWriteImm(void *ctx, ibv_mr *mr, ibv_mr *remote_mr) -> void;
  auto pollCq(ibv_wc *wc, int n = 1) -> void;

public:
  auto qpState() -> void;
  auto type() -> Side;

private:
  Side t_{ClientSide};

  rdma_cm_id *id_{nullptr};
  ibv_qp *qp_{nullptr};
  ibv_pd *pd_{nullptr};
  ibv_cq *cq_{nullptr};
  ibv_comp_channel *cc_{nullptr};

  ::event *comp_event_{nullptr};

  rdma_conn_param param_{};

  std::array<ConnCtx *, max_context_num> ctx_{};
};

class ConnCtx {
  friend class Conn;

public:
  enum State : int32_t {
    Vacant,
    WaitingForBufferMeta,
    ReadingRequest,
    FilledWithRequest,
    WritingResponse,
    ReadyForCall,
    SendingBufferMeta,
    WaitingForResponse,
    FilledWithResponse,
  };

public:
  constexpr static uint32_t max_buffer_size = 1024;

public:
  ConnCtx(uint32_t id, Conn *conn);
  ~ConnCtx();

public:
  auto advance(int32_t finished_op) -> void;
  auto trigger() -> void; // called at client side
  auto prepare() -> void; // called at server side

private:
  auto advanceServerSide(int32_t finished_op) -> void;
  auto advanceClientSide(int32_t finished_op) -> void;

public:
  auto fillBuffer(const char *src, uint32_t len) -> void;
  auto buffer() -> const char *;
  auto state() -> State;

private:
  uint32_t id_{0};

  std::atomic<State> state_{Vacant}; // trace the state of ConnCtx

  Conn *conn_{nullptr}; // created in which Conn

  char *buffer_{nullptr};
  ibv_mr *local_buffer_mr_{nullptr};
  ibv_mr *meta_mr_{nullptr};

  ibv_mr remote_buffer_mr_{}; // unused in client side
};

} // namespace rdma

#endif