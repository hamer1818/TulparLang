// L1 CORE — Span<T>: sahipsiz, bitisik gorunum. STL yok (plan L1).
#pragma once
#include <cstddef>

#include "platform/fatal.hpp"

namespace tulpar::engine {

template <class T> class Span {
public:
  Span() = default;
  Span(T *data, size_t count) : data_(data), count_(count) {}
  T *data() const { return data_; }
  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  T &operator[](size_t i) const {
    ENGINE_ASSERT(i < count_);
    return data_[i];
  }
  T *begin() const { return data_; }
  T *end() const { return data_ + count_; }
  Span sub(size_t off, size_t n) const {
    ENGINE_ASSERT(off + n <= count_);
    return Span(data_ + off, n);
  }

private:
  T *data_ = nullptr;
  size_t count_ = 0;
};

} // namespace tulpar::engine
