#ifndef _KV_RDMA_H_
#define _KV_RDMA_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *connection_handle;
typedef void *kv_rdma_handle;
typedef void *kv_rdma_mr;
typedef void *kv_rdma_mrs_handle;

typedef void (*kv_rdma_req_cb)(connection_handle h, bool success,
                               kv_rdma_mr req, kv_rdma_mr resp, void *cb_arg);
typedef void (*kv_rdma_connect_cb)(connection_handle h, void *cb_arg);
typedef void (*kv_rdma_disconnect_cb)(void *cb_arg);
typedef void (*kv_rdma_req_handler)(void *req_h, kv_rdma_mr req,
                                    uint32_t req_sz, void *arg);
typedef void (*kv_rdma_fini_cb)(void *ctx);
typedef void (*kv_rdma_server_init_cb)(void *arg);

void kv_rdma_init(kv_rdma_handle *h, uint32_t thread_num);

void kv_rdma_fini(kv_rdma_handle h, kv_rdma_fini_cb cb, void *cb_arg);

enum kv_rdma_mr_type { KV_RDMA_MR_REQ, KV_RDMA_MR_RESP, KV_RDMA_MR_SERVER };
kv_rdma_mrs_handle kv_rdma_alloc_bulk(kv_rdma_handle h,
                                      enum kv_rdma_mr_type type, size_t size,
                                      size_t count);
kv_rdma_mr kv_rdma_mrs_get(kv_rdma_mrs_handle h, size_t index);
void kv_rdma_free_bulk(kv_rdma_mrs_handle h);

kv_rdma_mr kv_rdma_alloc_req(kv_rdma_handle h, uint32_t size);
uint8_t *kv_rdma_get_req_buf(kv_rdma_mr mr);
kv_rdma_mr kv_rdma_alloc_resp(kv_rdma_handle h, uint32_t size);
uint8_t *kv_rdma_get_resp_buf(kv_rdma_mr mr);
void kv_rdma_free_mr(kv_rdma_mr h);

void kv_rdma_listen(kv_rdma_handle h, char *addr_str, char *port_str,
                    uint32_t con_req_num, uint32_t max_msg_sz,
                    kv_rdma_req_handler handler, void *arg,
                    kv_rdma_server_init_cb cb, void *cb_arg);
void kv_rdma_make_resp(void *req_h, uint8_t *resp,
                       uint32_t resp_sz); // resp must within buf
uint32_t kv_rdma_conn_num(kv_rdma_handle h);

void kv_rdma_connect(kv_rdma_handle h, char *addr_str, char *port_str,
                     kv_rdma_connect_cb connect_cb, void *connect_arg,
                     kv_rdma_disconnect_cb disconnect_cb, void *disconnect_arg);
void kv_rdma_send_req(connection_handle h, kv_rdma_mr req, uint32_t req_sz,
                      kv_rdma_mr resp, void *resp_addr, kv_rdma_req_cb cb,
                      void *cb_arg);
void kv_rdma_disconnect(connection_handle h);
#endif