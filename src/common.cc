#include "common.h"
#include "error.h"
#include <cassert>

namespace rdma {

Conn::Conn(Side t, rdma_cm_id *id, bool use_comp_channel) : t_(t), id_(id) {
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
  param_.responder_resources = max_context_num;
  param_.initiator_depth = max_context_num;
  param_.retry_count = 3;
  param_.rnr_retry_count = 15;

  info("initialize connection parameters");

  for (uint32_t i = 0; i < max_context_num; i++) {
    ctx_[i] = new ConnCtx(i, this);
    if (t_ == ServerSide) {
      ctx_[i]->prepare();
    }
  }

  info("initialize connection buffers");
}

auto Conn::registerCompEvent(::event_base *base, ::event_callback_fn fn)
    -> int {
  if (cc_ == nullptr or base == nullptr) {
    return EINVAL;
  }
  comp_event_ = ::event_new(base, cc_->fd, EV_READ | EV_PERSIST, fn, this);
  if (comp_event_ == nullptr) {
    return ENOMEM;
  }
  return ::event_add(comp_event_, nullptr);
}

auto Conn::fetchVacantBuffer() -> ConnCtx * {
  ConnCtx::State vacant_state = ConnCtx::Vacant;
  for (uint32_t i = 0; i < max_context_num; i++) {
    if (ctx_[i]->state_.compare_exchange_strong(vacant_state,
                                                ConnCtx::ReadyForCall)) {
      return ctx_[i];
    }
  }
  return nullptr;
}

auto Conn::postRecv(void *ctx, ibv_mr *mr) -> void {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
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

auto Conn::postSend(void *ctx, ibv_mr *mr) -> void {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
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

auto Conn::postRead(void *ctx, ibv_mr *mr, ibv_mr *remote_mr) -> void {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
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
              .remote_addr = (uintptr_t)remote_mr->addr,
              .rkey = remote_mr->rkey,
          },
      },
  };
  ibv_send_wr *bad = nullptr;
  auto ret = ::ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post read");
}

auto Conn::postWriteImm(void *ctx, ibv_mr *mr, ibv_mr *remote_mr) -> void {
  ibv_sge sge{
      .addr = (uintptr_t)mr->addr,
      .length = (uint32_t)mr->length,
      .lkey = mr->lkey,
  };
  ibv_send_wr wr{
      .wr_id = (uintptr_t)ctx,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
      .send_flags = IBV_SEND_SIGNALED,
      .imm_data = 0x1234,
      .wr{
          .rdma{
              .remote_addr = (uintptr_t)remote_mr->addr,
              .rkey = remote_mr->rkey,
          },
      },
  };
  ibv_send_wr *bad = nullptr;
  auto ret = ::ibv_post_send(qp_, &wr, &bad);
  check(ret, "fail to post write");
}

auto Conn::pollCq(ibv_wc *wc, int n) -> void {
  int got = 0;
  do {
    auto ret = ::ibv_poll_cq(cq_, n, &wc[got]);
    if (ret < 0) {
      info("meet an error when polling cq, errno: %d", errno);
      break;
    }
    if (ret > 0) {
      info("got %d wc", ret);
    }
    got += ret;
    if (got == n) {
      break;
    }
  } while (true);
}

auto Conn::qpState() -> void {
  ibv_qp_attr attr;
  ibv_qp_init_attr init_attr;
  ibv_query_qp(qp_, &attr, IBV_QP_STATE, &init_attr);
  return;
}

auto Conn::type() -> Side { return t_; }

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

  for (auto p : ctx_) {
    delete p;
  }

  ret = ::ibv_dealloc_pd(pd_);
  warn(ret, "fail to deallocate pd");

  info("cleanup connection resources");
}

