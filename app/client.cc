#include "client.h"

#include <cassert>
#include <cstdio>

auto main(int argc, char *argv[]) -> int {
  rdma::Client c(argv[1], argv[2]);
  for (int i = 0; i < 256; i++) {
    assert(c.call() == 0);
  }
  return 0;
}