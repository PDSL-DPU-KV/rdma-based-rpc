#ifndef _KV_APP_H_
#define _KV_APP_H_

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

typedef void (*kv_app_func)(void *ctx);
typedef int (*kv_app_poller_func)(void *ctx);

#define MAX_TASKS_NUM 63

struct kv_app_t {
  uint32_t task_num;
  uint64_t running_thread;
};

struct kv_app_task {
  kv_app_func func;
  void *arg;
};

const struct kv_app_t *kv_app(void);

int kv_app_start(const char *json_config_file, uint32_t task_num,
                 struct kv_app_task *tasks);

static inline int kv_app_start_single_task(const char *json_config_file,
                                           kv_app_func func, void *arg) {
  struct kv_app_task task = {func, arg};
  return kv_app_start(json_config_file, 1, &task);
}

void kv_app_stop(int rc);

void kv_app_send(uint32_t index, kv_app_func func, void *arg);

void kv_app_send_without_token(uint32_t index, kv_app_func func, void *arg);

void kv_app_send_msg(uint32_t index, kv_app_func func, void *arg);

uint32_t kv_app_get_thread_index(void);

void *kv_app_poller_register(kv_app_poller_func func, void *arg,
                             uint64_t period_microseconds);

void kv_app_poller_register_on(uint32_t index, kv_app_poller_func func,
                               void *arg, uint64_t period_microseconds,
                               void **poller);

void kv_app_poller_unregister(void **poller);

#endif
