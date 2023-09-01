#include "kv_rdma.h"

#include <stdio.h>
#include <string.h>

#include "kv_app.h"

void stop_all(void *arg) { kv_app_stop(0); }

void echo_cb(connection_handle h, bool success, kv_rdma_mr req, kv_rdma_mr resp,
             void *arg) {
  if (success) {
    printf("client issue: %s\n", (const char *)(kv_rdma_get_req_buf(req)));
    printf("server echo: %s\n", (const char *)(kv_rdma_get_resp_buf(resp)));
  }
  kv_rdma_disconnect(h);
  kv_rdma_fini(arg, stop_all, NULL);
}

void issue_echo(connection_handle h, void *arg) {
  kv_rdma_handle *rdma = arg;
  kv_rdma_mr req = kv_rdma_alloc_req(rdma, 5);
  memcpy(kv_rdma_get_req_buf(req), "hello", 5);
  kv_rdma_mr resp = kv_rdma_alloc_resp(rdma, 5);
  kv_rdma_send_req(h, req, 5, resp, kv_rdma_get_resp_buf(resp), echo_cb, arg);
}

void rdma_exit(void *arg) { printf("bye!\n"); }

void rdma_start(void *arg) {
  kv_rdma_handle rdma;
  kv_rdma_init(&rdma, 1);
  kv_rdma_connect(rdma, "192.168.200.89", "9000", issue_echo, rdma, rdma_exit,
                  rdma);
}

int main(int argc, char **argv) {
  kv_app_start_single_task(argv[1], rdma_start, NULL);
  return 0;
}
