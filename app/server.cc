#include "server.h"

auto main(int argc, char *argv[]) -> int {
  rdma::Server(argv[1], argv[2]).run();
  return 0;
}