#include "common.h"
#include "error.h"

#include <cassert>

namespace rdma {

Conn::Conn(Type t, rdma_cm_id *id, bool use_comp_channel) : t_(t), id_(id) {
  int ret = 0;

  pd_ = ::ibv_alloc_pd(id_->verbs);
  checkp(pd_, "fail to allocate pd");
  if (use_comp_channel) {
    cc_ = ::ibv_create_comp_channel(id_->verbs);
    checkp(cc_, "fail to create completion channel");
    id_->recv_cq_channel = cc_;
    id_->send_cq_channel = cc_;
  }
  id_->pd = pd_;

  cq_ = ::ibv_create_cq(id_->verbs, cq_capacity, this, cc_, 0);
  checkp(cq_, "fail to allocate cq");
  if (use_comp_channel) {
    ret = ::ibv_req_notify_cq(cq_, 0);
    check(ret, "fail to request completion notification on a cq");
  }
  id_->recv_cq = cq_;
  id_->send_cq = cq_;

  buffer_ = new char[max_buffer_size];
  checkp(buffer_, "fail to allocate rpc buffer");
  local_buffer_mr_ =
      ::ibv_reg_mr(pd_, buffer_, max_buffer_size,
                   IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                       IBV_ACCESS_REMOTE_READ);
  checkp(local_buffer_mr_, "fail to register rpc buffer");

  switch (t_) {
  case ClientSide: {
    meta_mr_ = ::ibv_reg_mr(pd_, local_buffer_mr_, sizeof(ibv_mr),
                            IBV_ACCESS_LOCAL_WRITE);
    break;
  }
  case ServerSide: {
    meta_mr_ = ::ibv_reg_mr(pd_, &remote_buffer_mr_, sizeof(ibv_mr),
                            IBV_ACCESS_LOCAL_WRITE);
    break;
  }
  }
  checkp(meta_mr_, "fail to register remote meta receiver");

  info("allocate protection domain, completion queue and memory region");

  ibv_qp_init_attr attr = defaultQpInitAttr();
  attr.recv_cq = cq_;
  attr.send_cq = cq_;
  ret = ::rdma_create_qp(id_, pd_, &attr);
  check(ret, "fail to create queue pair");
  qp_ = id_->qp;

  info("create queue pair");

  // self defined connection parameters
  ::memset(&param_, 0, sizeof(param_));
  param_.responder_resources = 128;
  param_.initiator_depth = 128;
  param_.retry_count = 3;
  param_.rnr_retry_count = 7;

  info("initialize connection parameters");
}

auto Conn::registerCompEvent(::event_base *base) -> int {
  if (cc_ == nullptr or base == nullptr) {
    return EINVAL;
  }
  comp_event_ =
      ::event_new(base, cc_->fd, EV_READ | EV_PERSIST, &Conn::onRecv, this);
  if (comp_event_ == nullptr) {
    return ENOMEM;
  }
  return ::event_add(comp_event_, nullptr);
}

auto Conn::onRecv([[gnu::unused]] int fd, [[gnu::unused]] short what, void *arg)
    -> void {
  Conn *conn = reinterpret_cast<Conn *>(arg);
  ibv_cq *cq = nullptr;
  void *ctx = nullptr;
  auto ret = ::ibv_get_cq_event(conn->cc_, &cq, &ctx);
  ::ibv_ack_cq_events(cq, 1);
  if (ret != 0) {
    info("meet an error cq event");
    return;
  }

  ret = ::ibv_req_notify_cq(cq, 0);
  if (ret != 0) {
    info("fail to request completion notification on a cq");
    return;
  }

  info("got a cq event");
  ibv_wc wc;
  conn->pollCq(&wc);
  if (wc.status != IBV_WC_SUCCESS) {
    info("meet an error wc, status: %d", wc.status);
    return;
  }

  // TODO: make this a general rpc handler
  switch (wc.opcode) {
  case IBV_WC_RECV: {
    info("remote memory region: address: %p, length: %d",
         conn->remote_buffer_mr_.addr, conn->remote_buffer_mr_.length);
    ret = conn->postRead(conn->local_buffer_mr_);
    check(ret, "fail to read request");
    break;
  }
  case IBV_WC_RDMA_READ: {
    info("read from remote side, request content: %s", conn->buffer_);
    ::memcpy(conn->buffer_, "world", 5);
    ret = conn->postWriteImm(conn->local_buffer_mr_);
    check(ret, "fail to write response");
    break;
  }
  case IBV_WC_RDMA_WRITE: {
    info("write to remote side, response content: %s", conn->buffer_);
    ::memset(&conn->remote_buffer_mr_, 0, sizeof(ibv_mr));
    ::memset(conn->buffer_, 0, max_buffer_size);
    ret = conn->postRecv(conn->meta_mr_);
    check(ret, "fail to pre-post recv");
    info("pre post for the next request");
    break;
  }
  default: {
    info("unexpected wc opcode: %d", wc.opcode);
    break;
  }
  }
}

auto Conn::postRecv(ibv_mr *mr) -> int {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
  };
  ibv_recv_wr wr{
      .wr_id = 0,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
  };
  ibv_recv_wr *bad = nullptr;
  return ::ibv_post_recv(qp_, &wr, &bad);
}

