#include "client.hh"
#include "bench.pb.h"
#include "measure.hh"

auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Client c;
  auto message_size = std::atoi(argv[3]);
  auto n_thread = std::atoi(argv[4]);
  auto conn_id_1 = c.connect(argv[1], argv[2]);

  // rdma::AccStatistics<1000> s;
  rdma::Timer t;
  auto fn = [&c](uint32_t conn_id, uint32_t message_size) {
    bench::Payload request;
    request.mutable_data()->resize(message_size, 'F');
    bench::Payload response;
    rdma::Status ret;
    // rdma::Timer t;
    for (int i = 0; i < 10000; i++) {
      // t.begin();
      ret = c.call(conn_id, 0, request, response);
      // t.end();
      // s.record(t.elapsed<rdma::microseconds>());
      if (not ret.ok()) {
        printf("%s\n", ret.whatHappened());
        break;
      }
    }
  };
  t.begin();
  std::vector<std::thread> ts;
  for (int32_t i = 0; i < n_thread; i++) {
    ts.emplace_back(fn, conn_id_1, message_size);
  }
  for (auto &t : ts) {
    t.join();
  }
  t.end();
  printf("elasped: %ld\n", t.elapsed<rdma::microseconds>());
  printf("RPC per s: %f\n", (double)(1000000 * n_thread) /
                                t.elapsed<rdma::microseconds>() * 1000 * 1000);
  // printf("%s", s.dump().c_str());
  return 0;
}