ConnCtx::ConnCtx(uint32_t id, Conn *conn) : id_(id), conn_(conn) {
  buffer_ = new char[max_buffer_size];
  checkp(buffer_, "fail to allocate rpc buffer");

  local_buffer_mr_ =
      ::ibv_reg_mr(conn->pd_, buffer_, max_buffer_size,
                   IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                       IBV_ACCESS_REMOTE_READ);
  checkp(local_buffer_mr_, "fail to register rpc buffer");

  switch (conn->type()) {
  case Conn::ClientSide: {
    meta_mr_ = ::ibv_reg_mr(conn->pd_, local_buffer_mr_, sizeof(ibv_mr),
                            IBV_ACCESS_LOCAL_WRITE);
    checkp(meta_mr_, "fail to register local meta sender");
    break;
  }
  case Conn::ServerSide: {
    meta_mr_ = ::ibv_reg_mr(conn->pd_, &remote_buffer_mr_, sizeof(ibv_mr),
                            IBV_ACCESS_LOCAL_WRITE);
    checkp(meta_mr_, "fail to register remote meta receiver");
    break;
  }
  }
}

auto ConnCtx::fillBuffer(const char *src, uint32_t len) -> void {
  assert(len <= max_buffer_size);
  if (state_ == ReadyForCall and conn_->t_ == Conn::ClientSide) {
    state_ = FilledWithRequest;
  } else if (state_ == FilledWithRequest and conn_->t_ == Conn::ServerSide) {
    state_ = FilledWithResponse;
  } else {
    assert(false);
  }
  ::memcpy(buffer_, src, len);
}

auto ConnCtx::trigger() -> void {
  assert(state_ == FilledWithRequest);
  conn_->postSend(this, meta_mr_);
  state_ = SendingBufferMeta;
}

auto ConnCtx::prepare() -> void {
  assert(state_ == Vacant);
  conn_->postRecv(this, meta_mr_);
  state_ = WaitingForBufferMeta;
}

auto ConnCtx::buffer() -> const char * { return buffer_; }

auto ConnCtx::state() -> State { return state_; }

auto ConnCtx::advance(int32_t finished_op) -> void {
  switch (conn_->type()) {
  case Conn::ServerSide: {
    advanceServerSide(finished_op);
    break;
  }
  case Conn::ClientSide: {
    advanceClientSide(finished_op);
    break;
  }
  }
}

auto ConnCtx::advanceServerSide(int32_t finished_op) -> void {
  switch (finished_op) {
  case IBV_WC_RECV: {
    assert(state_ == WaitingForBufferMeta);
    info("remote memory region: address: %p, length: %d",
         remote_buffer_mr_.addr, remote_buffer_mr_.length);
    conn_->postRead(this, local_buffer_mr_, &remote_buffer_mr_);
    state_ = ReadingRequest;
    break;
  }
  case IBV_WC_RDMA_READ: {
    assert(state_ == ReadingRequest);
    info("read from remote side, request content: %s", buffer_);
    state_ = FilledWithRequest;

    {
      // TODO: make this a self-defined handler
      fillBuffer("world", 5);
    }

    assert(state_ == FilledWithResponse);
    conn_->postWriteImm(this, local_buffer_mr_, &remote_buffer_mr_);
    state_ = WritingResponse;
    break;
  }
  case IBV_WC_RDMA_WRITE: {
    assert(state_ == WritingResponse);
    info("write to remote side, response content: %s", buffer_);
    conn_->postRecv(this, meta_mr_);
    state_ = WaitingForBufferMeta;
    break;
  }
  default: {
    info("unexpected wc opcode: %d", finished_op);
    break;
  }
  }
}

auto ConnCtx::advanceClientSide(int32_t finished_op) -> void {
  switch (finished_op) {
  case IBV_WC_SEND: {
    assert(state_ == SendingBufferMeta);
    info("send request buffer meta to the remote");
    conn_->postRecv(this, local_buffer_mr_);
    state_ = WaitingForResponse;
    break;
  }
  case IBV_WC_RECV_RDMA_WITH_IMM: {
    assert(state_ == WaitingForResponse);
    state_ = FilledWithResponse;
    info("receive from remote, response content: %s", buffer_);
    {
      // TODO: make this a self-defined callback
      state_ = Vacant;
    }
    break;
  }
  default: {
    info("unexpected wc opcode: %d", finished_op);
    break;
  }
  }
}

ConnCtx::~ConnCtx() {
  int ret = 0;
  ret = ::ibv_dereg_mr(local_buffer_mr_);
  warn(ret, "fail to deregister rpc buffer");
  ret = ::ibv_dereg_mr(meta_mr_);
  warn(ret, "fail to deregister remote meta receiver");
  delete[] buffer_;
}

} // namespace rdma