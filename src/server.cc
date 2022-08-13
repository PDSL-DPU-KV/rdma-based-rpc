#include "server.h"
#include "error.h"

namespace rdma {

Server::Server(const char *host, const char *port) {
  int ret = 0;
  ret = ::getaddrinfo(host, port, nullptr, &addr_);
  check(ret, "fail to parse host and port");

  info("listening address: %s:%s", host, port);

  ec_ = ::rdma_create_event_channel();
  checkp(ec_, "fail to create event channel");
  ret = ::rdma_create_id(ec_, &cm_id_, nullptr, RDMA_PS_TCP);
  check(ret, "fail to create cm id");

  info("initialize event channel and identifier");

  ret = ::rdma_bind_addr(cm_id_, addr_->ai_addr);
  check(ret, "fail to bind address");
  ret = ::rdma_listen(cm_id_, defautl_back_log);
  check(ret, "fail to listen for connections");

  info("bind address and begin listening for connection requests");
}

auto Server::run() -> void {
  running_ = true;
  while (running_) {
    onEvent();
  }
}

auto Server::stop() -> void { running_ = false; }

auto Server::onEvent() -> void {
  rdma_cm_event *e;
  if (::rdma_get_cm_event(ec_, &e) != 0) {
    info("fail to get cm event");
    return;
  }

  if (e->status != 0) {
    info("got a bad event");
    auto ret = ::rdma_ack_cm_event(e);
    warn(ret, "fail to ack event");
    return;
  }

  auto client_id = e->id;
  auto ret = ::rdma_ack_cm_event(e);
  warn(ret, "fail to ack event");

  switch (e->event) {
  case RDMA_CM_EVENT_CONNECT_REQUEST: { // throw when bad connection
    info("handle a connection request");

    auto conn = new Conn(client_id);
    auto ret = ::rdma_accept(client_id, &conn->param_);
    check(ret, "fail to accept connection");
    ::memcpy(&conn->remote_mr_, e->param.conn.private_data,
             e->param.conn.private_data_len);
    client_id->context = conn;

    info("remote memory region: address: %p, length: %d", conn->remote_mr_.addr,
         conn->remote_mr_.length);
    break;
  }
  case RDMA_CM_EVENT_ESTABLISHED: {
    // TODO: add this into a poller
    info("establish and register a connection");
    break;
  }
  case RDMA_CM_EVENT_DISCONNECTED: {
    delete reinterpret_cast<Conn *>(client_id->context);
    info("close a connection");
    break;
  }
  default: {
    info("unexpected event: %s", ::rdma_event_str(e->event));
    break;
  }
  }
}

Server::~Server() {
  auto ret = ::rdma_destroy_id(cm_id_);
  warn(ret, "fail to destroy cm id");
  ::rdma_destroy_event_channel(ec_);

  info("cleanup the local resources");
}

} // namespace rdma