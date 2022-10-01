#include "context.hh"

namespace rdma {

BufferMeta::BufferMeta(void *buf, uint32_t buf_len)
    : buf_(buf), buf_len_(buf_len) {}

ConnCtx::ConnCtx(uint32_t id, Conn *conn, void *buffer, uint32_t length)
    : id_(id), conn_(conn), meta_(buffer, length) {
  reset();
}

auto ConnCtx::setMessage(const message_t &message) -> void {
  header().msg_len_ = message.ByteSizeLong();
  assert(header().msg_len_ + sizeof(MessageHeader) <= meta_.buf_len_);
  auto ok = message.SerializeToArray(rawMessage(), header().msg_len_);
  assert(ok);
}

auto ConnCtx::getMessage(message_t &message) -> void {
  assert(header().msg_len_ > 0);
  auto ok = message.ParseFromArray(rawMessage(), header().msg_len_);
  assert(ok);
}

auto ConnCtx::setRequest(const message_t &message) -> void {
  setMessage(message);
  header().ctx_id_ = id_;
  header().type_ = header().msg_len_ <= imm_request_size
                       ? MessageType::ImmRequest
                       : MessageType::Request;
}

auto ConnCtx::setResponse(const message_t &message) -> void {
  setMessage(message);
  header().type_ = MessageType::Response;
}

auto ConnCtx::getRequest(message_t &message) -> void {
  assert(messageType() == Request or messageType() == ImmRequest);
  getMessage(message);
}

auto ConnCtx::getResponse(message_t &message) -> void {
  assert(messageType() == Response);
  getMessage(message);
}

auto ConnCtx::messageType() -> MessageType { return header().type_; }

auto ConnCtx::reset() -> void { memset(meta_.buf_, 0, meta_.buf_len_); }

auto ConnCtx::header() -> MessageHeader & {
  return *reinterpret_cast<MessageHeader *>(meta_.buf_);
}

auto ConnCtx::rawBuf() -> void * { return meta_.buf_; }

auto ConnCtx::rawBufLength() -> uint32_t { return meta_.buf_len_; }

auto ConnCtx::rawMessage() -> char * {
  return reinterpret_cast<char *>(meta_.buf_) + sizeof(MessageHeader);
}

auto ConnCtx::headerLength() -> uint32_t { return sizeof(MessageHeader); }

auto ConnCtx::readableLength() -> uint32_t {
  return header().msg_len_ + sizeof(MessageHeader);
}
} // namespace rdma