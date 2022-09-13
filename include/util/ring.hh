#ifndef __RDMA_EXAMPLE_RING__
#define __RDMA_EXAMPLE_RING__

#include "util.hh"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace rdma {

template <typename T, uint32_t Size> class Ring {
  class Handle {
  public:
    Handle() : head_(0), tail_(0) {}
    ~Handle() = default;

  public:
    std::atomic_uint32_t head_;
    std::atomic_uint32_t tail_;
  };

public:
  using ValueType = T;

public:
  Ring()
      : size_(alignUpPowerOf2(Size)), mask_(size_ - 1), capacity_(Size),
        elems_(
            static_cast<ValueType *>(std::malloc(sizeof(ValueType) * size_))) {
    checkp(elems_, "fail to allocate memory for ring");
  }
  ~Ring() {
    if (not std::is_trivially_destructible<ValueType>()) {
      size_t idx = consumer_handle_.head_;
      size_t end = producer_handle_.head_;
      while (idx != end) {
        elems_[idx].~ValueType();
        if (++idx == size_) {
          idx = 0;
        }
      }
    }
    std::free(elems_);
  }

public:
  template <typename... Args> auto push(Args &&...args) -> bool {
    uint32_t producer_new_head;
    uint32_t consumer_tail;
    uint32_t n_free;

    const uint32_t capacity = capacity_;
    uint32_t producer_old_head =
        producer_handle_.head_.load(std::memory_order_consume);
    // move producer head
    auto ok = true;
    do {
      n_free = capacity +
               consumer_handle_.tail_.load(std::memory_order_consume) -
               producer_old_head;
      if (n_free < 1)
        return false;
      producer_new_head = producer_old_head + 1;
      ok = producer_handle_.head_.compare_exchange_strong(
          producer_old_head, producer_new_head, std::memory_order_relaxed,
          std::memory_order_relaxed);
    } while (not ok);
    // place element
    new (&elems_[producer_old_head & mask_])
        ValueType(std::forward<Args>(args)...);

    // wait and update producer tail
    while (producer_handle_.tail_.load(std::memory_order_relaxed) !=
           producer_old_head) {
      pause();
    }

    producer_handle_.tail_.store(producer_new_head, std::memory_order_release);
    return true;
  }

  auto pop(ValueType &e) -> bool {
    uint32_t consumer_old_head;
    uint32_t consumer_new_head;

    // move consumer head
    consumer_old_head = consumer_handle_.head_.load(std::memory_order_consume);
    auto ok = true;
    do {
      auto n_remain = producer_handle_.tail_.load(std::memory_order_consume) -
                      consumer_old_head;
      if (n_remain < 1)
        return false;
      consumer_new_head = consumer_old_head + 1;
      ok = consumer_handle_.head_.compare_exchange_strong(
          consumer_old_head, consumer_new_head, std::memory_order_relaxed,
          std::memory_order_relaxed);
    } while (not ok);

    // move element
    uint32_t idx = consumer_old_head & mask_;
    e = std::move(elems_[idx]);
    elems_[idx].~ValueType();

    // wait and update consumer tail
    while (consumer_handle_.tail_.load(std::memory_order_relaxed) !=
           consumer_old_head) {
      pause();
    }
    consumer_handle_.tail_.store(consumer_new_head, std::memory_order_release);
    return true;
  }

private:
  Handle producer_handle_;
  [[gnu::unused]] char _pad1_[cache_line_size - sizeof(Handle)];

  Handle consumer_handle_;
  [[gnu::unused]] char _pad2_[cache_line_size - sizeof(Handle)];

  uint32_t size_;
  uint32_t mask_;
  uint32_t capacity_;

  ValueType *const elems_;
};
} // namespace rdma

#endif