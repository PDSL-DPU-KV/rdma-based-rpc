#ifndef __RDMA_EXAMPLE_SERVER__
#define __RDMA_EXAMPLE_SERVER__

#include "connection.hh"
#include "context.hh"
#include "ring.hh"
#include <unordered_map>

namespace rdma {

class Server {
  class Context final : public ConnCtx {
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
    Context(uint32_t id, Conn *conn, void *buffer, uint32_t length);
    ~Context();

  public:
    auto advance(const ibv_wc &wc) -> void override;
    auto prepare() -> void; // receiver
    auto handler() -> void;

  public:
    State state_{Vacant}; // trace the state of ConnCtx
  };

  class ConnWithCtx final : public Conn {
  public:
    constexpr static uint32_t max_context_num = queue_depth >> 1;

  public:
    ConnWithCtx(uint16_t conn_id, Server *s, rdma_cm_id *cm_id);
    ~ConnWithCtx();

  public:
    auto serve() -> void;

  public:
    Server *s_{nullptr};
    std::array<Context *, max_context_num> handle_ctx_{};
    SPSCRing<Context *, max_context_num> pending_ctx_{};
    std::atomic_bool serving_{false};
    std::thread *bg_handler_{nullptr};
  };

public:
  constexpr static uint32_t default_back_log = 8;
  constexpr static uint32_t max_connection_num = 64;

public:
  using Handler = std::function<void(RPCHandle &)>;

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
  auto registerHandler(uint32_t id, Handler fn) -> void;
  auto getHandler(uint32_t id) -> Handler;

private:
  addrinfo *addr_{nullptr};
  rdma_cm_id *cm_id_{nullptr};
  rdma_event_channel *ec_{nullptr};

  event_base *base_{nullptr};
  event *conn_event_{nullptr};
  event *exit_event_{nullptr};

  std::unordered_map<uint32_t, Handler> handlers_{};

#ifdef USE_POLL
  ConnPoller bg_poller_{};
#endif
};

} // namespace rdma

#endif