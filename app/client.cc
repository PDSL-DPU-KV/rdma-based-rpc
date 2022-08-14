#include "client.h"

#include <cassert>
#include <cstdio>
#include <unistd.h>

auto main(int argc, char *argv[]) -> int {
  return rdma::Client(argv[1], argv[2]).call();
}