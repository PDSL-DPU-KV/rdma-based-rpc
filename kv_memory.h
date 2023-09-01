#ifndef _KV_MEMORY_H_
#define _KV_MEMORY_H_
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
// TODO: using SPDK

#define kv_memcpy(dst, src, n) memcpy(dst, src, n)
#define kv_memmove(dst, src, n) memmove(dst, src, n)
#define kv_memcmp8(dst, src, n) memcmp(dst, src, n)
#define kv_memset(s, c, n) memset(s, c, n)
#define kv_malloc(size) malloc(size)
#define kv_calloc(nmemb, size) calloc(nmemb, size)
#define kv_free(ptr) free(ptr)

void *kv_dma_malloc(size_t size);
void *kv_dma_zmalloc(size_t size);
void kv_dma_free(void *buf);
struct kv_mempool;
struct kv_mempool *kv_mempool_create(size_t count, size_t ele_size);
void kv_mempool_put(struct kv_mempool *mp, void *ele);
void *kv_mempool_get(struct kv_mempool *mp);
void kv_mempool_free(struct kv_mempool *mp);
int64_t kv_mempool_get_id(struct kv_mempool *mp, void *ele);
void *kv_mempool_get_ele(struct kv_mempool *mp, int64_t id);

#endif
