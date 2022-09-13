#ifndef __RDMA_EXAMPLE_SERVER__
#define __RDMA_EXAMPLE_SERVER__

#include "common.hh"
#include "ring.hh"
#include "thread_pool.hh"
#include <functional>
#include <unordered_map>

namespace rdma {

class ServerSideCtx;
class ConnWithCtx;
class Server {
public:
  constexpr static uint32_t default_back_log = 8;

public:
  using Handle = std::function<void(ServerSideCtx *)>;

public:
  Server(const char *host, const char *port);
  ~Server();

public:
  auto run() -> int;

private:
  static auto onConnEvent(int fd, short what, void *arg) -> void;
  static auto onExit(int fd, short what, void *arg) -> void;

public:
  auto handleConnEvent() -> void;

public:
  auto registerHandler(uint32_t id, Handle fn) -> void;
  auto getHandler(uint32_t id) -> Handle;

private:
  addrinfo *addr_{nullptr};
  rdma_cm_id *cm_id_{nullptr};
  rdma_event_channel *ec_{nullptr};

  event_base *base_{nullptr};
  event *conn_event_{nullptr};
  event *exit_event_{nullptr};

  std::unordered_map<uint32_t, Handle> handlers_{};
};

class ServerSideCtx final : public ConnCtx {
  friend class ConnWithCtx;

private:
  enum State : int32_t {
    Vacant,
    WaitingForBufferMeta,
    ReadingRequest,
    FilledWithRequest,
    FilledWithResponse,
    WritingResponse,
  };

public:
  ServerSideCtx(Conn *conn, void *buffer, uint32_t length);
  ~ServerSideCtx();

protected:
  auto advance(int32_t finished_op) -> void override;
  auto prepare() -> void; // receiver
  auto handleWrapper() -> void;

public:
  template <typename T> auto getRequest() -> const T * {
    assert(state_ == FilledWithRequest);
    return reinterpret_cast<const T *>(buffer_);
  }
  template <typename T> auto setResponse(const T *resp) -> void {
    assert(sizeof(T) < Conn::buffer_page_size);
    memcpy(buffer_, resp, sizeof(T));
    state_ = FilledWithResponse;
  }

protected:
  auto swap(ServerSideCtx *r) -> void;

private:
  State state_{Vacant}; // trace the state of ConnCtx
  ibv_mr *meta_mr_{nullptr};
  BufferMeta *remote_meta_{nullptr};
};

class ConnWithCtx final : public Conn {
  friend class Server;
  friend class ServerSideCtx;

public:
  constexpr static uint32_t max_context_num = queue_depth >> 1;
  constexpr static uint32_t max_receiver_num = max_context_num >> 1;
  constexpr static uint32_t max_sender_num = max_context_num - max_receiver_num;
  constexpr static uint32_t default_thread_pool_size = 1;

public:
  ConnWithCtx(Server *s, rdma_cm_id *id);
  ~ConnWithCtx();

private:
  Server *s_{nullptr};
  std::array<ServerSideCtx *, max_receiver_num> receivers_{};
  Ring<ServerSideCtx *, max_sender_num> senders_{};
  ThreadPool pool_{default_thread_pool_size};
};

} // namespace rdma

#endif