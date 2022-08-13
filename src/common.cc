#include "common.h"
#include "error.h"

#include <cassert>

namespace rdma {

Conn::Conn(rdma_cm_id *id, ibv_comp_channel *cc) : id_(id) {
  int ret = 0;

  pd_ = ::ibv_alloc_pd(id_->verbs);
  checkp(pd_, "fail to allocate pd");
  cq_ = ::ibv_create_cq(id_->verbs, cq_capacity, this, cc, 0);
  checkp(cq_, "fail to allocate cq");
  if (cc != nullptr) {
    ret = ::ibv_req_notify_cq(cq_, 0);
    check(ret, "fail to request completion notification on a cq");
  }

  send_buffer_ = new char[max_buffer_size];
  checkp(send_buffer_, "fail to allocate rpc buffer");
  send_mr_ =
      ::ibv_reg_mr(pd_, send_buffer_, max_buffer_size, IBV_ACCESS_LOCAL_WRITE);
  checkp(send_mr_, "fail to register rpc buffer");

  recv_buffer_ = new char[max_buffer_size];
  checkp(recv_buffer_, "fail to allocate rpc buffer");
  recv_mr_ = ::ibv_reg_mr(pd_, recv_buffer_, max_buffer_size,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                              IBV_ACCESS_REMOTE_READ);
  checkp(recv_mr_, "fail to register rpc buffer");

  info("allocate protection domain, completion queue and memory region");

  ibv_qp_init_attr attr = defaultQpInitAttr();
  attr.recv_cq = cq_;
  attr.send_cq = cq_;
  ret = ::rdma_create_qp(id_, pd_, &attr);
  check(ret, "fail to create queue pair");
  qp_ = id_->qp;

  info("create queue pair");

  ::memset(&param_, 0, sizeof(param_));
  param_.private_data = recv_mr_;
  param_.private_data_len = sizeof(ibv_mr);
  // The length of the private data provided by the user is limited to 196 bytes
  // for RDMA_PS_TCP, or 136 bytes for RDMA_PS_UDP.
  param_.initiator_depth = 128;
  param_.responder_resources = 128;
  param_.retry_count = 3;

  info("initialize connection parameters");
}

auto Conn::postRecv() -> void {
  ibv_sge sge = {
      .addr = (uintptr_t)recv_mr_->addr,
      .length = (uint32_t)recv_mr_->length,
      .lkey = recv_mr_->lkey,
  };
  ibv_recv_wr wr = {
      .wr_id = 0,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
  };
  ibv_recv_wr *bad = nullptr;

  auto ret = ::ibv_post_recv(qp_, &wr, &bad);
  warn(ret, "fail to post receive");
}

auto Conn::postSend() -> void {
  ibv_sge sge = {
      .addr = (uintptr_t)send_mr_->addr,
      .length = (uint32_t)send_mr_->length,
      .lkey = send_mr_->lkey,
  };
  ibv_send_wr wr = {
      .wr_id = 0,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_SEND,
      .send_flags = IBV_SEND_SIGNALED,
  };
  ibv_send_wr *bad = nullptr;

  auto ret = ::ibv_post_send(qp_, &wr, &bad);
  warn(ret, "fail to post send");
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

auto Conn::recvBuffer() -> char * { return recv_buffer_; }
auto Conn::sendBuffer() -> char * { return send_buffer_; }

Conn::~Conn() {
  int ret = 0;
  ::rdma_destroy_ep(id_); // destroy qp cq cc srq id
  ret = ::ibv_dereg_mr(send_mr_);
  warn(ret, "fail to deregister rpc buffer");
  ret = ::ibv_dereg_mr(recv_mr_);
  warn(ret, "fail to deregister rpc buffer");
  ret = ::ibv_dealloc_pd(pd_);
  warn(ret, "fail to deallocate pd");
  delete[] send_buffer_;
  delete[] recv_buffer_;

  info("cleanup connection resources");
}

} // namespace rdma