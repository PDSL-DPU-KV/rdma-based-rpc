#include "client.h"
#include "error.h"

namespace rdma {

Client::Client(const char *host, const char *port) {
  int ret = 0;
  rdma_cm_event *e;
  rdma_cm_id *id;

  ret = ::getaddrinfo(host, port, nullptr, &addr_);
  check(ret, "fail to parse host and port");

  info("remote address: %s:%s", host, port);

  ec_ = ::rdma_create_event_channel();
  checkp(ec_, "fail to create event channel");
  ret = ::rdma_create_id(ec_, &id, nullptr, RDMA_PS_TCP);
  check(ret, "fail to create cm id");

  info("initialize event channel and identifier");

  ret = ::rdma_resolve_addr(id, nullptr, addr_->ai_addr,
                            default_connection_timeout);
  check(ret, "fail to resolve address");
  e = waitEvent(RDMA_CM_EVENT_ADDR_RESOLVED);
  checkp(e, "do not get the resolved address event");
  ret = ::rdma_ack_cm_event(e);
  check(ret, "fail to handle resolved address event");

  info("resolve the address");

  ret = ::rdma_resolve_route(id, default_connection_timeout);
  check(ret, "fail to resolve route");
  e = waitEvent(RDMA_CM_EVENT_ROUTE_RESOLVED);
  checkp(e, "do not get the resolved route event");
  ret = ::rdma_ack_cm_event(e);
  check(ret, "fail to handle resolved route address event");

  info("resolve the route");

  conn_ = new Conn(Conn::ClientSide, id);
  ret = ::rdma_connect(id, &conn_->param_);
  check(ret, "fail to connect the remote side");
  e = waitEvent(RDMA_CM_EVENT_ESTABLISHED);
  checkp(e, "do not get the established connection event");
  ret = ::rdma_ack_cm_event(e);
  check(ret, "fail to handle established connection event");

  info("establish the connection");

  ret = ::evthread_use_pthreads();
  check(ret, "fail to open multi-thread support for libevent");

  base_ = ::event_base_new();
  checkp(base_, "fail to allocate event loop");

  ret = conn_->registerCompEvent(base_);
  check(ret, "fail to register completion event");

  bg_poller_ = new std::thread([this]() {
    info("background poller start working");
    auto ret = ::event_base_dispatch(base_);
    check(ret, "poller stop with error");
    info("background poller stop working");
  });
}

auto Client::waitEvent(rdma_cm_event_type expected) -> rdma_cm_event * {
  rdma_cm_event *cm_event = nullptr;
  auto ret = ::rdma_get_cm_event(ec_, &cm_event); // block for an event
  if (ret != 0) {
    info("fail to get cm event");
    return nullptr;
  }
  if (cm_event->status != 0) { // bad
    info("get a bad cm event");
  } else if (cm_event->event != expected) { // unexpected
    info("got: %s, expected: %s", ::rdma_event_str(cm_event->event),
         ::rdma_event_str(expected));
  } else {           // good
    return cm_event; // ack outside...
  }
  if (::rdma_ack_cm_event(cm_event) != 0) {
    info("fail to ack the error event");
  }
  return nullptr;
}

auto Client::call(int id, int n) -> void {
  ConnCtx *ctx = new ConnCtx(0, conn_);
  char s[10]{};
  snprintf(s, 10, "%d-%d", id, n);
  ctx->fillBuffer(s, 10);
  ctx->trigger();
  ctx->wait();
  delete ctx;
}

Client::~Client() {
  if (conn_ == nullptr) {
    return;
  }
  int ret = 0;
  rdma_cm_event *e = nullptr;

  ret = ::event_base_loopbreak(base_);
  check(ret, "fail to stop event loop");

  bg_poller_->join();
  delete bg_poller_;

  ::event_base_free(base_);

  ret = ::rdma_disconnect(conn_->id_);
  warn(ret, "fail to disconnect");
  e = waitEvent(RDMA_CM_EVENT_DISCONNECTED);
  warnp(e, "do not get the disconnected event");
  ret = ::rdma_ack_cm_event(e);
  warnp(e, "fail to ack handle the disconnected connection event");

  info("disconnect with the remote side");

  delete conn_;

  ::rdma_destroy_event_channel(ec_);
  ::freeaddrinfo(addr_);

  info("cleanup the local resources");
}

} // namespace rdma