auto Conn::postSend(ibv_mr *mr) -> int {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
  };
  ibv_send_wr wr{
      .wr_id = 0,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_SEND,
      .send_flags = IBV_SEND_SIGNALED,
  };
  ibv_send_wr *bad = nullptr;
  return ::ibv_post_send(qp_, &wr, &bad);
}

auto Conn::postRead(ibv_mr *mr) -> int {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
  };
  ibv_send_wr wr{
      .wr_id = 0,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_RDMA_READ,
      .send_flags = IBV_SEND_SIGNALED,
      .wr{
          .rdma{
              .remote_addr = (uintptr_t)remote_buffer_mr_.addr,
              .rkey = remote_buffer_mr_.rkey,
          },
      },
  };
  ibv_send_wr *bad = nullptr;
  return ::ibv_post_send(qp_, &wr, &bad);
}

auto Conn::postWriteImm(ibv_mr *mr) -> int {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
  };
  ibv_send_wr wr{
      .wr_id = 0,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
      .send_flags = IBV_SEND_SIGNALED,
      .imm_data = max_buffer_size,
      .wr{
          .rdma{
              .remote_addr = (uintptr_t)remote_buffer_mr_.addr,
              .rkey = remote_buffer_mr_.rkey,
          },
      },
  };
  ibv_send_wr *bad = nullptr;
  return ::ibv_post_send(qp_, &wr, &bad);
}

auto Conn::pollCq(ibv_wc *wc) -> void {
  do {
    auto ret = ::ibv_poll_cq(cq_, 1, wc);
    if (ret < 0) {
      info("meet an error when polling cq, errno: %d", errno);
      break;
    } else if (ret == 0) {
      continue;
    } else {
      info("got one wc");
      assert(ret == 1);
      break;
    }
  } while (true);
}

Conn::~Conn() {
  int ret = 0;
  if (comp_event_ != nullptr) {
    ret = ::event_del(comp_event_);
    warn(ret, "fail to deregister completion event");
    ::event_free(comp_event_);
  }
  ret = ::ibv_destroy_qp(qp_);
  warn(ret, "fail to destroy qp");
  ret = ::ibv_destroy_cq(cq_);
  warn(ret, "fail to destroy cq");
  if (cc_ != nullptr) {
    ret = ::ibv_destroy_comp_channel(cc_);
    warn(ret, "fail to destroy cc");
  }
  ret = ::rdma_destroy_id(id_);
  warn(ret, "fail to destroy id");
  ret = ::ibv_dereg_mr(local_buffer_mr_);
  warn(ret, "fail to deregister rpc buffer");
  ret = ::ibv_dereg_mr(meta_mr_);
  warn(ret, "fail to deregister remote meta receiver");
  ret = ::ibv_dealloc_pd(pd_);
  warn(ret, "fail to deallocate pd");
  delete[] buffer_;

  info("cleanup connection resources");
}

} // namespace rdma