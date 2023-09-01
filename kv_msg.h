#ifndef _KV_MSG_H_
#define _KV_MSG_H_
#include <stdint.h>

#define _KV_MSG_ALIGN(size) ((size)&0x3 ? ((size) & ~0x3) + 0x4 : (size))
struct kv_msg {
// msg_types:
#define KV_MSG_OK (0U)
#define KV_MSG_GET (1U)
#define KV_MSG_SET (2U)
#define KV_MSG_DEL (3U)
#define KV_MSG_TEST (128U)
#define KV_MSG_OUTDATED (254U)
#define KV_MSG_ERR (255U)
  uint8_t type;
  uint8_t key_len;
  uint16_t hop;
  uint32_t value_len;
  uint8_t data[];
// data:
// uint8_t key[key_length]
// uint8_t _[key_length - _KV_MSG_ALIGN(key_length)];
// uint8_t value[value_len]; //must be 4 bytes aligned
#define KV_MSG_KEY(msg) ((msg)->data)
#define KV_MSG_VALUE(msg) ((msg)->data + _KV_MSG_ALIGN((msg)->key_len))
#define KV_MSG_SIZE(msg)                                                       \
  (sizeof(struct kv_msg) + _KV_MSG_ALIGN((msg)->key_len) + (msg)->value_len)
};

#endif
