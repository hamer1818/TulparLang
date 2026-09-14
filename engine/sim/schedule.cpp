#include "sim/schedule.hpp"

#include <cstring>

#include "platform/fatal.hpp"

namespace tulpar::engine::sim {

bool Schedule::init(Arena &arena, JobSystem *jobs) {
  jobs_ = jobs;
  scratch_ = arena.alloc_array_zeroed<Job>(kMaxSystems);
  return scratch_ != nullptr;
}

bool Schedule::add(const SystemDesc &d) {
  if (n_ >= kMaxSystems) return false;
  sys_[n_++] = d;
  return true;
}

void Schedule::build() {
  stage_count_ = 0;
  for (uint32_t i = 0; i < kMaxSystems; i++) stage_len_[i] = 0;
  for (uint32_t i = 0; i < n_; i++) {
    // Cakisma: ayni asamadaki bir sistemle yaz-yaz / yaz-oku / oku-yaz.
    // En erken uygun asama: sonuncudan geriye gitmiyoruz — sira korunur
    // (bir sistem kendinden ONCE kaydedilenlerden sonra kosar; onceki
    // asamalara "sizmasi" veri bagimliligi olmasa bile yasak, cunku
    // belirlenimli sira kayit sirasidir).
    uint32_t stage = stage_count_ == 0 ? 0 : stage_count_ - 1;
    bool conflict = false;
    for (uint32_t j = 0; j < i; j++) {
      if (stage_of_[j] != stage) continue;
      ComponentMask rw = sys_[i].writes & (sys_[j].reads | sys_[j].writes);
      ComponentMask wr = sys_[i].reads & sys_[j].writes;
      if (rw || wr) { conflict = true; break; }
    }
    if (stage_count_ == 0 || conflict) stage = stage_count_++;
    stage_of_[i] = stage;
    stage_len_[stage]++;
  }
}

void Schedule::job_fn(void *p) {
  Job *j = static_cast<Job *>(p);
  j->sched->sys_[j->sys].fn(j->ctx);
}

void Schedule::run(World &w, float dt, const void *input, uint32_t tick) {
  for (uint32_t s = 0; s < stage_count_; s++) {
    uint32_t n = 0;
    JobDecl decls[kMaxSystems];
    for (uint32_t i = 0; i < n_; i++) {
      if (stage_of_[i] != s) continue;
      scratch_[n] = Job{this, i, SystemCtx{&w, dt, input, tick}};
      decls[n] = JobDecl{job_fn, &scratch_[n], sys_[i].name};
      n++;
    }
    if (jobs_ && n > 1) {
      Counter c;
      jobs_->run(decls, n, &c);
      jobs_->wait(c);
    } else {
      for (uint32_t k = 0; k < n; k++) job_fn(&scratch_[k]);
    }
  }
}

bool InputRecorder::init(Arena &arena, uint32_t block_bytes, uint32_t max_ticks) {
  block_bytes_ = block_bytes;
  max_ticks_ = max_ticks;
  data_ = arena.alloc_array_zeroed<uint8_t>((size_t)block_bytes * max_ticks);
  return data_ != nullptr;
}

void InputRecorder::record(uint32_t tick, const void *block) {
  ENGINE_ASSERT_MSG(tick < max_ticks_, "kayit kapasitesi (%u tick) asildi", max_ticks_);
  std::memcpy(data_ + (size_t)tick * block_bytes_, block, block_bytes_);
  if (tick + 1 > count_) count_ = tick + 1;
}

const void *InputRecorder::get(uint32_t tick) const {
  return tick < count_ ? data_ + (size_t)tick * block_bytes_ : nullptr;
}

} // namespace tulpar::engine::sim
