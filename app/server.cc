#include "server.h"

#include <cassert>
#include <cstdio>

auto main(int argc, char *argv[]) -> int {
  rdma::Server s(argv[1], argv[2]);
  s.registerHandle([](rdma::Conn *conn) -> int {
    printf("receive: %s\n", conn->recvBuffer());

    const char *str = "world";
    ::memcpy(conn->sendBuffer(), str, strlen(str));

    conn->postSend();
    printf("send: %s\n", str);

    return 0;
  });
  s.run();
  return 0;
}