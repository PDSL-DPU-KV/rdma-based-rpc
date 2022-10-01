#include "client.hh"
#include <atomic>
#include <signal.h>

namespace rdma {

Client::Client() {
  ec_ = rdma_create_event_channel();
  checkp(ec_, "fail to create event channel");
  bg_poller_.run();
}

auto Client::connect(const char *host, const char *port) -> uint32_t {
  int ret = 0;
  addrinfo *addr;
  rdma_cm_id *id;
  rdma_cm_event *e;

  ret = rdma_create_id(ec_, &id, nullptr, RDMA_PS_TCP);
  check(ret, "fail to create cm id");

  ret = getaddrinfo(host, port, nullptr, &addr);
  check(ret, "fail to parse host and port");

  info("remote address: %s:%s", host, port);

  info("initialize event channel and identifier");

  ret =
      rdma_resolve_addr(id, nullptr, addr->ai_addr, default_connection_timeout);
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
  uint32_t conn_id = conns_.size();
  auto conn = new ConnWithCtx(conn_id, this, id, addr);
  conns_.push_back(conn);

  bg_poller_.registerConn(conn);
  return conn_id;
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

auto Client::findCtx(uint32_t ctx_id) -> Context * { return id2ctx_[ctx_id]; }

auto Client::call(uint32_t conn_id, uint32_t rpc_id, const message_t &request,
                  message_t &response) -> Status {
  return conns_[conn_id]->call(rpc_id, request, response);
}

Client::~Client() {
  for (auto conn : conns_) {
    bg_poller_.deregisterConn(conn->id_);
    delete conn;
  }
  bg_poller_.stop();
  rdma_destroy_event_channel(ec_);
  info("cleanup the local resources");
}

auto Client::Context::call(uint32_t rpc_id, const message_t &request) -> void {
  assert(state_ == Vacant);
  setRequest(request);
  header().rpc_id_ = rpc_id;
  state_ = SendingBufferMeta;

  // after filled with request, context will be handled by background poller
  l_.lock(); // unlock in adcance
  if (messageType() == MessageType::ImmRequest) {
    conn_->postSend(this, rawBuf(), readableLength(), conn_->localKey());
  } else {
    conn_->postSend(this, rawBuf(), headerLength(), conn_->localKey());
  }
  // NOTICE: must pre-post at here
  conn_->postRecv(this, rawBuf(), rawBufLength(), conn_->localKey());
}

Client::Context::Context(uint32_t id, Conn *conn, void *buffer, uint32_t length)
    : ConnCtx(id, conn, buffer, length) {
  header().addr_ = rawBuf();
}

auto Client::Context::advance(const ibv_wc &wc) -> void {
  switch (wc.opcode) {
  case IBV_WC_SEND: {
    assert(state_ == SendingBufferMeta);
    state_ = WaitingForResponse;
    break;
  }
  case IBV_WC_RECV_RDMA_WITH_IMM: {
    if (wc.imm_data != id_) {
      reinterpret_cast<ConnWithCtx *>(conn_)
          ->c_->findCtx(wc.imm_data)
          ->advance(wc);
      return;
    }
    assert(state_ == WaitingForResponse);
    state_ = Vacant;
    l_.unlock();
    break;
  }
  default: {
    info("unexpected wc opcode: %d", wc.opcode);
    break;
  }
  }
}

auto Client::Context::wait(message_t &response) -> Status {
  std::lock_guard<Spinlock> l(l_);
  if (state_ != Vacant) {
    return Status::CallFailure();
  }
  getResponse(response);
  return Status::Ok();
}

Client::ConnWithCtx::ConnWithCtx(uint16_t conn_id, Client *c, rdma_cm_id *cm_id,
                                 addrinfo *addr)
    : Conn(conn_id, cm_id, max_context_num), addr_(addr), c_(c) {
  auto ret = rdma_connect(cm_id, &param_);
  check(ret, "fail to connect the remote side");
  auto e = c_->waitEvent(RDMA_CM_EVENT_ESTABLISHED);
  checkp(e, "do not get the established connection event");
  remote_buffer_key_ = *(uint32_t *)e->param.conn.private_data;
  ret = rdma_ack_cm_event(e);
  check(ret, "fail to handle established connection event");

  info("establish the connection");

  for (uint32_t i = 0; i < max_context_num; i++) {
    uint32_t ctx_id = ((uint32_t)conn_id << 16) | i;
    auto ctx = new Context(ctx_id, this, bufferPage(i), buffer_page_size);
    senders_[i] = ctx;
    ctx_ring_.push(ctx);
    c_->id2ctx_[ctx_id] = ctx;
  }
}

auto Client::ConnWithCtx::call(uint32_t rpc_id, const message_t &request,
                               message_t &response) -> Status {
  Context *ctx = nullptr;
  ctx_ring_.pop(ctx);
  ctx->call(rpc_id, request);
  auto s = ctx->wait(response);
  ctx_ring_.push(ctx);
  return s;
}

Client::ConnWithCtx::~ConnWithCtx() {
  for (auto ctx : senders_) {
    delete ctx;
  }
  auto ret = rdma_disconnect(cm_id_);
  warn(ret, "fail to disconnect");
  auto e = c_->waitEvent(RDMA_CM_EVENT_DISCONNECTED);
  warnp(e, "do not get the disconnected event");
  ret = rdma_ack_cm_event(e);
  warnp(e, "fail to ack handle the disconnected connection event");
  info("disconnect with the remote side");
  freeaddrinfo(addr_);
}

} // namespace rdma