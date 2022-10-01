#ifndef __RDMA_EXAMPLE_MESSAGE__
#define __RDMA_EXAMPLE_MESSAGE__

#include <google/protobuf/message.h>

namespace rdma {

using message_t = google::protobuf::MessageLite;

class RPCHandle {
public:
  virtual ~RPCHandle() = default;
  virtual auto setRequest(const message_t &message) -> void = 0;
  virtual auto setResponse(const message_t &message) -> void = 0;
  virtual auto getRequest(message_t &message) -> void = 0;
  virtual auto getResponse(message_t &message) -> void = 0;
};

} // namespace rdma

#endif