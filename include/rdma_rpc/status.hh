#ifndef __RDMA_EXAMPLE_STATUS__
#define __RDMA_EXAMPLE_STATUS__

#include <cstdint>
#include <utility>

namespace rdma {

class Status {
public:
  Status() = default;
  ~Status() = default;

  // cmp
  auto operator==(const Status &rhs) const -> bool {
    return code_ == rhs.code_;
  }
  auto operator!=(const Status &rhs) const -> bool { return !(*this == rhs); }

private:
  enum Code : uint32_t {
    kNone,
    kOk,
    kCallFailure,
  };

private:
  Status(Code code) : code_(code) {}

public:
  auto ok() -> bool { return code_ == kOk; }

public:
#define StatusName(what) what##Msg
#define StatusMessage(what)                                                    \
  static const constexpr char *StatusName(what) = #what
#define StatusDefine(c)                                                        \
  StatusMessage(c);                                                            \
  static auto c()->Status { return Status(k##c); }

  StatusDefine(None);
  StatusDefine(Ok);
  StatusDefine(CallFailure);

  auto whatHappened() -> const char * {
#define Branch(c)                                                              \
  case k##c: {                                                                 \
    return StatusName(c);                                                      \
  }
    switch (code_) {
    default:
      Branch(None);
      Branch(Ok);
      Branch(CallFailure);
    }

#undef Branch
  }

#undef StatusDefine
#undef StatusMessage
#undef StatusName

private:
  Code code_;
};

} // namespace rdma

#endif