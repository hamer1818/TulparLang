#include "sim/desync.hpp"

namespace tulpar::engine::sim {

void DesyncHistory::push(uint32_t tick, uint64_t hash) {
  uint32_t slot = next_ % kMaxHistory;
  ticks_[slot] = tick;
  hashes_[slot] = hash;
  valid_[slot] = true;
  next_++;
  if (count_ < kMaxHistory) count_++;
}

bool DesyncHistory::get(uint32_t tick, uint64_t *out_hash) const {
  // En yeniden eskiye tara: cakisan tick olamaz (push tek tick ilerler),
  // ama en yeniyi once bulmak ortalama aramayi hizlandirir.
  for (uint32_t i = 0; i < count_; i++) {
    uint32_t slot = (next_ - 1 - i) % kMaxHistory;
    if (valid_[slot] && ticks_[slot] == tick) {
      *out_hash = hashes_[slot];
      return true;
    }
  }
  return false;
}

DesyncHistory::Result DesyncHistory::compare(uint32_t tick, uint64_t remote_hash) const {
  uint64_t local = 0;
  if (!get(tick, &local)) return Result::kUnknown;
  return local == remote_hash ? Result::kMatch : Result::kMismatch;
}

} // namespace tulpar::engine::sim
