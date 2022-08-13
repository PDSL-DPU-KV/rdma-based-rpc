#ifndef __RDMA_EXAMPLE_COMMON__
#define __RDMA_EXAMPLE_COMMON__

#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>

#include <atomic>
#include <functional>
#include <thread>

namespace rdma {

class Conn {
  friend class Client;
  friend class Server;

public:
  using Handle = std::function<int(Conn *)>;

  //! NOTICE
  //! Before Handle function, the buffer_ is used to receive one request.
  //! After Handle function, the buffer_ is used to send one response.

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
    };
  }
  constexpr static uint32_t cq_capacity = 8;

public:
  constexpr static uint32_t max_buffer_size = 1024;

public:
  Conn(rdma_cm_id *id, ibv_comp_channel *cc = nullptr);
  ~Conn();

public:
  auto postRecv() -> void;
  auto postSend() -> void;
  auto pollCq(ibv_wc *wc) -> void;

public:
  auto recvBuffer() -> char *; // TODO: do not expose the buffer
  auto sendBuffer() -> char *; // TODO: do not expose the buffer

private:
  rdma_cm_id *id_{nullptr};
  ibv_qp *qp_{nullptr};
  ibv_pd *pd_{nullptr};
  ibv_cq *cq_{nullptr};

  rdma_conn_param param_{};

  char *send_buffer_{nullptr};
  char *recv_buffer_{nullptr};
  ibv_mr *send_mr_{nullptr};
  ibv_mr *recv_mr_{nullptr};
  ibv_mr remote_mr_{nullptr};
};

} // namespace rdma

#endif