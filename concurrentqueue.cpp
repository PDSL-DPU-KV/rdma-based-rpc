#include "concurrentqueue.h"

#include "concurrentqueue.hpp"

typedef moodycamel::ConcurrentQueue<void *> MoodycamelCQType, *MoodycamelCQPtr;

extern "C" {
using namespace moodycamel;
int moodycamel_cq_create(MoodycamelCQHandle *handle) {
  MoodycamelCQPtr retval =
      new MoodycamelCQType(1024 * MoodycamelCQType::BLOCK_SIZE);
  if (retval == nullptr) {
    return 0;
  }
  *handle = retval;
  return 1;
}

int moodycamel_cq_destroy(MoodycamelCQHandle handle) {
  delete reinterpret_cast<MoodycamelCQPtr>(handle);
  return 1;
}

int moodycamel_cons_token(MoodycamelCQHandle handle, MoodycamelToken *token) {
  *token = new ConsumerToken(*reinterpret_cast<MoodycamelCQPtr>(handle));
  return 1;
}

int moodycamel_prod_token(MoodycamelCQHandle handle, MoodycamelToken *token) {
  *token = new ProducerToken(*reinterpret_cast<MoodycamelCQPtr>(handle));
  return 1;
}

int moodycamel_cons_token_destroy(MoodycamelToken token) {
  delete reinterpret_cast<ConsumerToken *>(token);
  return 1;
}

int moodycamel_prod_token_destroy(MoodycamelToken token) {
  delete reinterpret_cast<ProducerToken *>(token);
  return 1;
}

int moodycamel_cq_enqueue(MoodycamelCQHandle handle, MoodycamelValue value) {
  return reinterpret_cast<MoodycamelCQPtr>(handle)->enqueue(value) ? 1 : 0;
}
int moodycamel_cq_enqueue_with_token(MoodycamelCQHandle handle,
                                     MoodycamelToken prod_token,
                                     MoodycamelValue value) {
  ProducerToken *token = (ProducerToken *)prod_token;
  return reinterpret_cast<MoodycamelCQPtr>(handle)->enqueue(*token, value) ? 1
                                                                           : 0;
}
size_t moodycamel_cq_enqueue_bulk_with_token(MoodycamelCQHandle handle,
                                             MoodycamelToken prod_token,
                                             MoodycamelValue *values,
                                             size_t num) {
  ProducerToken *token = (ProducerToken *)prod_token;
  return reinterpret_cast<MoodycamelCQPtr>(handle)->enqueue_bulk(*token, values,
                                                                 num);
}

int moodycamel_cq_try_dequeue(MoodycamelCQHandle handle,
                              MoodycamelValue *value) {
  return reinterpret_cast<MoodycamelCQPtr>(handle)->try_dequeue(*value) ? 1 : 0;
}

int moodycamel_cq_try_dequeue_with_token(MoodycamelCQHandle handle,
                                         MoodycamelToken cons_token,
                                         MoodycamelValue *value) {
  ConsumerToken *token = (ConsumerToken *)cons_token;
  return reinterpret_cast<MoodycamelCQPtr>(handle)->try_dequeue(*token, *value)
             ? 1
             : 0;
}

size_t moodycamel_cq_try_dequeue_bulk(MoodycamelCQHandle handle,
                                      MoodycamelValue *values, size_t max) {
  return reinterpret_cast<MoodycamelCQPtr>(handle)->try_dequeue_bulk(values,
                                                                     max);
}

size_t moodycamel_cq_try_dequeue_bulk_with_token(MoodycamelCQHandle handle,
                                                 MoodycamelToken cons_token,
                                                 MoodycamelValue *values,
                                                 size_t max) {
  ConsumerToken *token = (ConsumerToken *)cons_token;
  return reinterpret_cast<MoodycamelCQPtr>(handle)->try_dequeue_bulk(
      *token, values, max);
}

size_t moodycamel_cq_size_approx(MoodycamelCQHandle handle) {
  return reinterpret_cast<MoodycamelCQPtr>(handle)->size_approx();
}
}
