#ifndef __RDMA_EXAMPLE_CONTEXT__
#define __RDMA_EXAMPLE_CONTEXT__

#include "connection.hh"
#include "message.hh"

namespace rdma {

class [[gnu::packed]] BufferMeta {
public:
  explicit BufferMeta(void *buf = nullptr, uint32_t buf_len = 0);
  ~BufferMeta() = default;

public:
  void *buf_{nullptr};
  uint32_t buf_len_{0};
};

class [[gnu::packed]] MessageHeader {
public:
  void *addr_{nullptr};
  uint32_t msg_len_{0};
  uint32_t ctx_id_{0};
  uint32_t rpc_id_{0};
  bool is_response_{false};
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

private:
  auto setMessage(const message_t &message) -> void;
  auto getMessage(message_t &message) -> void;

protected:
  auto reset() -> void;
  auto header() -> MessageHeader &;
  auto headerLength() -> uint32_t;
  auto rawBuf() -> void *;
  auto rawMessage() -> char *;
  auto readableLength() -> uint32_t;

protected:
  uint32_t id_;
  Conn *conn_{nullptr}; // created in which Conn
  BufferMeta meta_{};
};

} // namespace rdma

#endif