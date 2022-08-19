#include "server.h"
#include <signal.h>

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
  ret = ::rdma_listen(cm_id_, default_back_log);
  check(ret, "fail to listen for connections");

  info("bind address and begin listening for connection requests");

  ret = ::evthread_use_pthreads();
  check(ret, "fail to open multi-thread support for libevent");
  base_ = ::event_base_new();
  checkp(base_, "fail to create event loop base");
  conn_event_ = ::event_new(base_, ec_->fd, EV_READ | EV_PERSIST,
                            &Server::onConnEvent, this);
  checkp(conn_event_, "fail to create connection event");
  ret = ::event_add(conn_event_, nullptr);
  check(ret, "fail to register connection event");
  exit_event_ = ::event_new(base_, SIGINT, EV_SIGNAL, &Server::onExit, this);
  checkp(exit_event_, "fail to create exit event");
  ret = ::event_add(exit_event_, nullptr);
  check(ret, "fail to register exit event");
  info("register all events into event loop");
}

auto Server::run() -> int {
  info("event loop running");
  return ::event_base_dispatch(base_);
}

auto Server::handleConnEvent() -> void {
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

  switch (e->event) {
  case RDMA_CM_EVENT_CONNECT_REQUEST: { // throw when bad connection
    info("handle a connection request");

    auto ctx = new ConnWithCtx(client_id);

    auto ret = ::rdma_accept(client_id, &ctx->conn_->param_);
    check(ret, "fail to accept connection");

    ctx->conn_->remote_buffer_key_ = *(uint32_t *)e->param.conn.private_data;

    client_id->context = ctx;
    info("accept the connection");

#ifdef USE_NOTIFY
    ret = ctx->conn_->registerCompEvent(base_);
    check(ret, "fail to register completion event");
#endif

    info("register the connection completion event");
    break;
  }
  case RDMA_CM_EVENT_ESTABLISHED: {
    info("establish a connection");
    break;
  }
  case RDMA_CM_EVENT_DISCONNECTED: {
    delete reinterpret_cast<ConnWithCtx *>(client_id->context);
    info("close a connection");
    break;
  }
  default: {
    info("unexpected event: %s", ::rdma_event_str(e->event));
    break;
  }
  }

  auto ret = ::rdma_ack_cm_event(e);
  warn(ret, "fail to ack event");
}

auto Server::onConnEvent([[gnu::unused]] int fd, [[gnu::unused]] short what,
                         void *arg) -> void {
  reinterpret_cast<Server *>(arg)->handleConnEvent();
}

auto Server::onExit(int fd, short what, void *arg) -> void {
  Server *s = reinterpret_cast<Server *>(arg);
  auto ret = ::event_base_loopbreak(s->base_);
  check(ret, "fail to stop event loop");
  info("stop event loop");
}

Server::~Server() {
  ::event_base_free(base_);
  ::event_free(conn_event_);
  ::event_free(exit_event_);

  auto ret = ::rdma_destroy_id(cm_id_);
  warn(ret, "fail to destroy cm id");
  ::rdma_destroy_event_channel(ec_);

  ::freeaddrinfo(addr_);

  info("cleanup the local resources");
}

ServerSideCtx::ServerSideCtx(Conn *conn, void *buffer, uint32_t length)
    : ConnCtx(conn, buffer, length) {
  meta_mr_ = ::ibv_reg_mr(conn->pd_, &remote_meta_, sizeof(Meta),
                          IBV_ACCESS_LOCAL_WRITE);
  checkp(meta_mr_, "fail to register remote meta receiver");
}

ServerSideCtx::~ServerSideCtx() {
  check(::ibv_dereg_mr(meta_mr_), "fail to deregister remote meta receiver");
}

auto ServerSideCtx::prepare() -> void {
  state_ = WaitingForBufferMeta;
  conn_->postRecv(this, meta_mr_->addr, meta_mr_->length, meta_mr_->lkey);
}

auto ServerSideCtx::advance(int32_t finished_op) -> void {
  switch (finished_op) {
  case IBV_WC_RECV: {
    assert(state_ == WaitingForBufferMeta);
    info("remote memory region: address: %p, length: %d", remote_meta_.addr_,
         remote_meta_.length_);
    state_ = ReadingRequest;
    assert(buffer_ != nullptr);
    conn_->postRead(this, buffer_, length_, conn_->buffer_mr_->lkey,
                    remote_meta_.addr_, conn_->remote_buffer_key_);
    break;
  }
  case IBV_WC_RDMA_READ: {
    assert(state_ == ReadingRequest);
    info("read from remote side, request content: %s", buffer_);
    state_ = FilledWithRequest;

    {
      // TODO: make this a self-defined handler
      char src[16];
      ::snprintf(src, 16, "%s-done", (char *)buffer_);
      ::memcpy(buffer_, src, 16);
      state_ = FilledWithResponse;
    }

    assert(state_ == FilledWithResponse);
    info("write to remote side, response content: %s", buffer_);
    state_ = WritingResponse;
    conn_->postWriteImm(this, buffer_, length_, conn_->buffer_mr_->lkey,
                        remote_meta_.addr_, conn_->remote_buffer_key_);
    break;
  }
  case IBV_WC_RDMA_WRITE: {
    assert(state_ == WritingResponse);
    info("write done, wait for next request");
    prepare();
    break;
  }
  default: {
    info("unexpected wc opcode: %d", finished_op);
    break;
  }
  }
}

ConnWithCtx::ConnWithCtx(rdma_cm_id *id) {
  conn_ = new Conn(id, max_context_num);
  for (uint32_t i = 0; i < max_context_num; i++) {
    ctx_[i] =
        new ServerSideCtx(conn_, conn_->bufferPage(i), Conn::buffer_page_size);
    ctx_[i]->prepare();
  }
}

ConnWithCtx::~ConnWithCtx() {
  for (auto p : ctx_) {
    delete p;
  }
  delete conn_;
}

} // namespace rdma