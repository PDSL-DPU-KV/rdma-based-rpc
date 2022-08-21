#ifndef __RDMA_EXAMPLE_APP_COMMON__
#define __RDMA_EXAMPLE_APP_COMMON__

#include <cstdint>

class [[gnu::packed]] HelloRequest {
public:
  uint32_t who;
  uint32_t which;
  char payload[16];
};

class [[gnu::packed]] HelloResponse {
public:
  char payload[32];
};

#endif