#ifndef __RDMA_EXAMPLE_CONTEXT__
#define __RDMA_EXAMPLE_CONTEXT__

#include "connection.hh"
#include "message.hh"

namespace rdma {

constexpr static uint32_t imm_request_size = 4096; // 4k;

class [[gnu::packed]] BufferMeta {
public:
  explicit BufferMeta(void *buf = nullptr, uint32_t buf_len = 0);
  ~BufferMeta() = default;

public:
  void *buf_{nullptr};
  uint32_t buf_len_{0};
};

enum MessageType : uint32_t {
  Dummy,
  Request,
  ImmRequest,
  Response,
};

class [[gnu::packed]] MessageHeader {
public:
  void *addr_{nullptr};
  uint32_t msg_len_{0};
  uint32_t ctx_id_{0};
  uint32_t rpc_id_{0};
  MessageType type_{Dummy};
};

class ConnCtx : public RPCHandle {
public:
  explicit ConnCtx(uint32_t id, Conn *conn, void *buffer = nullptr,
                   uint32_t length = 0);

public:
  virtual ~ConnCtx() = default;
  virtual auto advance(const ibv_wc &wc) -> void = 0;

public:
  virtual auto setRequest(const message_t &message) -> void;
  virtual auto setResponse(const message_t &message) -> void;
  virtual auto getRequest(message_t &message) -> void;
  virtual auto getResponse(message_t &message) -> void;

public:
  auto messageType() -> MessageType;

private:
  auto setMessage(const message_t &message) -> void;
  auto getMessage(message_t &message) -> void;

protected:
  auto reset() -> void;
  auto header() -> MessageHeader &;
  auto headerLength() -> uint32_t;
  auto rawBuf() -> void *;
  auto rawBufLength() -> uint32_t;
  auto rawMessage() -> char *;
  auto readableLength() -> uint32_t;

protected:
  uint32_t id_;
  Conn *conn_{nullptr}; // created in which Conn
  BufferMeta meta_{};
};

} // namespace rdma

#endif