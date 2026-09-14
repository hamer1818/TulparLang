// L4: ECS + zamanlayici + sabit adim + replay determinizmi (Faz 2 kapisi:
// ayni girdi dizisi -> ayni content_hash, seri ve paralel zamanlayicida).
#include <cstdio>
#include <cstring>

#include "core/jobs/job_system.hpp"
#include "core/math/vec.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "sim/ecs.hpp"
#include "sim/schedule.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
struct Pos { Vec3 p; };
struct Vel { Vec3 v; };
struct Health { int32_t hp; };
struct Input { float ax, ay; uint32_t fire; };

ComponentId cid_pos() { return component_id<Pos>("Pos"); }
ComponentId cid_vel() { return component_id<Vel>("Vel"); }
ComponentId cid_hp() { return component_id<Health>("Health"); }

void sys_input(SystemCtx &c) { // girdi -> hiz (Vel yazar)
  const Input *in = static_cast<const Input *>(c.input);
  c.world->each(mask_of(cid_vel()), [&](const ChunkView &v) {
    Vel *vel = v.col<Vel>();
    for (uint32_t i = 0; i < v.count; i++) vel[i].v = Vec3{in->ax, in->ay, 0};
  });
}
void sys_move(SystemCtx &c) { // Vel okur, Pos yazar
  c.world->each(mask_of(cid_pos()) | mask_of(cid_vel()), [&](const ChunkView &v) {
    Pos *p = v.col<Pos>();
    const Vel *vel = v.col<Vel>();
    for (uint32_t i = 0; i < v.count; i++) p[i].p += vel[i].v * c.dt;
  });
}
void sys_damage(SystemCtx &c) { // Health yazar (Pos/Vel'den bagimsiz -> move ile ayni asama)
  const Input *in = static_cast<const Input *>(c.input);
  if (!in->fire) return;
  c.world->each(mask_of(cid_hp()), [&](const ChunkView &v) {
    Health *h = v.col<Health>();
    for (uint32_t i = 0; i < v.count; i++) h[i].hp -= 1;
  });
}
void sys_bounds(SystemCtx &c) { // Pos okur+yazar -> move'dan sonraki asama
  c.world->each(mask_of(cid_pos()), [&](const ChunkView &v) {
    Pos *p = v.col<Pos>();
    for (uint32_t i = 0; i < v.count; i++) {
      if (p[i].p.x > 100) p[i].p.x = -100;
      if (p[i].p.y > 100) p[i].p.y = -100;
    }
  });
}

bool build_world(Arena &a, World &w) {
  WorldConfig wc;
  wc.max_entities = 2048;
  wc.max_chunks = 64;
  if (!w.init(a, wc)) return false;
  ComponentMask mover = mask_of(cid_pos()) | mask_of(cid_vel());
  ComponentMask soldier = mover | mask_of(cid_hp());
  for (int i = 0; i < 500; i++) {
    Entity e = w.create(i % 3 == 0 ? soldier : mover);
    if (!e.valid()) return false;
    w.get<Pos>(e)->p = Vec3{(float)i, (float)(i % 7), 0};
    if (i % 3 == 0) w.get<Health>(e)->hp = 10;
  }
  return true;
}

bool build_schedule(Arena &a, Schedule &s, JobSystem *js) {
  if (!s.init(a, js)) return false;
  s.add(SystemDesc{"input", sys_input, 0, mask_of(cid_vel())});
  s.add(SystemDesc{"move", sys_move, mask_of(cid_vel()), mask_of(cid_pos())});
  s.add(SystemDesc{"damage", sys_damage, 0, mask_of(cid_hp())});
  s.add(SystemDesc{"bounds", sys_bounds, mask_of(cid_pos()), mask_of(cid_pos())});
  s.build();
  return true;
}

uint64_t run_sim(Arena &a, JobSystem *js, InputRecorder *rec_in, InputRecorder *rec_out, uint32_t ticks,
                 uint32_t *stages_out) {
  World w;
  if (!build_world(a, w)) return 0;
  Schedule s;
  if (!build_schedule(a, s, js)) return 0;
  if (stages_out) *stages_out = s.stage_count();
  uint32_t seed = 12345u;
  for (uint32_t t = 0; t < ticks; t++) {
    Input in;
    if (rec_in) in = *static_cast<const Input *>(rec_in->get(t));
    else {
      seed = seed * 1664525u + 1013904223u;
      in = Input{(float)(seed % 200) / 100.0f - 1.0f, (float)((seed >> 8) % 200) / 100.0f - 1.0f, (seed >> 16) & 1};
      if (rec_out) rec_out->record(t, &in);
    }
    s.run(w, 1.0f / 60.0f, &in, t);
  }
  return w.content_hash();
}
} // namespace

