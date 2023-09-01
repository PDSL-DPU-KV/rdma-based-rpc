#include "kv_rdma.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/queue.h>
#include <unistd.h>

#include "kv_app.h"
#include "kv_memory.h"
#include "uthash.h"

#define TIMEOUT_IN_MS (500U)
#define MAX_Q_NUM (4096U)

#define TEST_NZ(x)                                                             \
  do {                                                                         \
    int rc;                                                                    \
    if (rc = (x)) {                                                            \
      fprintf(stderr, "error: " #x " failed with rc = %d.\n", rc);             \
      exit(-1);                                                                \
    }                                                                          \
  } while (0)
#define TEST_Z(x) TEST_NZ(!(x))

struct req_header {
  uint64_t resp_addr;
  uint32_t req_id;
#define HEADER_SIZE (sizeof(struct req_header))
} __attribute__((packed));

struct rdma_connection {
  struct kv_rdma *self;
  struct rdma_cm_id *cm_id;
  struct ibv_qp *qp;
  bool is_server;
  union {
    // server connection data
    struct {
      kv_rdma_req_handler handler;
      void *arg;
      UT_hash_handle hh;
    } s;
    // client connection data
    struct {
      kv_rdma_connect_cb connect;
      void *connect_arg;
      kv_rdma_disconnect_cb disconnect;
      void *disconnect_arg;
      struct kv_mempool *mp;
    } c;
  } u;
};
struct cq_poller_ctx {
  struct kv_rdma *self;
  struct ibv_cq *cq;
  void *poller;
};
struct fini_ctx_t {
  uint32_t thread_id;
  uint32_t io_cnt;
  kv_app_func cb;
  void *cb_arg;
};
struct kv_rdma {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct rdma_event_channel *ec;
  void *cm_poller;
  bool has_server;
  uint32_t thread_num;
  uint32_t thread_id;
  struct cq_poller_ctx *cq_pollers;
  // client data
  uint32_t conn_id;
  // server data
  struct ibv_srq *srq;
  uint32_t con_req_num;
  uint32_t max_msg_sz;
  pthread_rwlock_t lock;
  struct mr_bulk *mrs;
  struct server_req_ctx *requests;
  struct rdma_connection *connections;
  kv_rdma_server_init_cb init_cb;
  void *init_cb_arg;
  // finish ctx
  struct fini_ctx_t fini_ctx;
};
struct client_req_ctx {
  struct rdma_connection *conn;
  kv_rdma_req_cb cb;
  void *cb_arg;
  struct ibv_mr *req, *resp;
};
struct server_req_ctx {
  struct rdma_connection *conn;
  struct kv_rdma *self;
  uint32_t resp_rkey;
  struct ibv_mr *mr;
  struct req_header header;
};

// --- alloc and free ---
struct mr_bulk {
  struct ibv_mr *mr;
  uint8_t *buf;
  struct ibv_mr *mrs;
};
kv_rdma_mrs_handle kv_rdma_alloc_bulk(kv_rdma_handle h,
                                      enum kv_rdma_mr_type type, size_t size,
                                      size_t count) {
  struct kv_rdma *self = h;
  struct mr_bulk *mr_h = kv_malloc(sizeof(struct mr_bulk));
  if (type != KV_RDMA_MR_RESP)
    size += HEADER_SIZE;
  mr_h->buf = kv_dma_malloc(size * count);
  mr_h->mr = ibv_reg_mr(self->pd, mr_h->buf, size * count,
                        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  mr_h->mrs = kv_calloc(count, sizeof(struct ibv_mr));
  for (size_t i = 0; i < count; i++) {
    mr_h->mrs[i] = *mr_h->mr;
    mr_h->mrs[i].addr = (char *)mr_h->mr->addr + i * size;
    mr_h->mrs[i].length = size;
  }
  return mr_h;
}
kv_rdma_mr kv_rdma_mrs_get(kv_rdma_mrs_handle h, size_t index) {
  return ((struct mr_bulk *)h)->mrs + index;
}
void kv_rdma_free_bulk(kv_rdma_mrs_handle h) {
  struct mr_bulk *mr_h = h;
  ibv_dereg_mr(mr_h->mr);
  kv_dma_free(mr_h->buf);
  kv_free(mr_h->mrs);
  kv_free(mr_h);
}

kv_rdma_mr kv_rdma_alloc_req(kv_rdma_handle h, uint32_t size) {
  struct kv_rdma *self = h;
  size += HEADER_SIZE;
  uint8_t *buf = kv_dma_malloc(size);
  return ibv_reg_mr(self->pd, buf, size, 0);
}

uint8_t *kv_rdma_get_req_buf(kv_rdma_mr mr) {
  return (uint8_t *)((struct ibv_mr *)mr)->addr + HEADER_SIZE;
}

kv_rdma_mr kv_rdma_alloc_resp(kv_rdma_handle h, uint32_t size) {
  struct kv_rdma *self = h;
  uint8_t *buf = kv_dma_malloc(size);
  return ibv_reg_mr(self->pd, buf, size,
                    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
}

uint8_t *kv_rdma_get_resp_buf(kv_rdma_mr mr) {
  return (uint8_t *)((struct ibv_mr *)mr)->addr;
}

void kv_rdma_free_mr(kv_rdma_mr h) {
  struct ibv_mr *mr = h;
  uint8_t *buf = mr->addr;
  ibv_dereg_mr(mr);
  kv_dma_free(buf);
}

// --- cm_poller ---
static int rdma_cq_poller(void *arg);
static void server_data_init(struct kv_rdma *self) {
  struct ibv_srq_init_attr srq_init_attr;
  memset(&srq_init_attr, 0, sizeof(srq_init_attr));
  srq_init_attr.attr.max_wr = MAX_Q_NUM;
  srq_init_attr.attr.max_sge = 1;
  TEST_Z(self->srq = ibv_create_srq(self->pd, &srq_init_attr));

  self->requests = kv_calloc(self->con_req_num, sizeof(struct server_req_ctx));
  self->mrs = kv_rdma_alloc_bulk(self, KV_RDMA_MR_SERVER, self->max_msg_sz,
                                 self->con_req_num);
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge = {0, self->mrs->mr->length, self->mrs->mr->lkey};
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  for (size_t i = 0; i < self->con_req_num; i++) {
    self->requests[i].self = self;
    self->requests[i].mr = kv_rdma_mrs_get(self->mrs, i);
    sge.addr = (uint64_t)self->requests[i].mr->addr;
    wr.wr_id = (uint64_t)(self->requests + i);
    TEST_NZ(ibv_post_srq_recv(self->srq, &wr, &bad_wr));
  }
  if (self->init_cb)
    self->init_cb(self->init_cb_arg);
}

static int create_connetion(struct kv_rdma *self, struct rdma_cm_id *cm_id) {
  struct rdma_connection *conn = cm_id->context;
  // --- build context ---
  if (self->ctx == NULL) {
    self->ctx = cm_id->verbs;
    TEST_Z(self->pd = ibv_alloc_pd(self->ctx));
    TEST_Z(self->cq = ibv_create_cq(self->ctx, 3 * MAX_Q_NUM /* max_conn_num */,
                                    NULL, NULL, 0));
    self->cq_pollers =
        kv_calloc(self->thread_num, sizeof(struct cq_poller_ctx));
    for (size_t i = 0; i < self->thread_num; i++) {
      self->cq_pollers[i] =
          (struct cq_poller_ctx){self, self->cq, .poller = NULL};
      kv_app_poller_register_on(self->thread_id + i, rdma_cq_poller,
                                self->cq_pollers + i, 0,
                                &self->cq_pollers[i].poller);
    }
  }
  // assume only have one context
  assert(self->ctx == cm_id->verbs);
  if (self->has_server && self->requests == NULL)
    server_data_init(self);
  // --- build qp ---
  struct ibv_qp_init_attr qp_attr;
  memset(&qp_attr, 0, sizeof(struct ibv_qp_init_attr));
  qp_attr.send_cq = self->cq;
  qp_attr.recv_cq = self->cq;
  qp_attr.qp_type = IBV_QPT_RC;
  if (conn->is_server)
    qp_attr.srq = self->srq;

  qp_attr.cap.max_send_wr = MAX_Q_NUM;
  qp_attr.cap.max_recv_wr = MAX_Q_NUM;
  qp_attr.cap.max_send_sge = 1;
  qp_attr.cap.max_recv_sge = 1;
  TEST_NZ(rdma_create_qp(cm_id, self->pd, &qp_attr));
  conn->qp = cm_id->qp;
  return 0;
}

static inline int on_addr_resolved(struct kv_rdma *self,
                                   struct rdma_cm_id *cm_id) {
  TEST_NZ(create_connetion(self, cm_id));
  TEST_NZ(rdma_resolve_route(cm_id, TIMEOUT_IN_MS));
  return 0;
}

static inline int
on_route_resolved(__attribute__((unused)) struct kv_rdma *self,
                  struct rdma_cm_id *cm_id) {
  struct rdma_conn_param cm_params;
  memset(&cm_params, 0, sizeof(cm_params));
  TEST_NZ(rdma_connect(cm_id, &cm_params));
  return 0;
}

static inline int on_connect_request(struct kv_rdma *self,
                                     struct rdma_cm_id *cm_id) {
  struct rdma_connection *conn = kv_malloc(sizeof(struct rdma_connection)),
                         *lconn = cm_id->context;
  *conn = (struct rdma_connection){self, cm_id, NULL, true};
  conn->u.s.handler = lconn->u.s.handler;
  conn->u.s.arg = lconn->u.s.arg;
  cm_id->context = conn;
  TEST_NZ(create_connetion(self, cm_id));
  pthread_rwlock_wrlock(&self->lock);
  HASH_ADD(u.s.hh, self->connections, qp->qp_num, sizeof(uint32_t), conn);
  pthread_rwlock_unlock(&self->lock);
  struct rdma_conn_param cm_params;
  memset(&cm_params, 0, sizeof(cm_params));
  TEST_NZ(rdma_accept(cm_id, &cm_params));
  return 0;
}
static inline int on_connect_error(__attribute__((unused)) struct kv_rdma *self,
                                   struct rdma_cm_id *cm_id) {
  struct rdma_connection *conn = cm_id->context;
  if (!conn->is_server) {
    if (conn->u.c.connect)
      conn->u.c.connect(NULL, conn->u.c.connect_arg);
    kv_free(conn);
  }
  return 0;
}
static inline int on_established(__attribute__((unused)) struct kv_rdma *self,
                                 struct rdma_cm_id *cm_id) {
  struct rdma_connection *conn = cm_id->context;
  if (!conn->is_server && conn->u.c.connect) {
    conn->u.c.connect(conn, conn->u.c.connect_arg);
  }
  if (conn->is_server) {
    struct sockaddr_in *addr = (struct sockaddr_in *)rdma_get_peer_addr(cm_id);
    printf("server: received connection from peer %s:%u.\n",
           inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
  }
  return 0;
}
static inline int on_disconnect(struct rdma_cm_id *cm_id) {
  struct rdma_connection *conn = cm_id->context;
  if (conn->is_server) {
    struct sockaddr_in *addr = (struct sockaddr_in *)rdma_get_peer_addr(cm_id);
    printf("server: peer %s:%u disconnected.\n", inet_ntoa(addr->sin_addr),
           ntohs(addr->sin_port));
    pthread_rwlock_wrlock(&conn->self->lock);
    HASH_DELETE(u.s.hh, conn->self->connections, conn);
    pthread_rwlock_unlock(&conn->self->lock);
  } else {
    kv_mempool_free(conn->u.c.mp);
  }
  rdma_destroy_qp(cm_id);
  rdma_destroy_id(cm_id);
  if (!conn->is_server && conn->u.c.disconnect)
    conn->u.c.disconnect(conn->u.c.disconnect_arg);
  kv_free(conn);
  return 0;
}

static int rdma_cm_poller(void *_self) {
  struct kv_rdma *self = _self;
  struct rdma_cm_event *event = NULL;
  while (self->ec && rdma_get_cm_event(self->ec, &event) == 0) {
    struct rdma_cm_id *cm_id = event->id;
    enum rdma_cm_event_type event_type = event->event;
    rdma_ack_cm_event(event);
    switch (event_type) {
    case RDMA_CM_EVENT_ADDR_RESOLVED:
      on_addr_resolved(self, cm_id);
      break;
    case RDMA_CM_EVENT_ROUTE_RESOLVED:
      on_route_resolved(self, cm_id);
      break;
    case RDMA_CM_EVENT_UNREACHABLE:
    case RDMA_CM_EVENT_REJECTED:
      on_connect_error(self, cm_id);
      break;
    case RDMA_CM_EVENT_CONNECT_REQUEST:
      on_connect_request(self, cm_id);
      break;
    case RDMA_CM_EVENT_ESTABLISHED:
      on_established(self, cm_id);
      break;
    case RDMA_CM_EVENT_DISCONNECTED:
      on_disconnect(cm_id);
      break;
    default:
      break;
    }
  }
  return 0;
}

// --- client ---

void kv_rdma_connect(kv_rdma_handle h, char *addr_str, char *port_str,
                     kv_rdma_connect_cb connect_cb, void *connect_arg,
                     kv_rdma_disconnect_cb disconnect_cb,
                     void *disconnect_arg) {
  struct kv_rdma *self = h;
  struct rdma_connection *conn = kv_malloc(sizeof(struct rdma_connection));
  *conn = (struct rdma_connection){self, NULL, NULL, false};
  conn->u.c.connect = connect_cb;
  conn->u.c.connect_arg = connect_arg;
  conn->u.c.disconnect = disconnect_cb;
  conn->u.c.disconnect_arg = disconnect_arg;
  conn->u.c.mp = kv_mempool_create(8191, sizeof(struct client_req_ctx));
  struct addrinfo *addr;
  TEST_NZ(getaddrinfo(addr_str, port_str, NULL, &addr));
  TEST_NZ(rdma_create_id(self->ec, &conn->cm_id, NULL, RDMA_PS_TCP));
  conn->cm_id->context = conn;
  TEST_NZ(rdma_resolve_addr(conn->cm_id, NULL, addr->ai_addr, TIMEOUT_IN_MS));
  freeaddrinfo(addr);
}

void kv_rdma_send_req(connection_handle h, kv_rdma_mr req, uint32_t req_sz,
                      kv_rdma_mr resp, void *resp_addr, kv_rdma_req_cb cb,
                      void *cb_arg) {
  struct rdma_connection *conn = h;
  assert(conn->is_server == false);
  struct client_req_ctx *ctx = kv_mempool_get(conn->u.c.mp);
  *ctx = (struct client_req_ctx){conn, cb, cb_arg, req, resp};
  assert(req_sz <= ctx->req->length);
  if (resp_addr == NULL)
    resp_addr = ctx->resp->addr;
  *(struct req_header *)ctx->req->addr = (struct req_header){
      (uint64_t)resp_addr, (uint32_t)kv_mempool_get_id(conn->u.c.mp, ctx)};
  struct ibv_recv_wr r_wr = {(uintptr_t)conn, NULL, NULL, 0}, *r_bad_wr = NULL;
  if (ibv_post_recv(conn->qp, &r_wr, &r_bad_wr)) {
    goto fail;
  }
  struct ibv_sge sge = {(uintptr_t)ctx->req->addr, req_sz + HEADER_SIZE,
                        ctx->req->lkey};
  struct ibv_send_wr s_wr, *s_bad_wr = NULL;
  memset(&s_wr, 0, sizeof(s_wr));
  s_wr.wr_id = (uintptr_t)ctx;
  s_wr.opcode = IBV_WR_SEND_WITH_IMM;
  s_wr.imm_data = ctx->resp->rkey;
  s_wr.sg_list = &sge;
  s_wr.num_sge = 1;
  s_wr.send_flags = IBV_SEND_SIGNALED;
  if (ibv_post_send(conn->qp, &s_wr, &s_bad_wr)) {
    goto fail;
  }
  return;
fail:
  if (ctx->cb)
    ctx->cb(h, false, req, resp, ctx->cb_arg);
  kv_free(ctx);
}

void kv_rdma_disconnect(connection_handle h) {
  struct rdma_connection *conn = h;
  TEST_NZ(rdma_disconnect(conn->cm_id));
}

// --- server ---
void kv_rdma_listen(kv_rdma_handle h, char *addr_str, char *port_str,
                    uint32_t con_req_num, uint32_t max_msg_sz,
                    kv_rdma_req_handler handler, void *arg,
                    kv_rdma_server_init_cb cb, void *cb_arg) {
  struct kv_rdma *self = h;
  self->has_server = true;
  self->init_cb = cb;
  self->init_cb_arg = cb_arg;
  struct rdma_connection *conn = kv_malloc(sizeof(struct rdma_connection));
  *conn = (struct rdma_connection){self, NULL, NULL, true};
  conn->u.s.handler = handler;
  conn->u.s.arg = arg;
  struct addrinfo *addr;
  TEST_NZ(getaddrinfo(addr_str, port_str, NULL, &addr));
  TEST_NZ(rdma_create_id(self->ec, &conn->cm_id, NULL, RDMA_PS_TCP));
  TEST_NZ(rdma_bind_addr(conn->cm_id, addr->ai_addr));
  TEST_NZ(rdma_listen(conn->cm_id, 10));
  /* backlog=10 is arbitrary  TODO:conn_num*/
  conn->cm_id->context = conn;
  freeaddrinfo(addr);
  self->con_req_num = con_req_num;
  self->max_msg_sz = max_msg_sz;
  pthread_rwlock_init(&self->lock, NULL);
  printf("kv rdma listening on %s %s.\n", addr_str, port_str);
}

void kv_rdma_make_resp(void *req_h, uint8_t *resp, uint32_t resp_sz) {
  struct server_req_ctx *ctx = req_h;
  struct ibv_sge sge = {(uintptr_t)resp, resp_sz, ctx->mr->lkey};
  struct ibv_send_wr wr, *bad_wr = NULL;
  wr.wr_id = (uintptr_t)ctx;
  wr.next = NULL;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.imm_data = ctx->header.req_id;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr = ctx->header.resp_addr;
  wr.wr.rdma.rkey = ctx->resp_rkey;
  TEST_NZ(ibv_post_send(ctx->conn->qp, &wr, &bad_wr));
}

uint32_t kv_rdma_conn_num(kv_rdma_handle h) {
  struct kv_rdma *self = h;
  uint32_t num;
  pthread_rwlock_rdlock(&self->lock);
  num = HASH_CNT(u.s.hh, self->connections);
  pthread_rwlock_unlock(&self->lock);
  return num;
}

// --- cq_poller ---
static inline void on_write_resp_done(struct ibv_wc *wc) {
  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr, "on_write_resp_done: status is %d\n", wc->status);
  }
  struct server_req_ctx *ctx = (struct server_req_ctx *)wc->wr_id;
  assert(ctx->conn->is_server);
  struct ibv_sge sge = {(uint64_t)ctx->mr->addr, ctx->mr->length,
                        ctx->mr->lkey};
  struct ibv_recv_wr wr = {(uint64_t)ctx, NULL, &sge, 1}, *bad_wr = NULL;
  TEST_NZ(ibv_post_srq_recv(ctx->self->srq, &wr, &bad_wr));
}

static inline void on_recv_req(struct ibv_wc *wc) {
  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr, "on_recv_req: status is %d\n", wc->status);
    wc->status = IBV_WC_SUCCESS;
    on_write_resp_done(wc);
    return;
  }
  struct server_req_ctx *ctx = (struct server_req_ctx *)wc->wr_id;
  assert(wc->byte_len > HEADER_SIZE);
  assert(wc->wc_flags & IBV_WC_WITH_IMM);
  pthread_rwlock_rdlock(&ctx->self->lock);
  HASH_FIND(u.s.hh, ctx->self->connections, &wc->qp_num, sizeof(uint32_t),
            ctx->conn);
  pthread_rwlock_unlock(&ctx->self->lock);
  assert(ctx->conn);
  assert(ctx->conn->is_server);
  ctx->resp_rkey = wc->imm_data;
  ctx->header = *(struct req_header *)ctx->mr->addr;
  ctx->conn->u.s.handler(ctx, ctx->mr, wc->byte_len - HEADER_SIZE,
                         ctx->conn->u.s.arg);
}

