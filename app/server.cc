#include "server.hh"
#include "hello.pb.h"

auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Server s(argv[1], argv[2]);

  s.registerHandler(0, [](rdma::RPCHandle &handle) -> void {
    echo::Hello request;
    handle.getRequest(request);
    printf("receive request: \"%s\"\n", request.greeting().c_str());
    echo::Hello response;
    auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    response.set_greeting("hi from server thread " + std::to_string(thread_id));
    printf("set response: \"%s\"\n", response.greeting().c_str());
    handle.setResponse(response);
  });

  return s.run();
}