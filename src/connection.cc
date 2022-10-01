#include "connection.hh"
#include "context.hh"
#include <unistd.h>

namespace rdma {

Conn::Conn(uint16_t id, rdma_cm_id *cm_id, uint32_t n_buffer_page)
    : id_(id), cm_id_(cm_id), n_buffer_page_(n_buffer_page) {
  int ret = 0;

  pd_ = ibv_alloc_pd(cm_id_->verbs);
  checkp(pd_, "fail to allocate pd");

  cq_ = ibv_create_cq(cm_id_->verbs, cq_capacity, this, nullptr, 0);
  checkp(cq_, "fail to allocate cq");

  cm_id_->recv_cq = cq_;
  cm_id_->send_cq = cq_;

  info("allocate protection domain, completion queue and memory region");

  auto init_attr = defaultQpInitAttr();
  init_attr.recv_cq = cq_;
  init_attr.send_cq = cq_;
  ret = rdma_create_qp(cm_id_, pd_, &init_attr);
  check(ret, "fail to create queue pair");
  qp_ = cm_id_->qp;

  info("create queue pair");

  buffer_ = alloc(n_buffer_page_ * buffer_page_size);
  checkp(buffer_, "fail to allocate buffer");
  buffer_mr_ = ibv_reg_mr(pd_, buffer_, n_buffer_page_ * buffer_page_size,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                              IBV_ACCESS_REMOTE_WRITE);
  checkp(buffer_mr_, "fail to allocate buffer memory region");

  info("create connection buffer");

  // self defined connection parameters
  memset(&param_, 0, sizeof(param_));
  param_.responder_resources = 16;
  param_.initiator_depth = 16;
  param_.retry_count = 7;
  param_.rnr_retry_count = 7; // '7' indicates retry infinitely
  param_.private_data = (void *)&buffer_mr_->rkey;
  param_.private_data_len = sizeof(buffer_mr_->rkey);

  info("initialize connection parameters");
}

auto Conn::id() -> uint16_t { return id_; }

auto Conn::poll() -> void {
  static ibv_wc wc[cq_capacity];

  auto ret = ibv_poll_cq(cq_, cq_capacity, wc);
  if (ret < 0) {
    info("poll cq error");
    return;
  } else if (ret == 0) {
    // info("cq is empty");
    return;
  }

  // we use wc.wr_id to match the Connection Context
  for (int i = 0; i < ret; i++) {
    reinterpret_cast<ConnCtx *>(wc[i].wr_id)->advance(wc[i]);
  }

  memset(wc, 0, sizeof(ibv_wc) * ret);
}

auto Conn::postRecv(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
    -> void {
  ibv_sge sge{
      (uint64_t)local_addr, // addr
      length,               // length
      lkey,                 // lkey
  };
  ibv_recv_wr wr{
      (uintptr_t)ctx, // wr_id
      nullptr,        // next
      &sge,           // sg_list
      1,              // num_sge
  };
  ibv_recv_wr *bad = nullptr;
  auto ret = ibv_post_recv(qp_, &wr, &bad);
  check(ret, "fail to post recv");
}

auto Conn::postSend(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                    bool need_inline) -> void {
  ibv_sge sge{
      (uint64_t)local_addr, // addr
      length,               // length
      lkey,                 // lkey
  };
  ibv_send_wr wr{
      (uintptr_t)ctx,    // wr_id
      nullptr,           // next
      &sge,              // sg_list
      1,                 // num_sge
      IBV_WR_SEND,       // opcode
      IBV_SEND_SIGNALED, // send_flags
      {},
      {},
      {},
      {},
  };
  if (need_inline) {
    wr.send_flags |= IBV_SEND_INLINE;
  }
  ibv_send_wr *bad = nullptr;
  auto ret = ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post send");
}

auto Conn::postRead(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                    void *remote_addr, uint32_t rkey) -> void {
  ibv_sge sge{
      (uint64_t)local_addr, // addr
      length,               // length
      lkey,                 // lkey
  };
  ibv_send_wr wr{
      (uintptr_t)ctx,    // wr_id
      nullptr,           // next
      &sge,              // sg_list
      1,                 // num_sge
      IBV_WR_RDMA_READ,  // opcode
      IBV_SEND_SIGNALED, // send_flags
      {},
      {
          {
              (uint64_t)remote_addr, // remote_addr
              rkey,                  // rkey
          },
      }, // wr
      {},
      {},
  };
  ibv_send_wr *bad = nullptr;
  auto ret = ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post read");
}

auto Conn::postWrite(void *ctx, void *local_addr, uint32_t length,
                     uint32_t lkey, void *remote_addr, uint32_t rkey,
                     bool need_inline) -> void {
  ibv_sge sge{
      (uint64_t)local_addr, // addr
      length,               // length
      lkey,                 // lkey
  };
  ibv_send_wr wr{
      (uintptr_t)ctx,    // wr_id
      nullptr,           // next
      &sge,              // sg_list
      1,                 // num_sge
      IBV_WR_RDMA_WRITE, // opcode
      IBV_SEND_SIGNALED, // send_flags
      {},
      {
          {
              (uint64_t)remote_addr, // remote_addr
              rkey,                  // rkey
          },
      }, // wr
      {},
      {},
  };
  if (need_inline) {
    wr.send_flags |= IBV_SEND_INLINE;
  }
  ibv_send_wr *bad = nullptr;
  auto ret = ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post write");
}

auto Conn::postWriteImm(void *ctx, void *local_addr, uint32_t length,
                        uint32_t lkey, void *remote_addr, uint32_t rkey,
                        uint32_t imm) -> void {
  ibv_sge sge{
      (uint64_t)local_addr, // addr
      length,               // length
      lkey,                 // lkey
  };
  ibv_send_wr wr{
      (uintptr_t)ctx,             // wr_id
      nullptr,                    // next
      &sge,                       // sg_list
      1,                          // num_sge
      IBV_WR_RDMA_WRITE_WITH_IMM, // opcode
      IBV_SEND_SIGNALED,          // send_flags
      {imm},                      // imm_data
      {
          {
              (uint64_t)remote_addr, // remote_addr
              rkey,                  // rkey
          },
      }, // wr
      {},
      {},
  };
  ibv_send_wr *bad = nullptr;
  auto ret = ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post write");
}

auto Conn::qpState() -> void {
  ibv_qp_attr attr;
  ibv_qp_init_attr init_attr;
  ibv_query_qp(qp_, &attr,
               IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                   IBV_QP_RNR_RETRY,
               &init_attr);
}

auto Conn::bufferPage(uint32_t id) -> void * {
  assert(id < n_buffer_page_);
  return buffer_pages_[id];
}

auto Conn::remoteKey() -> uint32_t { return remote_buffer_key_; }

auto Conn::localKey() -> uint32_t { return buffer_mr_->lkey; }

Conn::~Conn() {
  int ret = 0;

  ret = ibv_destroy_qp(qp_);
  warn(ret, "fail to destroy qp");
  ret = ibv_destroy_cq(cq_);
  warn(ret, "fail to destroy cq");

  ret = rdma_destroy_id(cm_id_);
  warn(ret, "fail to destroy id");

  ret = ibv_dereg_mr(buffer_mr_);
  warn(ret, "fail to deregister buffer memory region");

  ret = ibv_dealloc_pd(pd_);
  warn(ret, "fail to deallocate pd");

  dealloc(buffer_, n_buffer_page_ * buffer_page_size);

  info("cleanup connection resources");
}

ConnPoller::ConnPoller() : running_(false) {}

ConnPoller::~ConnPoller() { stop(); }

auto ConnPoller::run() -> void {
  running_.store(true, std::memory_order_release);
  poller_ = std::thread(&ConnPoller::poll, this);
  info("connection poller is running");
}

auto ConnPoller::stop() -> void {
  if (running_.load(std::memory_order_acquire)) {
    running_.store(false, std::memory_order_release);
    poller_.join();
    info("connection poller stopped");
  }
}

auto ConnPoller::poll() -> void {
  while (running_.load(std::memory_order_acquire)) {
    if (l_.tryLock()) {
      for (auto conn : conns_) {
        conn->poll();
      }
      l_.unlock();
    } else {
      pause();
    }
  }
}

auto ConnPoller::registerConn(Conn *conn) -> void {
  std::lock_guard<Spinlock> l(l_);
  conns_.emplace_back(conn);
}

auto ConnPoller::deregisterConn(uint16_t conn_id) -> void {
  std::lock_guard<Spinlock> l(l_);
  for (auto it = conns_.begin(); it != conns_.end(); it++) {
    if ((*it)->id() == conn_id) {
      conns_.erase(it);
      break;
    }
  }
}

} // namespace rdma