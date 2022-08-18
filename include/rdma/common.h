#ifndef __RDMA_EXAMPLE_COMMON__
#define __RDMA_EXAMPLE_COMMON__

#include <array>
#include <atomic>
#include <condition_variable>
#include <event2/event.h>
#include <event2/thread.h>
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
  friend class ClientSideCtx;
  friend class ServerSideCtx;

public:
  // constexpr static uint64_t default_buffer_size = 4 * 1024 * 1024; // 4MB
  constexpr static uint32_t cq_capacity = 16;

private:
  constexpr static auto defaultQpInitAttr() -> ibv_qp_init_attr {
    return {
        .cap =
            {
                .max_send_wr = cq_capacity,
                .max_recv_wr = cq_capacity,
                .max_send_sge = 1,
                .max_recv_sge = 1,
            },
        .qp_type = IBV_QPT_RC,
        .sq_sig_all = 0,
    };
  }

public:
  Conn(rdma_cm_id *id);
  ~Conn();

public:
  auto registerCompEvent(::event_base *base) -> int;

private:
  static auto onWorkComp(int fd, short what, void *arg) -> void;

public:
  auto postRecv(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
      -> void;
  auto postSend(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
      -> void;
  auto postRead(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                void *remote_addr, uint32_t rkey) -> void;
  auto postWriteImm(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                    void *remote_addr, uint32_t rkey) -> void;

public:
  auto qpState() -> void;

private:
  static auto onRecv(int fd, short what, void *arg) -> void;

private:
  rdma_cm_id *id_{nullptr};
  ibv_qp *qp_{nullptr};
  ibv_pd *pd_{nullptr};
  ibv_cq *cq_{nullptr};
  ibv_comp_channel *cc_{nullptr};

  ::event *comp_event_{nullptr};

  rdma_conn_param param_{};
};

class ConnCtx {
public:
  constexpr static uint32_t max_buffer_size = 16;

public:
  ConnCtx(Conn *conn);

public:
  virtual ~ConnCtx();
  virtual auto advance(int32_t finished_op) -> void = 0;

public:
  auto buffer() -> char *;

protected:
  Conn *conn_{nullptr}; // created in which Conn
  char *buffer_{nullptr};
  uint64_t length_{0};
  ibv_mr *buffer_mr_{nullptr};
};

} // namespace rdma

#endif