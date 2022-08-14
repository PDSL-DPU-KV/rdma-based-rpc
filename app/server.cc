#include "server.h"

#include <cassert>
#include <cstdio>

auto main(int argc, char *argv[]) -> int {
  return rdma::Server(argv[1], argv[2]).run();
}