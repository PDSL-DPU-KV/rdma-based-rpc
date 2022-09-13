#include "client.hh"

namespace rdma {

Client::Client(const char *host, const char *port) {
  int ret = 0;
  rdma_cm_event *e;
  rdma_cm_id *id;

  ret = getaddrinfo(host, port, nullptr, &addr_);
  check(ret, "fail to parse host and port");

  info("remote address: %s:%s", host, port);

  ec_ = rdma_create_event_channel();
  checkp(ec_, "fail to create event channel");
  ret = rdma_create_id(ec_, &id, nullptr, RDMA_PS_TCP);
  check(ret, "fail to create cm id");

  info("initialize event channel and identifier");

  ret = rdma_resolve_addr(id, nullptr, addr_->ai_addr,
                          default_connection_timeout);
  check(ret, "fail to resolve address");
  e = waitEvent(RDMA_CM_EVENT_ADDR_RESOLVED);
  checkp(e, "do not get the resolved address event");
  ret = rdma_ack_cm_event(e);
  check(ret, "fail to handle resolved address event");

  info("resolve the address");

  ret = rdma_resolve_route(id, default_connection_timeout);
  check(ret, "fail to resolve route");
  e = waitEvent(RDMA_CM_EVENT_ROUTE_RESOLVED);
  checkp(e, "do not get the resolved route event");
  ret = rdma_ack_cm_event(e);
  check(ret, "fail to handle resolved route address event");

  info("resolve the route");

  conn_ = new Conn(id, max_context_num);
  ret = rdma_connect(id, &conn_->param_);
  check(ret, "fail to connect the remote side");
  e = waitEvent(RDMA_CM_EVENT_ESTABLISHED);
  checkp(e, "do not get the established connection event");
  conn_->remote_buffer_key_ = *(uint32_t *)e->param.conn.private_data;
  ret = rdma_ack_cm_event(e);
  check(ret, "fail to handle established connection event");

  info("establish the connection");

  for (uint32_t i = 0; i < max_context_num; i++) {
    ctx_ring_.push(
        new Context(conn_, conn_->bufferPage(i), Conn::buffer_page_size));
  }

#ifdef USE_NOTIFY
  ret = evthread_use_pthreads();
  check(ret, "fail to open multi-thread support for libevent");
  base_ = event_base_new();
  checkp(base_, "fail to allocate event loop");
  ret = conn_->registerCompEvent(base_);
  check(ret, "fail to register completion event");
  bg_poller_ = new std::thread([this]() -> void {
    info("background poller start working");
    check(event_base_dispatch(base_), "poller stop with error");
    info("background poller stop working");
  });
#endif
}

auto Client::call(uint32_t rpc_id, const message_t &request,
                  message_t &response) -> void {
  Context *ctx = nullptr;
  while (not ctx_ring_.pop(ctx)) {
    pause();
  }
  ctx->call(rpc_id, request);
  ctx->wait(response);
  assert(ctx_ring_.push(ctx));
}

auto Client::waitEvent(rdma_cm_event_type expected) -> rdma_cm_event * {
  rdma_cm_event *cm_event = nullptr;
  auto ret = rdma_get_cm_event(ec_, &cm_event); // block for an event
  if (ret != 0) {
    info("fail to get cm event");
    return nullptr;
  }
  if (cm_event->status != 0) { // bad
    info("get a bad cm event");
  } else if (cm_event->event != expected) { // unexpected
    info("got: %s, expected: %s", rdma_event_str(cm_event->event),
         rdma_event_str(expected));
  } else {           // good
    return cm_event; // ack outside...
  }
  if (rdma_ack_cm_event(cm_event) != 0) {
    info("fail to ack the error event");
  }
  return nullptr;
}

Client::~Client() {
  if (conn_ == nullptr) {
    return;
  }
  int ret = 0;
  rdma_cm_event *e = nullptr;

#ifdef USE_NOTIFY
  ret = event_base_loopbreak(base_);
  check(ret, "fail to stop event loop");
  bg_poller_->join();
  delete bg_poller_;
  event_base_free(base_);
#endif

  Context *ctx;
  for (uint32_t i = 0; i < max_context_num; i++) {
    assert(ctx_ring_.pop(ctx));
    delete ctx;
  }

  ret = rdma_disconnect(conn_->id_);
  warn(ret, "fail to disconnect");
  e = waitEvent(RDMA_CM_EVENT_DISCONNECTED);
  warnp(e, "do not get the disconnected event");
  ret = rdma_ack_cm_event(e);
  warnp(e, "fail to ack handle the disconnected connection event");

  info("disconnect with the remote side");

  delete conn_;

  rdma_destroy_event_channel(ec_);
  freeaddrinfo(addr_);

  info("cleanup the local resources");
}

auto Client::Context::call(uint32_t rpc_id, const message_t &request) -> void {
  assert(state_ == Vacant);
  setRequest(request);
  state_ = SendingBufferMeta;
  conn_->postSend(this, meta_mr_->addr, meta_mr_->length, meta_mr_->lkey);
  // NOTICE: must pre-post at here
  conn_->postRecv(this, rawBuf(), readableLength(), conn_->buffer_mr_->lkey);
}

Client::Context::Context(Conn *conn, void *buffer, uint32_t size)
    : ConnCtx(conn, buffer, size) {
  meta_mr_ =
      ibv_reg_mr(conn->pd_, &meta_, sizeof(BufferMeta), IBV_ACCESS_LOCAL_WRITE);
  checkp(meta_mr_, "fail to register local meta sender");
}

auto Client::Context::advance(const ibv_wc &wc) -> void {
  switch (wc.opcode) {
  case IBV_WC_SEND: {
    assert(state_ == SendingBufferMeta);
    info("send request buffer meta to the remote");
    state_ = WaitingForResponse;
    break;
  }
  case IBV_WC_RECV_RDMA_WITH_IMM: {
    assert(state_ == WaitingForResponse);
    state_ = Vacant;
    cv_.notify_all();
    break;
  }
  default: {
    info("unexpected wc opcode: %d", wc.opcode);
    break;
  }
  }
}

auto Client::Context::wait(message_t &response) -> void {
  std::unique_lock<std::mutex> l(mu_);
  cv_.wait(l, [this]() -> bool { return state_ == Vacant; });
  getResponse(response);
}

Client::Context::~Context() {
  check(ibv_dereg_mr(meta_mr_), "fail to deregister remote meta receiver");
}

} // namespace rdma