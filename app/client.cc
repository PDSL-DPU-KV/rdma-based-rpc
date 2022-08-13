#include "client.h"

auto main(int argc, char *argv[]) -> int {
  rdma::Client c(argv[1], argv[2]);
  return 0;
}