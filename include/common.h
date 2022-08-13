#ifndef __RDMA_EXAMPLE_COMMON__
#define __RDMA_EXAMPLE_COMMON__

#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>

#include <atomic>
#include <thread>

namespace rdma {

class Conn {
  friend class Client;
  friend class Server;

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
  constexpr static uint32_t max_rpc_buffer_size = 1024;

public:
  Conn(rdma_cm_id *id);
  ~Conn();

private:
  rdma_cm_id *id_{nullptr};
  ibv_qp *qp_{nullptr};
  ibv_pd *pd_{nullptr};
  ibv_cq *cq_{nullptr};

  rdma_conn_param param_{};

  char *buffer_{nullptr};
  ibv_mr *local_mr_{nullptr};
  ibv_mr remote_mr_{nullptr};
};

} // namespace rdma

#endif