#include "client.hh"
#include "hello.pb.h"

#include <thread>

auto main(int argc, char *argv[]) -> int {
  rdma::Client c;

  auto conn_id_1 = c.connect(argv[1], argv[2]);
  auto conn_id_2 = c.connect(argv[1], argv[2]);

  auto fn = [&c](uint32_t conn_id) {
    echo::Hello request;
    echo::Hello response;
    for (int i = 0; i < 10; i++) {
      request.set_greeting("hello from " + std::to_string(i));
      printf("send request: \"%s\"\n", request.greeting().c_str());
      c.call(conn_id, 0, request, response);
      printf("receive response: \"%s\"\n", response.greeting().c_str());
    }
  };

  fn(conn_id_1);
  std::thread(fn, conn_id_2).join();
  return 0;
}
