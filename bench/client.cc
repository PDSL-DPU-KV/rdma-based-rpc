#include "client.hh"
#include "bench.pb.h"
#include "measure.hh"

auto main(int argc, char *argv[]) -> int {
  rdma::Client c;
  auto message_size = std::atoi(argv[3]);
  auto conn_id_1 = c.connect(argv[1], argv[2]);

  rdma::AccStatistics<1000> s;
  auto fn = [&c, &s](uint32_t conn_id, uint32_t message_size) {
    bench::Payload request;
    request.set_data("F", message_size);
    bench::Payload response;
    rdma::Status ret;
    rdma::Timer t;
    for (int i = 0; i < 1000; i++) {
      t.begin();
      ret = c.call(conn_id, 0, request, response);
      t.end();
      s.record(t.elapsed<rdma::microseconds>());
      if (not ret.ok()) {
        printf("%s\n", ret.whatHappened());
        break;
      }
    }
  };
  fn(conn_id_1, message_size);
  printf("%s", s.dump().c_str());
  return 0;
}
