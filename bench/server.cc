#include "server.hh"
#include "bench.pb.h"
#include "measure.hh"

auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Server s(argv[1], argv[2]);

  rdma::AccStatistics<1000> c;
  s.registerHandler(0, [&c](rdma::RPCHandle &handle) -> void {
    rdma::Timer t;
    t.begin();
    bench::Payload request;
    handle.getRequest(request);
    handle.setResponse(request);
    t.end();
    c.record(t.elapsed<rdma::microseconds>());
  });
  int i = s.run();
  printf("%s", c.dump().c_str());
  return i;
}