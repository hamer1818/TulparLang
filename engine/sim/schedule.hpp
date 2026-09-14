// L4 SIMULATION — sistem zamanlayici + sabit adim + kayit/replay.
//
// Sistemler okuma/yazma bilesen kumelerini BILDIRIR; bagimlilik grafigi
// bundan cikar (plan L4: "compile-time cikarilir -> otomatik
// paralellestirme" — alt kume gelene kadar init'te cikarilir, kare icinde
// degil). Cakismayan sistemler ayni asamada job olarak paralel kosar,
// cakisanlar sonraki asamaya. Asama sirasi kayit sirasidir: BELIRLENIMLI.
//
// Sabit adim: sim her zaman `step_s` ile ilerler; kare suresi biriktirilir.
// Kayit: her tick'in girdisi (sabit boyutlu blok) halkaya yazilir; replay
// ayni girdilerle ayni tick sayisi -> ayni content_hash (Faz 2 kapisi).
#pragma once
#include <cstdint>

#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "sim/ecs.hpp"

namespace tulpar::engine::sim {

struct SystemCtx {
  World *world;
  float dt;
  const void *input; // bu tick'in girdi blogu (kayit/replay)
  uint32_t tick;
};
using SystemFn = void (*)(SystemCtx &);

struct SystemDesc {
  const char *name;
  SystemFn fn;
  ComponentMask reads;
  ComponentMask writes;
};

class Schedule {
public:
  static constexpr uint32_t kMaxSystems = 64;
  bool init(Arena &arena, JobSystem *jobs /* null = seri */);
  bool add(const SystemDesc &d);
  // Asamalari cikar: sirayla, bir sistem onceki asamadaki biriyle cakisiyorsa
  // (yaz-yaz, yaz-oku, oku-yaz) yeni asama. Kayit sirasi korunur.
  void build();
  void run(World &w, float dt, const void *input, uint32_t tick);
  uint32_t stage_count() const { return stage_count_; }
  uint32_t systems_in_stage(uint32_t s) const { return stage_len_[s]; }

private:
  struct Job {
    Schedule *sched;
    uint32_t sys;
    SystemCtx ctx;
  };
  static void job_fn(void *p);
  JobSystem *jobs_ = nullptr;
  SystemDesc sys_[kMaxSystems];
  uint32_t n_ = 0;
  uint32_t stage_of_[kMaxSystems];
  uint32_t stage_len_[kMaxSystems];
  uint32_t stage_count_ = 0;
  Job *scratch_ = nullptr;
};

// Girdi kaydi: tick basina sabit boyutlu blok, halka. Replay icin geri okunur.
class InputRecorder {
public:
  bool init(Arena &arena, uint32_t block_bytes, uint32_t max_ticks);
  void record(uint32_t tick, const void *block);
  const void *get(uint32_t tick) const;
  uint32_t ticks() const { return count_; }
  uint32_t block_bytes() const { return block_bytes_; }

private:
  uint8_t *data_ = nullptr;
  uint32_t block_bytes_ = 0, max_ticks_ = 0, count_ = 0;
};

// Sabit adim biriktirici. A7: kare suresi ne olursa olsun tick sayisi
// belirlenimli; fazla birikim (spiral of death) `max_ticks_per_frame` ile
// kirpilir ve SAYILIR.
struct FixedStep {
  float step_s = 1.0f / 60.0f;
  float accumulator = 0;
  uint32_t tick = 0;
  uint32_t max_ticks_per_frame = 4;
  uint32_t clipped = 0;
  // Kare suresini ekler; kac tick kosulacagini doner.
  uint32_t advance(float frame_s) {
    accumulator += frame_s;
    uint32_t n = 0;
    while (accumulator >= step_s && n < max_ticks_per_frame) {
      accumulator -= step_s;
      n++;
    }
    if (accumulator >= step_s) {
      clipped++;
      accumulator = 0; // birikimi at: yakalamaya calisma, sarmal olur
    }
    return n;
  }
};

} // namespace tulpar::engine::sim