ENGINE_TEST(ecs_create_get_destroy_and_generations) {
  SystemArena sys;
  CHECK(sys.reserve(8u << 20, "ecs"));
  World w;
  WorldConfig wc;
  wc.max_entities = 64;
  wc.max_chunks = 8;
  CHECK(w.init(sys, wc));
  ComponentMask m = mask_of(cid_pos()) | mask_of(cid_vel());
  Entity a = w.create(m), b = w.create(m), c = w.create(mask_of(cid_hp()));
  CHECK(a.valid() && b.valid() && c.valid());
  CHECK(w.stats().archetypes == 2);
  w.get<Pos>(a)->p = Vec3{1, 2, 3};
  w.get<Pos>(b)->p = Vec3{4, 5, 6};
  CHECK(w.get<Vel>(a) != nullptr && w.get<Health>(a) == nullptr); // a'da Health yok
  w.destroy(a);                       // swap-remove: b, a'nin satirina tasinir
  CHECK(!w.alive(a) && w.get<Pos>(a) == nullptr);
  bool moved_ok = w.alive(b) && (w.get<Pos>(b)->p == Vec3{4, 5, 6}); // tasinan veri korundu
  CHECK(moved_ok);
  Entity a2 = w.create(m);
  CHECK(a2.index() == a.index() && a2 != a); // ayni slot, yeni nesil
  CHECK(!w.alive(a));
  // Kapasite: 64 entity -> 65. gecersiz ve sayilir
  uint32_t made = 0;
  for (int i = 0; i < 80; i++) if (w.create(m).valid()) made++;
  CHECK(w.stats().entities == 64);
  CHECK(w.stats().create_failed == 80 - made);
}

ENGINE_TEST(ecs_each_iterates_soa_columns) {
  SystemArena sys;
  CHECK(sys.reserve(8u << 20, "ecs2"));
  World w;
  WorldConfig wc;
  wc.max_entities = 1000;
  wc.chunk_bytes = 4096; // kucuk chunk: birden fazla chunk zorlansin
  wc.max_chunks = 64;
  CHECK(w.init(sys, wc));
  ComponentMask m = mask_of(cid_pos()) | mask_of(cid_vel());
  for (int i = 0; i < 700; i++) {
    Entity e = w.create(i % 2 ? m : mask_of(cid_pos()));
    w.get<Pos>(e)->p.x = (float)i;
  }
  CHECK(w.stats().chunks_used > 2);
  uint32_t seen = 0;
  double sum = 0;
  w.each(mask_of(cid_pos()), [&](const ChunkView &v) {
    const Pos *p = v.col<Pos>();
    for (uint32_t i = 0; i < v.count; i++) { sum += p[i].p.x; seen++; }
  });
  CHECK(seen == 700);
  CHECK(sum == 699.0 * 700.0 / 2.0);
  uint32_t movers = 0;
  w.each(m, [&](const ChunkView &v) { movers += v.count; });
  CHECK(movers == 350);
}

ENGINE_TEST(schedule_builds_stages_from_read_write_sets) {
  SystemArena sys;
  CHECK(sys.reserve(4u << 20, "sch"));
  Schedule s;
  CHECK(build_schedule(sys, s, nullptr));
  // input(yaz Vel) | move(oku Vel, yaz Pos) cakisir -> asama 2;
  // damage (Health) move ile cakismaz -> asama 2; bounds Pos yazar -> asama 3.
  CHECK(s.stage_count() == 3);
  CHECK(s.systems_in_stage(0) == 1);
  CHECK(s.systems_in_stage(1) == 2);
  CHECK(s.systems_in_stage(2) == 1);
}

ENGINE_TEST(fixed_step_is_deterministic_and_clips) {
  FixedStep fs;
  uint32_t total = 0;
  for (int f = 0; f < 60; f++) total += fs.advance(1.0f / 60.0f + 1e-7f);
  CHECK(total == 60);
  FixedStep fs2;
  CHECK(fs2.advance(1.0f) == 4); // hitch: en cok 4 tick, kalan atilir
  CHECK(fs2.clipped == 1 && fs2.accumulator == 0);
}

// FAZ 2 KAPISI: replay determinizmi — kaydedilen girdiyle ayni ozet (seri),
// ve paralel zamanlayici ayni ozeti verir (asama disiplini yeterli).
ENGINE_TEST(faz2_gate_replay_is_bit_identical) {
  static SystemArena sys;
  CHECK(sys.reserve(64u << 20, "replay"));
  InputRecorder rec;
  CHECK(rec.init(sys, sizeof(Input), 1200));
  uint32_t stages = 0;
  uint64_t h1 = run_sim(sys, nullptr, nullptr, &rec, 1000, &stages);
  CHECK(h1 != 0);
  CHECK(rec.ticks() == 1000);
  uint64_t h2 = run_sim(sys, nullptr, &rec, nullptr, 1000, nullptr);
  CHECK(h1 == h2);
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));
  uint64_t h3 = run_sim(sys, &js, &rec, nullptr, 1000, nullptr);
  CHECK(h1 == h3);
  // Pozitif kontrol: girdi degisirse ozet degismeli (ozet gercekten icerigi olcuyor).
  Input *tampered = const_cast<Input *>(static_cast<const Input *>(rec.get(500)));
  tampered->ax += 0.5f;
  uint64_t h4 = run_sim(sys, &js, &rec, nullptr, 1000, nullptr);
  CHECK(h4 != h1);
  js.shutdown();
  std::printf("    [bilgi] 1000 tick, 500 entity, %u asama; ozet %016llx (seri = replay = paralel)\n", stages,
              (unsigned long long)h1);
}

ENGINE_TEST(sim_tick_allocates_nothing) {
  static SystemArena sys;
  CHECK(sys.reserve(32u << 20, "sim0"));
  World w;
  CHECK(build_world(sys, w));
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));
  Schedule s;
  CHECK(build_schedule(sys, s, &js));
  Input in{0.5f, -0.25f, 1};
  AllocGate::begin_frame();
  for (uint32_t t = 0; t < 50; t++) s.run(w, 1.0f / 60.0f, &in, t);
  CHECK(AllocGate::end_frame() == 0);
  js.shutdown();
}
