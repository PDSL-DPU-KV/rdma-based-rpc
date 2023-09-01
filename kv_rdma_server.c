#include "kv_rdma.h"

#include <signal.h>
#include <stdio.h>

#include "kv_app.h"

kv_rdma_handle rdma;

void app_stop(void *arg) { kv_app_stop(0); }

void rdma_stop(int s) {
  printf("trigger %d\n", s);
  kv_rdma_fini(rdma, app_stop, NULL);
}

void handler(void *req_h, kv_rdma_mr req, uint32_t req_sz, void *arg) {
  kv_rdma_make_resp(req_h, kv_rdma_get_req_buf(req), 5);
}

void rdma_start(void *arg) {
  kv_rdma_init(&rdma, 1);
  kv_rdma_listen(rdma, "192.168.200.89", "9000", 32, 8192, handler, NULL, NULL,
                 NULL);
}

int main(int argc, char **argv) {
  kv_app_start_single_task(argv[1], rdma_start, NULL);
  return 0;
}
