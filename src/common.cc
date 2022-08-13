#include "common.h"
#include "error.h"

namespace rdma {

Conn::Conn(rdma_cm_id *id) : id_(id) {
  pd_ = ::ibv_alloc_pd(id_->verbs);
  checkp(pd_, "fail to allocate pd");
  cq_ = ::ibv_create_cq(id_->verbs, cq_capacity, nullptr, nullptr, 0);
  checkp(cq_, "fail to allocate cq");
  buffer_ = new char[max_rpc_buffer_size];
  checkp(buffer_, "fail to allocate rpc buffer");
  local_mr_ =
      ::ibv_reg_mr(pd_, buffer_, max_rpc_buffer_size, IBV_ACCESS_LOCAL_WRITE);
  checkp(local_mr_, "fail to register rpc buffer");

  info("allocate protection domain, completion queue and memory region");

  ibv_qp_init_attr attr = defaultQpInitAttr();
  attr.recv_cq = cq_;
  attr.send_cq = cq_;
  auto ret = ::rdma_create_qp(id_, pd_, &attr);
  check(ret, "fail to create queue pair");
  qp_ = id_->qp;

  info("create queue pair");

  ::memset(&param_, 0, sizeof(param_));
  param_.private_data = local_mr_;
  param_.private_data_len = sizeof(ibv_mr);
  // The length of the private data provided by the user is limited to 196 bytes
  // for RDMA_PS_TCP, or 136 bytes for RDMA_PS_UDP.
  param_.initiator_depth = param_.responder_resources = 128;
  param_.retry_count = 3;

  info("initialize connection parameters");
}

Conn::~Conn() {
  int ret = 0;
  ::rdma_destroy_ep(id_); // destroy qp cq cc srq id
  ret = ::ibv_dereg_mr(local_mr_);
  warn(ret, "fail to deregister rpc buffer");
  ret = ::ibv_dealloc_pd(pd_);
  warn(ret, "fail to deallocate pd");
  delete[] buffer_;

  info("cleanup connection resources");
}

} // namespace rdma