#include "common.h"

namespace rdma {

Conn::Conn(rdma_cm_id *id) : id_(id) {
  int ret = 0;

  pd_ = ::ibv_alloc_pd(id_->verbs);
  checkp(pd_, "fail to allocate pd");

#ifdef USE_NOTIFY
  cc_ = ::ibv_create_comp_channel(id_->verbs);
  checkp(cc_, "fail to create completion channel");
  id_->recv_cq_channel = cc_;
  id_->send_cq_channel = cc_;
  id_->pd = pd_;
#endif

#ifdef USE_NOTIFY
  cq_ = ::ibv_create_cq(id_->verbs, cq_capacity, this, cc_, 0);
#endif

#ifdef USE_POLL
  cq_ = ::ibv_create_cq(id_->verbs, cq_capacity, this, nullptr, 0);
#endif

  checkp(cq_, "fail to allocate cq");

#ifdef USE_NOTIFY
  ret = ::ibv_req_notify_cq(cq_, 0);
  check(ret, "fail to request completion notification on a cq");
#endif

  id_->recv_cq = cq_;
  id_->send_cq = cq_;

  info("allocate protection domain, completion queue and memory region");

  ibv_qp_init_attr init_attr = defaultQpInitAttr();
  init_attr.recv_cq = cq_;
  init_attr.send_cq = cq_;
  ret = ::rdma_create_qp(id_, pd_, &init_attr);
  check(ret, "fail to create queue pair");
  qp_ = id_->qp;

  info("create queue pair");

  // self defined connection parameters
  ::memset(&param_, 0, sizeof(param_));
  param_.responder_resources = 16;
  param_.initiator_depth = 16;
  param_.retry_count = 7;
  param_.rnr_retry_count = 7; // '7' indicates retry infinitely

  info("initialize connection parameters");

#ifdef USE_POLL
  bg_poller_ = new std::thread([this]() -> void {
    running_ = true;
    info("background poller start running");
    while (running_) {
      poll();
    }
  });
#endif
}

auto Conn::poll() -> void {
  static ibv_wc wc[cq_capacity];
  auto ret = ::ibv_poll_cq(cq_, cq_capacity, wc);
  if (ret < 0) {
    info("poll cq error");
    return;
  } else if (ret == 0) {
    // info("cq is empty");
    return;
  }

  // we use wc.wr_id to match the Connection Context
  for (int i = 0; i < ret; i++) {
    reinterpret_cast<ConnCtx *>(wc[i].wr_id)->advance(wc[i].opcode);
  }
}

#ifdef USE_NOTIFY
auto Conn::registerCompEvent(::event_base *base) -> int {
  assert(base != nullptr);
  comp_event_ =
      ::event_new(base, cc_->fd, EV_READ | EV_PERSIST, &Conn::onWorkComp, this);
  assert(comp_event_ != nullptr);
  return ::event_add(comp_event_, nullptr);
}

auto Conn::onWorkComp([[gnu::unused]] int fd, [[gnu::unused]] short what,
                      void *arg) -> void {
  Conn *conn = reinterpret_cast<Conn *>(arg);

  ibv_cq *cq = nullptr;
  [[gnu::unused]] void *unused_ctx = nullptr;
  auto ret = ::ibv_get_cq_event(conn->cc_, &cq, &unused_ctx);
  ::ibv_ack_cq_events(cq, 1);
  if (ret != 0) {
    info("meet an error cq event");
    return;
  }
  assert(conn->cq_ == cq); // must be the same cq
  ret = ::ibv_req_notify_cq(cq, 0);
  if (ret != 0) {
    info("fail to request completion notification on a cq");
    return;
  }

  conn->poll();
}
#endif

auto Conn::postRecv(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
    -> void {
  ibv_sge sge{
      .addr = (uint64_t)local_addr,
      .length = length,
      .lkey = lkey,
  };
  ibv_recv_wr wr{
      .wr_id = (uintptr_t)ctx,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
  };
  ibv_recv_wr *bad = nullptr;
  auto ret = ::ibv_post_recv(qp_, &wr, &bad);
  check(ret, "fail to post recv");
}

auto Conn::postSend(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
    -> void {
  ibv_sge sge{
      .addr = (uint64_t)local_addr,
      .length = length,
      .lkey = lkey,
  };
  ibv_send_wr wr{
      .wr_id = (uintptr_t)ctx,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_SEND,
      .send_flags = IBV_SEND_SIGNALED,
  };
  ibv_send_wr *bad = nullptr;
  auto ret = ::ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post send");
}

auto Conn::postRead(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                    void *remote_addr, uint32_t rkey) -> void {
  ibv_sge sge{
      .addr = (uint64_t)local_addr,
      .length = length,
      .lkey = lkey,
  };
  ibv_send_wr wr{
      .wr_id = (uintptr_t)ctx,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_RDMA_READ,
      .send_flags = IBV_SEND_SIGNALED,
      .wr{
          .rdma{
              .remote_addr = (uint64_t)remote_addr,
              .rkey = rkey,
          },
      },
  };
  ibv_send_wr *bad = nullptr;
  auto ret = ::ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post read");
}

auto Conn::postWriteImm(void *ctx, void *local_addr, uint32_t length,
                        uint32_t lkey, void *remote_addr, uint32_t rkey)
    -> void {
  ibv_sge sge{
      .addr = (uint64_t)local_addr,
      .length = length,
      .lkey = lkey,
  };
  ibv_send_wr wr{
      .wr_id = (uintptr_t)ctx,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
      .send_flags = IBV_SEND_SIGNALED,
      .imm_data = 0x1234, // TODO: use imm_data to pass some meta data
      .wr{
          .rdma{
              .remote_addr = (uint64_t)remote_addr,
              .rkey = rkey,
          },
      },
  };
  ibv_send_wr *bad = nullptr;
  auto ret = ::ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post write");
}

auto Conn::qpState() -> void {
  ibv_qp_attr attr;
  ibv_qp_init_attr init_attr;
  ibv_query_qp(qp_, &attr,
               IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                   IBV_QP_RNR_RETRY,
               &init_attr);
  return;
}

Conn::~Conn() {
  int ret = 0;

#ifdef USE_NOTIFY
  if (comp_event_ != nullptr) {
    ret = ::event_del(comp_event_);
    warn(ret, "fail to deregister completion event");
    ::event_free(comp_event_);
  }
#endif

#ifdef USE_POLL
  running_ = false;
  bg_poller_->join();
  delete bg_poller_;
#endif

  ret = ::ibv_destroy_qp(qp_);
  warn(ret, "fail to destroy qp");
  ret = ::ibv_destroy_cq(cq_);
  warn(ret, "fail to destroy cq");

#ifdef USE_NOTIFY
  ret = ::ibv_destroy_comp_channel(cc_);
  warn(ret, "fail to destroy cc");
#endif

  ret = ::rdma_destroy_id(id_);
  warn(ret, "fail to destroy id");

  ret = ::ibv_dealloc_pd(pd_);
  warn(ret, "fail to deallocate pd");

  info("cleanup connection resources");
}

ConnCtx::ConnCtx(Conn *conn) : conn_(conn) {
  buffer_ = new char[max_buffer_size];
  buffer_mr_ = ::ibv_reg_mr(conn->pd_, buffer_, max_buffer_size,
                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_REMOTE_READ);
  checkp(buffer_mr_, "fail to register rpc buffer");
}

auto ConnCtx::buffer() -> char * { return buffer_; }

ConnCtx::~ConnCtx() {
  check(::ibv_dereg_mr(buffer_mr_), "fail to deregister buffer mr");
  delete[] buffer_;
}

} // namespace rdma