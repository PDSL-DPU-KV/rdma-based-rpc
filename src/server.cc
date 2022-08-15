#include "server.h"
#include "error.h"
#include "signal.h"

namespace rdma {

Server::Server(const char *host, const char *port) : base_(::event_base_new()) {
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

  conn_event_ = ::event_new(base_, ec_->fd, EV_READ | EV_PERSIST,
                            &Server::onConnEvent, this);
  checkp(conn_event_, "fail to create connection event");
  ret = ::event_add(conn_event_, nullptr);
  check(ret, "fail to register connection event");

  exit_event_ =
      ::event_new(base_, SIGINT, EV_SIGNAL | EV_PERSIST, &Server::onExit, this);
  checkp(exit_event_, "fail to create exit event");
  ret = ::event_add(exit_event_, nullptr);
  check(ret, "fail to register exit event");

  info("register all events into event loop");
}

auto Server::run() -> int {
  info("event loop running");
  return ::event_base_dispatch(base_);
}

auto Server::onExit([[gnu::unused]] int fd, [[gnu::unused]] short what,
                    void *arg) -> void {
  int ret = ::event_base_loopbreak(reinterpret_cast<Server *>(arg)->base_);
  check(ret, "fail to break event loop");
  info("event loop break");
}

auto Server::onConnEvent([[gnu::unused]] int fd, [[gnu::unused]] short what,
                         void *arg) -> void {
  Server *s = reinterpret_cast<Server *>(arg);
  rdma_cm_event *e;
  if (::rdma_get_cm_event(s->ec_, &e) != 0) {
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

    auto conn = new Conn(Conn::ServerSide, client_id, true);

    auto ret = ::rdma_accept(client_id, &conn->param_);
    check(ret, "fail to accept connection");

    client_id->context = conn;
    info("accept the connection");

    ret = conn->registerCompEvent(s->base_, &Server::onRecv);
    check(ret, "fail to register completion event");

    info("register the connection completion event");
    break;
  }
  case RDMA_CM_EVENT_ESTABLISHED: {
    info("establish a connection");
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

auto Server::onRecv([[gnu::unused]] int fd, [[gnu::unused]] short what,
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

  info("got a cq event");
  ibv_wc wc;
  conn->pollCq(&wc);
  if (wc.status != IBV_WC_SUCCESS) {
    info("meet an error wc, status: %d", wc.status);
    return;
  }
  reinterpret_cast<ConnCtx *>(wc.wr_id)->advance(wc.opcode);
}

Server::~Server() {
  ::event_base_free(base_);
  ::event_free(conn_event_);
  ::event_free(exit_event_);

  auto ret = ::rdma_destroy_id(cm_id_);
  warn(ret, "fail to destroy cm id");
  ::rdma_destroy_event_channel(ec_);

  info("cleanup the local resources");
}

} // namespace rdma