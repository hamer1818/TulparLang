// L1 CORE — Array<T>: sabit kapasiteli dizi. Kapasite init'te ARENADAN
// alinir, ASLA buyumez (A2). Dolunca push false doner; cagiran karar verir.
#pragma once
#include <cstddef>
#include <new>
#include <utility>

#include "core/containers/span.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine {

template <class T> class Array {
public:
  bool init(Arena &arena, size_t capacity) {
    data_ = arena.alloc_array<T>(capacity);
    cap_ = data_ ? capacity : 0;
    n_ = 0;
    return data_ != nullptr;
  }
  template <class... Args> bool push(Args &&...args) {
    if (n_ == cap_) return false;
    new (&data_[n_]) T(std::forward<Args>(args)...);
    n_++;
    return true;
  }
  void pop() {
    ENGINE_ASSERT(n_ > 0);
    data_[--n_].~T();
  }
  void clear() {
    for (size_t i = 0; i < n_; i++) data_[i].~T();
    n_ = 0;
  }
  T &operator[](size_t i) {
    ENGINE_ASSERT(i < n_);
    return data_[i];
  }
  const T &operator[](size_t i) const {
    ENGINE_ASSERT(i < n_);
    return data_[i];
  }
  T &back() { return (*this)[n_ - 1]; }
  size_t size() const { return n_; }
  size_t capacity() const { return cap_; }
  bool empty() const { return n_ == 0; }
  bool full() const { return n_ == cap_; }
  T *begin() { return data_; }
  T *end() { return data_ + n_; }
  Span<T> span() { return Span<T>(data_, n_); }

private:
  T *data_ = nullptr;
  size_t n_ = 0;
  size_t cap_ = 0;
};

} // namespace tulpar::engine
