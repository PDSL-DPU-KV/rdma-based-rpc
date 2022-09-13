#include "client.hh"
#include "hello.pb.h"

auto main(int argc, char *argv[]) -> int {
  rdma::Client c(argv[1], argv[2]);

  auto fn = [&c]() {
    echo::Hello request;
    echo::Hello response;
    for (int i = 0; i < 10; i++) {
      request.set_greeting("hello from " + std::to_string(i));
      printf("send request: \"%s\"\n", request.greeting().c_str());
      c.call(0, request, response);
      printf("receive response: \"%s\"\n", response.greeting().c_str());
    }
  };

  fn();
  return 0;
}
