// L1 CORE — StaticString<N>: sabit tamponlu, NUL sonlu dizgi. Ayirma yok;
// tasan icerik KIRPILIR ve `truncated()` ile gorunur (sessiz degil).
#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace tulpar::engine {

template <size_t N> class StaticString {
  static_assert(N >= 2, "en az 1 karakter + NUL");

public:
  StaticString() { buf_[0] = 0; }
  StaticString(const char *s) { buf_[0] = 0; append(s); }
  const char *c_str() const { return buf_; }
  size_t length() const { return len_; }
  size_t capacity() const { return N - 1; }
  bool empty() const { return len_ == 0; }
  bool truncated() const { return truncated_; }
  void clear() { len_ = 0; buf_[0] = 0; truncated_ = false; }

  StaticString &append(const char *s) {
    size_t n = std::strlen(s);
    size_t room = N - 1 - len_;
    if (n > room) { n = room; truncated_ = true; }
    std::memcpy(buf_ + len_, s, n);
    len_ += n;
    buf_[len_] = 0;
    return *this;
  }
  StaticString &append_char(char c) {
    if (len_ + 1 >= N) { truncated_ = true; return *this; }
    buf_[len_++] = c;
    buf_[len_] = 0;
    return *this;
  }
  StaticString &appendf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    size_t room = N - len_;
    int w = std::vsnprintf(buf_ + len_, room, fmt, ap);
    va_end(ap);
    if (w < 0) return *this;
    if ((size_t)w >= room) { truncated_ = true; len_ = N - 1; }
    else len_ += (size_t)w;
    return *this;
  }
  bool equals(const char *s) const { return std::strcmp(buf_, s) == 0; }

private:
  char buf_[N];
  size_t len_ = 0;
  bool truncated_ = false;
};

} // namespace tulpar::engine
