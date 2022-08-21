#include "server.h"
#include "common.h"

auto main(int argc, char *argv[]) -> int {
  rdma::Server s(argv[1], argv[2]);

  s.registerHandler(0, [](rdma::ServerSideCtx *ctx) -> void {
    auto request = ctx->getRequest<HelloRequest>();
    ::printf("receiver request from %d, number is %d, payload is \"%s\"\n",
             request->who, request->which, request->payload);
    HelloResponse resp;
    ::snprintf(resp.payload, sizeof(resp.payload), "%d-%d-%s-world-done",
               request->who, request->which, request->payload);
    ctx->setResponse(&resp);
  });

  return s.run();
}