static inline void on_recv_resp(struct ibv_wc *wc) {
  struct rdma_connection *conn = (struct rdma_connection *)wc->wr_id;
  assert(!conn->is_server);
  assert(wc->wc_flags & IBV_WC_WITH_IMM);
  // using wc->imm_data(req_id) to find corresponding request_ctx
  struct client_req_ctx *ctx =
      kv_mempool_get_ele(conn->u.c.mp, (int32_t)wc->imm_data);
  ctx->cb(ctx->conn, wc->status == IBV_WC_SUCCESS, ctx->req, ctx->resp,
          ctx->cb_arg);
  kv_mempool_put(conn->u.c.mp, ctx);
}

static inline void on_send_req(struct ibv_wc *wc) {
  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr, "on_send_req: status is %d\n", wc->status);
  }
}

#define MAX_ENTRIES_PER_POLL 128
static int rdma_cq_poller(void *arg) {
  struct cq_poller_ctx *ctx = arg;
  struct ibv_wc wc[MAX_ENTRIES_PER_POLL];
  while (ctx->cq) {
    int rc = ibv_poll_cq(ctx->cq, MAX_ENTRIES_PER_POLL, wc);
    if (rc <= 0)
      return rc;
    for (int i = 0; i < rc; i++) {
      switch (wc[i].opcode) {
      case IBV_WC_RECV:
        on_recv_req(wc + i);
        break;
      case IBV_WC_RECV_RDMA_WITH_IMM:
        on_recv_resp(wc + i);
        break;
      case IBV_WC_RDMA_WRITE:
        on_write_resp_done(wc + i);
        break;
      case IBV_WC_SEND:
        on_send_req(wc + i);
        break;
      default:
        fprintf(stderr, "kv_rdma: unknown event %u \n.", wc[i].opcode);
        break;
      }
    }
  }
  return 0;
}

