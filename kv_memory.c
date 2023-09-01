#include "kv_memory.h"

#include "kv_app.h"
#include "spdk/env.h"
#include "concurrentqueue.h"

void *kv_dma_malloc(size_t size) { return spdk_dma_malloc(size, 4, NULL); }

void *kv_dma_zmalloc(size_t size) { return spdk_dma_zmalloc(size, 4, NULL); }

void kv_dma_free(void *buf) { spdk_dma_free(buf); }

struct _kv_mempool {
  uint8_t *buf;
  MoodycamelCQHandle cq;
  MoodycamelToken *c_tokens, *p_tokens;
};

struct kv_mempool *kv_mempool_create(size_t count, size_t ele_size) {
  struct _kv_mempool *mp = kv_malloc(sizeof(struct _kv_mempool));
  moodycamel_cq_create(&mp->cq);
  mp->c_tokens = kv_calloc(kv_app()->task_num, sizeof(MoodycamelToken));
  mp->p_tokens = kv_calloc(kv_app()->task_num, sizeof(MoodycamelToken));
  for (size_t i = 0; i < kv_app()->task_num; i++) {
    moodycamel_cons_token(mp->cq, mp->c_tokens + i);
    moodycamel_prod_token(mp->cq, mp->p_tokens + i);
  };
  mp->buf = kv_dma_malloc(count * ele_size);
  for (size_t i = 0; i < count; i++)
    kv_mempool_put((struct kv_mempool *)mp, mp->buf + i * ele_size);
  return (struct kv_mempool *)mp;
}

void kv_mempool_put(struct kv_mempool *_mp, void *ele) {
  struct _kv_mempool *mp = (struct _kv_mempool *)_mp;
  // moodycamel_cq_enqueue(mp->cq, ele);
  moodycamel_cq_enqueue_with_token(
      mp->cq, mp->p_tokens[kv_app_get_thread_index()], ele);
}

void *kv_mempool_get(struct kv_mempool *_mp) {
  struct _kv_mempool *mp = (struct _kv_mempool *)_mp;
  MoodycamelValue value;
  // moodycamel_cq_try_dequeue(mp->cq, &value);
  if (moodycamel_cq_try_dequeue_with_token(
          mp->cq, mp->c_tokens[kv_app_get_thread_index()], &value))
    return value;
  else
    return NULL;
}

void kv_mempool_free(struct kv_mempool *_mp) {
  struct _kv_mempool *mp = (struct _kv_mempool *)_mp;
  for (size_t i = 0; i < kv_app()->task_num; i++) {
    moodycamel_cons_token_destroy(mp->c_tokens[i]);
    moodycamel_prod_token_destroy(mp->p_tokens[i]);
  }
  moodycamel_cq_destroy(mp->cq);
  kv_free(mp->c_tokens);
  kv_free(mp->p_tokens);
  kv_dma_free(mp->buf);
  kv_free(mp);
}

int64_t kv_mempool_get_id(struct kv_mempool *mp, void *ele) {
  return (uint8_t *)ele - ((struct _kv_mempool *)mp)->buf;
}

void *kv_mempool_get_ele(struct kv_mempool *mp, int64_t id) {
  return ((struct _kv_mempool *)mp)->buf + id;
}
