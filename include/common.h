#ifndef __RDMA_EXAMPLE_COMMON__
#define __RDMA_EXAMPLE_COMMON__

#include <atomic>
#include <event.h>
#include <functional>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <thread>

namespace rdma {

class Conn {
  friend class Client;
  friend class Server;

public:
  enum Type : int {
    ClientSide,
    ServerSide,
  };

private:
  constexpr static auto defaultQpInitAttr() -> ibv_qp_init_attr {
    return {
        .cap =
            {
                .max_send_wr = 128,
                .max_recv_wr = 128,
                .max_send_sge = 1,
                .max_recv_sge = 1,
            },
        .qp_type = IBV_QPT_RC,
        .sq_sig_all = 0,
    };
  }
  constexpr static uint32_t cq_capacity = 8;

public:
  constexpr static uint32_t max_buffer_size = 1024;

public:
  Conn(Type t, rdma_cm_id *id, bool use_comp_channel = false);
  ~Conn();

public:
  auto registerCompEvent(::event_base *base) -> int;

private:
  static auto onRecv(int fd, short what, void *arg) -> void;

public:
  auto postRecv(ibv_mr *mr) -> int;
  auto postSend(ibv_mr *mr) -> int;
  auto postRead(ibv_mr *mr) -> int;
  auto postWriteImm(ibv_mr *mr) -> int;
  auto pollCq(ibv_wc *wc) -> void;

private:
  Type t_{ClientSide};

  rdma_cm_id *id_{nullptr};
  ibv_qp *qp_{nullptr};
  ibv_pd *pd_{nullptr};
  ibv_cq *cq_{nullptr};
  ibv_comp_channel *cc_{nullptr};

  rdma_conn_param param_{};

  char *buffer_{nullptr};
  ibv_mr *local_buffer_mr_{nullptr};
  ibv_mr *meta_mr_{nullptr};

  ibv_mr remote_buffer_mr_{}; // unused in client side

  ::event *comp_event_{nullptr};
};

} // namespace rdma

#endif