#include "server.h"

auto main(int argc, char *argv[]) -> int {
  return rdma::Server(argv[1], argv[2]).run();
}