// --- init & fini ---
void kv_rdma_init(kv_rdma_handle *h, uint32_t thread_num) {
  struct kv_rdma *self = kv_malloc(sizeof(struct kv_rdma));
  kv_memset(self, 0, sizeof(struct kv_rdma));
  self->ec = rdma_create_event_channel();
  if (!self->ec) {
    fprintf(stderr, "fail to create event channel.\n");
    exit(-1);
  }
  int flag = fcntl(self->ec->fd, F_GETFL);
  fcntl(self->ec->fd, F_SETFL, flag | O_NONBLOCK);
  self->cm_poller = kv_app_poller_register(rdma_cm_poller, self, 1000);
  self->thread_num = thread_num;
  self->thread_id = kv_app_get_thread_index();
  *h = self;
}

static void poller_unregister_done(void *arg) {
  struct kv_rdma *self = arg;
  if (--self->fini_ctx.io_cnt)
    return;
  if (self->ctx) {
    ibv_destroy_cq(self->cq);
    ibv_dealloc_pd(self->pd);
    kv_free(self->cq_pollers);
    if (self->requests) {
      ibv_destroy_srq(self->srq);
      kv_rdma_free_bulk(self->mrs);
      kv_free(self->requests);
    }
  }
  kv_app_send(self->fini_ctx.thread_id, self->fini_ctx.cb,
              self->fini_ctx.cb_arg);
  kv_free(self);
}

static void cq_poller_unregister(void *arg) {
  struct cq_poller_ctx *ctx = arg;
  ctx->cq = NULL;
  kv_app_poller_unregister(&ctx->poller);
  kv_app_send(ctx->self->thread_id, poller_unregister_done, ctx->self);
}

static void cm_poller_unregister(void *arg) {
  struct kv_rdma *self = arg;
  rdma_destroy_event_channel(self->ec);
  self->ec = NULL;
  kv_app_poller_unregister(&self->cm_poller);
  kv_app_send(self->thread_id, poller_unregister_done, self);
}

void kv_rdma_fini(kv_rdma_handle h, kv_rdma_fini_cb cb, void *cb_arg) {
  struct kv_rdma *self = h;
  self->fini_ctx =
      (struct fini_ctx_t){kv_app_get_thread_index(), 1, cb, cb_arg};
  kv_app_send(self->thread_id, cm_poller_unregister, self);
  if (self->ctx) {
    self->fini_ctx.io_cnt += self->thread_num;
    for (size_t i = 0; i < self->thread_num; i++) {
      kv_app_send(self->thread_id + i, cq_poller_unregister,
                  self->cq_pollers + i);
    }
  }
}
