// L1 CORE — kisa kritik bolgeler icin spinlock. Mutex YOK (plan L1):
// bekleme uzunsa tasarim hatasidir, kilit degil fiber gecisi gerekir.
#pragma once
#include <atomic>

#include "platform/thread.hpp"

namespace tulpar::engine {

class SpinLock {
public:
  void lock() {
    for (uint32_t spins = 0;; spins++) {
      if (!flag_.test_and_set(std::memory_order_acquire)) return;
      if (spins > 64) {
        platform::thread_yield();
        spins = 0;
      }
    }
  }
  void unlock() { flag_.clear(std::memory_order_release); }

private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

struct SpinGuard {
  explicit SpinGuard(SpinLock &l) : l_(l) { l_.lock(); }
  ~SpinGuard() { l_.unlock(); }
  SpinGuard(const SpinGuard &) = delete;
  SpinGuard &operator=(const SpinGuard &) = delete;
private:
  SpinLock &l_;
};

} // namespace tulpar::engine
