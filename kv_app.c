#include "kv_app.h"

#include <pthread.h>
#include <stdio.h>

#include "concurrentqueue.h"
#include "kv_memory.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/thread.h"

pthread_mutex_t g_lock;

static struct kv_app_t g_app;

static struct spdk_mempool *g_event_mempool;

static struct thread_data {
  struct spdk_thread *thread;
  MoodycamelCQHandle cq;
  MoodycamelToken c_token;
  MoodycamelToken p_tokens[MAX_TASKS_NUM];
  struct spdk_poller *poller;
  uint32_t index;
} g_threads[MAX_TASKS_NUM];

static __thread struct thread_data *app_thread = NULL;

const struct kv_app_t *kv_app(void) { return &g_app; }

void kv_app_send_msg(uint32_t index, kv_app_func func, void *arg) {
  spdk_thread_send_msg(g_threads[index].thread, func, arg);
}

uint32_t kv_app_get_thread_index(void) {
  assert(app_thread);
  return app_thread->index;
}

void kv_app_send(uint32_t index, kv_app_func func, void *arg) {
  uint32_t local_index = kv_app_get_thread_index();
  struct kv_app_task *msg = spdk_mempool_get(g_event_mempool);
  *msg = (struct kv_app_task){func, arg};
  moodycamel_cq_enqueue_with_token(g_threads[index].cq,
                                   g_threads[index].p_tokens[local_index], msg);
}

void kv_app_send_without_token(uint32_t index, kv_app_func func, void *arg) {
  struct kv_app_task *msg = spdk_mempool_get(g_event_mempool);
  *msg = (struct kv_app_task){func, arg};
  moodycamel_cq_enqueue(g_threads[index].cq, msg);
}

#define MAX_POLL_SZ 128

static __thread struct kv_app_task *msg_buf[MAX_POLL_SZ];

static int msg_poller(void *arg) {
  struct thread_data *data = arg;
  size_t size = moodycamel_cq_try_dequeue_bulk_with_token(
      data->cq, data->c_token, (MoodycamelValue *)msg_buf, MAX_POLL_SZ);
  for (size_t i = 0; i < size; i++) {
    if (msg_buf[i]->func) {
      msg_buf[i]->func(msg_buf[i]->arg);
    }
    spdk_mempool_put(g_event_mempool, msg_buf[i]);
  }
  return SPDK_POLLER_IDLE;
}

static void app_start(void *_tasks) {
  struct kv_app_task *tasks = _tasks;
  for (uint32_t i = 0; i < g_app.task_num; ++i) {
    if (tasks[i].func != NULL) {
      kv_app_send(i, tasks[i].func, tasks[i].arg);
    }
  }
}

static inline void thread_init(uint32_t index) {
  g_threads[index].thread = spdk_get_thread();
  g_threads[index].index = index;
  moodycamel_cq_create(&g_threads[index].cq);
  for (size_t i = 0; i < g_app.task_num; i++)
    moodycamel_prod_token(g_threads[index].cq, &g_threads[index].p_tokens[i]);
  moodycamel_cons_token(g_threads[index].cq, &g_threads[index].c_token);
  g_threads[index].poller =
      spdk_poller_register(msg_poller, g_threads + index, 0);
  app_thread = g_threads + index;
}

static void register_func(void *arg) {
  struct spdk_thread *thread = spdk_get_thread();
  uint32_t index;
  if (sscanf(spdk_thread_get_name(thread), "reactor_%u", &index) == 1) {
    thread_init(index);
  }
}

static void send_msg_to_all(void *arg) {
  char mempool_name[64];
  snprintf(mempool_name, sizeof(mempool_name), "kv_app_evtpool_%d", getpid());
  g_event_mempool = spdk_mempool_create(
      mempool_name,
      262144 - 1, /* Power of 2 minus 1 is optimal for memory consumption */
      sizeof(struct kv_app_task), SPDK_MEMPOOL_DEFAULT_CACHE_SIZE,
      SPDK_ENV_SOCKET_ID_ANY);
  thread_init(0);
  spdk_for_each_thread(register_func, arg, app_start);
}

void *kv_app_poller_register(kv_app_poller_func func, void *arg,
                             uint64_t period_microseconds) {
  return spdk_poller_register(func, arg, period_microseconds);
}

struct poller_register_ctx {
  kv_app_poller_func func;
  void *arg;
  uint64_t period_microseconds;
  void **poller;
};

static void poller_register(void *arg) {
  struct poller_register_ctx *ctx = arg;
  *ctx->poller =
      spdk_poller_register(ctx->func, ctx->arg, ctx->period_microseconds);
  kv_free(arg);
}

void kv_app_poller_register_on(uint32_t index, kv_app_poller_func func,
                               void *arg, uint64_t period_microseconds,
                               void **poller) {
  struct poller_register_ctx *ctx = kv_malloc(sizeof(*ctx));
  *ctx = (struct poller_register_ctx){func, arg, period_microseconds, poller};
  kv_app_send(index, poller_register, ctx);
}

void kv_app_poller_unregister(void **poller) {
  spdk_poller_unregister((struct spdk_poller **)poller);
}

int kv_app_start(const char *json_config_file, uint32_t task_num,
                 struct kv_app_task *tasks) {
  assert(task_num >= 1 && task_num < MAX_TASKS_NUM);
  struct spdk_app_opts opts;
  static char cpu_mask[255];
  g_app.task_num = task_num;
  g_app.running_thread = (1ULL << task_num) - 1;
  spdk_app_opts_init(&opts, sizeof(struct spdk_app_opts));
  sprintf(cpu_mask, "0x%lX", g_app.running_thread);
  opts.name = "kv_app";
  opts.rpc_addr = "./spdk.sock";
  opts.reactor_mask = cpu_mask;
  opts.json_config_file = json_config_file;
  pthread_mutex_init(&g_lock, NULL);
  int rc = 0;
  if ((rc = spdk_app_start(&opts, send_msg_to_all, tasks))) {
    SPDK_ERRLOG("ERROR starting application\n");
  }
  for (size_t i = 0; i < task_num; i++) {
    moodycamel_cons_token_destroy(g_threads[i].c_token);
    for (size_t j = 0; j < g_app.task_num; j++) {
      moodycamel_prod_token_destroy(g_threads[i].p_tokens[j]);
    }
    moodycamel_cq_destroy(g_threads[i].cq);
  }
  spdk_app_fini();
  return rc;
}

void kv_app_stop(int rc) {
  uint32_t index = kv_app_get_thread_index();
  spdk_poller_unregister(&g_threads[index].poller);
  pthread_mutex_lock(&g_lock);
  if (g_app.running_thread) {
    if (rc) {
      spdk_app_stop(rc);
      g_app.running_thread = 0;
    } else {
      g_app.running_thread &= ~(1ULL << index);
      spdk_thread_exit(g_threads[index].thread);
      if (!g_app.running_thread) {
        spdk_app_stop(rc);
      }
    }
  }
  pthread_mutex_unlock(&g_lock);
}
