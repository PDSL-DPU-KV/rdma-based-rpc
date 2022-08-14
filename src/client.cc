#include "client.h"
#include "error.h"

#include <cassert>

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
  ::memcpy(&conn_->remote_buffer_mr_, e->param.conn.private_data,
           sizeof(conn_->remote_buffer_mr_));
  ret = ::rdma_ack_cm_event(e);
  check(ret, "fail to handle established connection event");

  info("establish the connection");
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

// TODO: make this a general rpc call
auto Client::call() -> int {
  ::memcpy(conn_->buffer_, "hello", 6);

  info("request content: %s", conn_->buffer_);

  ibv_wc wc;
  conn_->postSend(conn_->meta_mr_);
  conn_->pollCq(&wc);
  if (wc.status != IBV_WC_SUCCESS) {
    info("fail to send local buffer meta");
    return -1;
  }

  info("local buffer region: address: %p, length: %d", conn_->buffer_,
       Conn::max_buffer_size);

  conn_->postRecv(conn_->local_buffer_mr_);
  conn_->pollCq(&wc);
  if (wc.status != IBV_WC_SUCCESS) {
    info("fail to send local buffer meta");
    return -1;
  }

  info("response content: %s", conn_->buffer_);

  ::memset(conn_->buffer_, 0, Conn::max_buffer_size);
  return 0;
}

Client::~Client() {
  if (conn_ == nullptr) {
    return;
  }
  int ret = 0;
  rdma_cm_event *e = nullptr;
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