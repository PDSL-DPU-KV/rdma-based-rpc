#ifndef __RDMA_EXAMPLE_RING__
#define __RDMA_EXAMPLE_RING__

#include "misc.hh"
#include "util.hh"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace rdma {

template <typename T, uint32_t Size> class SPSCRing {
public:
  using ValueType = T;
  using QueueType = SPSCRing<ValueType, Size>;

public:
  explicit SPSCRing()
      : size_(Size + 1), elems_(static_cast<ValueType *>(
                             std::malloc(sizeof(ValueType) * size_))),
        read_idx_(0), write_idx_(0) {
    if (size_ < 2) {
      throw std::runtime_error("size of queue is too small");
    }
    if (elems_ == nullptr) {
      throw std::bad_alloc();
    }
  }
  ~SPSCRing() {
    if (not std::is_trivially_destructible<ValueType>()) {
      size_t idx = read_idx_;
      size_t end = write_idx_;
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
  template <typename... Args> auto push(Args &&...args) -> void {
    while (not tryPush(args...)) {
      pause();
    }
  }

  auto pop(ValueType &e) -> void {
    while (not tryPop(e)) {
      pause();
    }
  }

public:
  template <typename... Args> auto tryPush(Args &&...args) -> bool {
    auto const cur_write = write_idx_.load(std::memory_order_relaxed);
    auto next = cur_write + 1;
    if (next == size_) {
      next = 0;
    }
    if (next != read_idx_.load(std::memory_order_acquire)) {
      new (&elems_[cur_write]) ValueType(std::forward<Args>(args)...);
      write_idx_.store(next, std::memory_order_release);
      return true;
    }
    return false;
  }

  auto tryPop(ValueType &e) -> bool {
    auto const cur_read = read_idx_.load(std::memory_order_relaxed);
    if (cur_read == write_idx_.load(std::memory_order_acquire)) {
      return false;
    }
    auto next = cur_read + 1;
    if (next == size_) {
      next = 0;
    }
    e = std::move(elems_[cur_read]);
    elems_[cur_read].~ValueType();
    read_idx_.store(next, std::memory_order_release);
    return true;
  }

public:
  auto isEmpty() const -> bool {
    return read_idx_.load(std::memory_order_acquire) ==
           write_idx_.load(std::memory_order_acquire);
  }

  auto isFull() const -> bool {
    auto next = write_idx_.load(std::memory_order_acquire) + 1;
    if (next == size_) {
      next = 0;
    }
    return next == read_idx_.load(std::memory_order_acquire);
  }

  auto approximateSize() const -> size_t {
    int ret = write_idx_.load(std::memory_order_acquire) -
              read_idx_.load(std::memory_order_acquire);
    if (ret < 0) {
      ret += size_;
    }
    return ret;
  }

  auto capacity() const -> size_t { return size_ - 1; }

private:
  [[gnu::unused]] char _head_pad_[cache_line_size];

  const uint32_t size_;
  ValueType *const elems_;
  alignas(cache_line_size) std::atomic_uint64_t read_idx_;
  alignas(cache_line_size) std::atomic_uint64_t write_idx_;

  [[gnu::unused]] char
      _tail_pad_[cache_line_size - sizeof(std::atomic_uint64_t)];
};

template <typename T, uint32_t Size> class MPMCRing {
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
  MPMCRing()
      : size_(alignUpPowerOf2(Size)), mask_(size_ - 1), capacity_(Size),
        elems_(
            static_cast<ValueType *>(std::malloc(sizeof(ValueType) * size_))) {
    checkp(elems_, "fail to allocate memory for ring");
  }
  ~MPMCRing() {
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
  template <typename... Args> auto push(Args &&...args) -> void {
    while (not tryPush(args...)) {
      pause();
    }
  }

  auto pop(ValueType &e) -> void {
    while (not tryPop(e)) {
      pause();
    }
  }

public:
  template <typename... Args> auto tryPush(Args &&...args) -> bool {
    uint32_t producer_new_head;
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

  auto tryPop(ValueType &e) -> bool {
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