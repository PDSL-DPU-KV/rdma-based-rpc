#include "client.h"

#include <cstdio>

auto main(int argc, char *argv[]) -> int {
  rdma::Client c(argv[1], argv[2]);
  auto fn = [&c](int id) {
    for (int i = 0; i < 10; i++) {
      c.call(id, i);
    }
    printf("done\n");
  };

  std::thread t1(fn, 1), t2(fn, 2);
  fn(0);
  t1.join();
  t2.join();
  return 0;
}
