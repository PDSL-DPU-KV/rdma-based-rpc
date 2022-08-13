#include "client.h"

#include <cassert>
#include <cstdio>
#include <unistd.h>

auto main(int argc, char *argv[]) -> int {
  rdma::Client c(argv[1], argv[2]);
  auto fn = [](rdma::Conn *conn) -> int {
    const char *str = "hello";
    ::memcpy(conn->sendBuffer(), str, strlen(str));

    ibv_wc wc;
    conn->postRecv();
    conn->postSend();
    conn->pollCq(&wc);
    printf("send: %s\n", str);

    conn->pollCq(&wc);
    printf("receive: %s\n", conn->recvBuffer());

    return 0;
  };
  for (int i = 0; i < 256; i++) {
    c.call(fn);
    sleep(1);
  }
  return 0;
}