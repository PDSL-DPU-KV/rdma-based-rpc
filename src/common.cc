#include "common.h"
#include "error.h"
#include <cassert>

namespace rdma {

Conn::Conn(Side t, rdma_cm_id *id) : t_(t), id_(id) {
  int ret = 0;

  pd_ = ::ibv_alloc_pd(id_->verbs);
  checkp(pd_, "fail to allocate pd");
  cc_ = ::ibv_create_comp_channel(id_->verbs);
  checkp(cc_, "fail to create completion channel");
  id_->recv_cq_channel = cc_;
  id_->send_cq_channel = cc_;
  id_->pd = pd_;

  cq_ = ::ibv_create_cq(id_->verbs, cq_capacity, this, cc_, 0);
  checkp(cq_, "fail to allocate cq");
  ret = ::ibv_req_notify_cq(cq_, 0);
  check(ret, "fail to request completion notification on a cq");
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
  param_.responder_resources = cq_capacity;
  param_.initiator_depth = cq_capacity;
  param_.retry_count = 7;
  param_.rnr_retry_count = 7; // '7' indicates retry infinitely

  info("initialize connection parameters");
  if (t_ == ServerSide) {
    for (uint32_t i = 0; i < max_context_num; i++) {
      ctx_[i] = new ConnCtx(i, this);
      ctx_[i]->prepare();
    }
  }
  info("initialize connection buffers");
}

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

  ret = ::ibv_req_notify_cq(cq, 0);
  if (ret != 0) {
    info("fail to request completion notification on a cq");
    return;
  }

  ibv_wc wc[cq_capacity];
  ret = ::ibv_poll_cq(conn->cq_, cq_capacity, wc);
  if (ret < 0) {
    info("poll cq error");
    return;
  } else if (ret == 0) { // this may caused by a timeout
    info("cq is empty");
    return;
  }

  // we use wc.wr_id to match the Connection Context
  for (int i = 0; i < ret; i++) {
    reinterpret_cast<ConnCtx *>(wc[i].wr_id)->advance(wc[i].opcode);
  }
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
      .imm_data = 0x1234, // TODO: use imm_data to pass some meta data
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

auto Conn::qpState() -> void {
  ibv_qp_attr attr;
  ibv_qp_init_attr init_attr;
  ibv_query_qp(qp_, &attr,
               IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                   IBV_QP_RNR_RETRY,
               &init_attr);
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
  if (state_ == Vacant and conn_->t_ == Conn::ClientSide) {
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
  info("local memory region: address: %p, length: %d", local_buffer_mr_->addr,
       local_buffer_mr_->length);
  state_ = SendingBufferMeta;
  conn_->postSend(this, meta_mr_);
}

auto ConnCtx::prepare() -> void {
  assert(state_ == Vacant);
  state_ = WaitingForBufferMeta;
  conn_->postRecv(this, meta_mr_);
}

auto ConnCtx::buffer() -> const char * { return buffer_; }

auto ConnCtx::state() -> State { return state_; }

auto ConnCtx::id() -> uint32_t { return id_; }

auto ConnCtx::wait() -> void {
  std::unique_lock<std::mutex> l(mu_);
  cv_.wait(l, [this] { return state_ == Vacant; });
}

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
    state_ = ReadingRequest;
    conn_->postRead(this, local_buffer_mr_, &remote_buffer_mr_);
    break;
  }
  case IBV_WC_RDMA_READ: {
    assert(state_ == ReadingRequest);
    info("read from remote side, request content: %s", buffer_);
    state_ = FilledWithRequest;

    {
      // TODO: make this a self-defined handler
      char s[10]{};
      snprintf(s, 10, "%s-done", buffer_);
      fillBuffer(s, 10);
    }

    assert(state_ == FilledWithResponse);
    info("write to remote side, response content: %s", buffer_);
    state_ = WritingResponse;
    conn_->postWriteImm(this, local_buffer_mr_, &remote_buffer_mr_);
    break;
  }
  case IBV_WC_RDMA_WRITE: {
    assert(state_ == WritingResponse);
    info("write done, wait for next request");
    state_ = WaitingForBufferMeta;
    conn_->postRecv(this, meta_mr_);
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
    state_ = WaitingForResponse;
    conn_->postRecv(this, local_buffer_mr_);
    break;
  }
  case IBV_WC_RECV_RDMA_WITH_IMM: {
    assert(state_ == WaitingForResponse);
    info("receive from remote, response content: %s", buffer_);
    state_ = FilledWithResponse;
    {
      // TODO: make this a self-defined callback
      state_ = Vacant;
      cv_.notify_all();
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