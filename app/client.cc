#include "client.hh"
#include "hello.pb.h"

#include <thread>

auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Client c;

  auto conn_id_1 = c.connect(argv[1], argv[2]);
  auto conn_id_2 = c.connect(argv[1], argv[2]);

  auto fn = [&c](uint32_t conn_id) {
    echo::Hello request;
    echo::Hello response;
    rdma::Status s;
    for (int i = 0; i < 1000; i++) {
      request.set_greeting("hello from " + std::to_string(i));
      printf("send request: \"%s\"\n", request.greeting().c_str());
      s = c.call(conn_id, 0, request, response);
      if (not s.ok()) {
        printf("%s\n", s.whatHappened());
        break;
      }
      printf("receive response: \"%s\"\n", response.greeting().c_str());
    }
  };

  std::thread t2(fn, conn_id_2);
  std::thread t3(fn, conn_id_1);
  std::thread t4(fn, conn_id_2);
  fn(conn_id_1);
  t2.join();
  t3.join();
  t4.join();
  return 0;
}
