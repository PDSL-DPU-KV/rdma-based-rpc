#include "client.h"
#include "common.h"

auto main(int argc, char *argv[]) -> int {
  rdma::Client c(argv[1], argv[2]);
  auto fn = [&c](int id) {
    HelloRequest request;
    HelloResponse response;
    for (int i = 0; i < 10; i++) {
      request.who = id;
      request.which = i;

      ::snprintf(request.payload, sizeof(request.payload), "hello");
      
      ::printf("send request from %d, number is %d, payload is \"%s\"\n",
               request.who, request.which, request.payload);

      c.call(0, &request, &response);

      ::printf("receive response: \"%s\"\n", response.payload);
    }
    printf("done\n");
  };

  std::thread t1(fn, 1), t2(fn, 2);
  fn(0);
  t1.join();
  t2.join();
  return 0;
}
