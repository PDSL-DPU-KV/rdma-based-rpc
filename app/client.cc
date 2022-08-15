#include "client.h"

#include <cstdio>
#include <unistd.h>

auto main(int argc, char *argv[]) -> int {
  rdma::Client c(argv[1], argv[2]);
  for (int i = 0; i < 256; i++) {
    c.call();
    usleep(100);
  }
  return 0;
}