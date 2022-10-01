#ifndef __RDMA_EXAMPLE_COMMON__
#define __RDMA_EXAMPLE_COMMON__

#include "util/misc.hh"
#include "util/util.hh"
#include <cassert>
#include <condition_variable>
#include <event2/event.h>
#include <event2/thread.h>
#include <functional>
#include <infiniband/verbs.h>
#include <list>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <thread>

namespace rdma {

class Conn {
  friend class Client;
  friend class Server;

public:
  constexpr static uint32_t cq_capacity = 64;
  constexpr static uint32_t queue_depth = cq_capacity;
  constexpr static uint32_t buffer_page_size = 1024 * 64; // 64K
  using BufferPage = char[buffer_page_size];

private:
  constexpr static auto defaultQpInitAttr() -> ibv_qp_init_attr {
    return {
        nullptr, // qp_context
        nullptr, // send_cq
        nullptr, // recv_cq
        nullptr, // srq
        {
            queue_depth, // max_send_wr
            queue_depth, // max_recv_wr
            1,           // max_send_sge
            1,           // max_recv_sge
            0,           // max_inline_data
        },               // cap
        IBV_QPT_RC,      // qp_type
        0,               // sq_sig_all
    };
  }

public:
  Conn(uint16_t id, rdma_cm_id *cm_id, uint32_t n_buffer_page);
  ~Conn();

public:
  auto id() -> uint16_t;
  auto poll() -> void;

public:
  auto postRecv(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
      -> void;
  auto postSend(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                bool need_inline = false) -> void;
  auto postRead(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                void *remote_addr, uint32_t rkey) -> void;
  auto postWrite(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                 void *remote_addr, uint32_t rkey, bool need_inline = false)
      -> void;
  auto postWriteImm(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                    void *remote_addr, uint32_t rkey, uint32_t imm) -> void;

public:
  auto qpState() -> void;
  auto bufferPage(uint32_t id) -> void *;

public:
  auto remoteKey() -> uint32_t;
  auto localKey() -> uint32_t;

protected:
  static auto onRecv(int fd, short what, void *arg) -> void;

protected:
  uint16_t id_{0};
  rdma_cm_id *cm_id_{nullptr};
  ibv_qp *qp_{nullptr};
  ibv_pd *pd_{nullptr};
  ibv_cq *cq_{nullptr};
  union {
    void *buffer_{nullptr};
    BufferPage *buffer_pages_;
  };
  uint32_t n_buffer_page_{0};
  ibv_mr *buffer_mr_{nullptr};
  uint32_t remote_buffer_key_{0};
  rdma_conn_param param_{};
};

class ConnPoller {
public:
  ConnPoller();
  ~ConnPoller();

public:
  auto run() -> void;
  auto stop() -> void;
  auto registerConn(Conn *conn) -> void;
  auto deregisterConn(uint16_t conn_id) -> void;

private:
  auto poll() -> void;

private:
  std::atomic_bool running_{false};
  Spinlock l_{};
  std::list<Conn *> conns_{};
  std::thread poller_{};
};

} // namespace rdma

#endif