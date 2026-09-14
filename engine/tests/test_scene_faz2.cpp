// FAZ 2 CIKTISI (headless): "fizikli, animasyonlu, gezinen test sahnesi".
// ECS + zamanlayici + Jolt (fiber job'larda) + navmesh takibi + animasyon
// ornekleme, 600 tick, sabit adim. Kapilar: seri = paralel ayni ozet
// (ECS icerigi ^ fizik durumu), tick icinde 0 operator new (isinma sonrasi),
// ajanlar navmesh'te kaliyor.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/jobs/job_system.hpp"
#include "core/math/vec.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "platform/time.hpp"
#include "sim/animation.hpp"
#include "sim/ecs.hpp"
#include "sim/navmesh.hpp"
#include "sim/physics.hpp"
#include "sim/schedule.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
constexpr uint32_t kAgents = 16, kBoxes = 20, kJoints = 4, kTicks = 600;

struct Agent { Vec3 pos; Vec3 target; Vec3 path[16]; int path_n; int path_i; uint32_t seed; float speed; };
struct AnimState { float time; Vec3 t[kJoints]; Quat r[kJoints]; Vec3 s[kJoints]; };
struct PhysMirror { BodyId body; Vec3 pos; Quat rot; };

ComponentId cid_agent() { return component_id<Agent>("Agent"); }
ComponentId cid_anim() { return component_id<AnimState>("AnimState"); }
ComponentId cid_phys() { return component_id<PhysMirror>("PhysMirror"); }

struct SceneCtx {
  NavMesh nav;
  Physics phys;
  const ClipHeader *clip = nullptr;
  float dt = 1.0f / 60.0f;
};
SceneCtx *g_ctx = nullptr;

// Zemin + duvar + gecit (navmesh testiyle ayni sahne) — fizik zemini de ayni.
bool build_nav(NavMesh &nm) {
  float v[3 * 64]; int t[3 * 64]; int nv = 0, nt = 0;
  auto add = [&](float x, float y, float z) { v[nv * 3] = x; v[nv * 3 + 1] = y; v[nv * 3 + 2] = z; return nv++; };
  auto quad = [&](int a, int b, int c, int d) { t[nt*3]=a; t[nt*3+1]=b; t[nt*3+2]=c; nt++; t[nt*3]=a; t[nt*3+1]=c; t[nt*3+2]=d; nt++; };
  int a = add(-10, 0, -10), b = add(10, 0, -10), c = add(10, 0, 10), d = add(-10, 0, 10);
  quad(a, d, c, b);
  float x0 = -0.5f, x1 = 0.5f, y0 = 0, y1 = 3, z0 = -10, z1 = 6;
  int p = add(x0,y0,z0), q = add(x1,y0,z0), r = add(x1,y0,z1), s = add(x0,y0,z1);
  int e = add(x0,y1,z0), f = add(x1,y1,z0), g = add(x1,y1,z1), h = add(x0,y1,z1);
  quad(e,h,g,f); quad(p,q,r,s); quad(p,e,f,q); quad(q,f,g,r); quad(r,g,h,s); quad(s,h,e,p);
  NavMeshBuildConfig cfg; cfg.agent_radius = 0.4f;
  return nm.build(v, nv, t, nt, cfg) && nm.init_query(1024);
}

uint32_t lcg(uint32_t &s) { s = s * 1664525u + 1013904223u; return s; }
Vec3 random_target(uint32_t &seed) { // zeminde rastgele nokta (duvarin uzerine dusse de nearest ile mesh'e ceker)
  float x = (float)(lcg(seed) % 1800) / 100.0f - 9.0f;
  float z = (float)(lcg(seed) % 1800) / 100.0f - 9.0f;
  return Vec3{x, 0, z};
}

void sys_nav(SystemCtx &c) { // Agent yazar
  c.world->each(mask_of(cid_agent()), [&](const ChunkView &v) {
    Agent *ag = v.col<Agent>();
    for (uint32_t i = 0; i < v.count; i++) {
      Agent &a = ag[i];
      if (a.path_i >= a.path_n) { // yeni hedef
        Vec3 tgt;
        if (!g_ctx->nav.nearest_point(random_target(a.seed), &tgt)) continue;
        a.target = tgt;
        a.path_n = g_ctx->nav.find_path(a.pos, a.target, a.path, 16);
        a.path_i = 1; // 0 = baslangic
        if (a.path_n < 2) { a.path_n = 0; continue; }
      }
      Vec3 to = a.path[a.path_i] - a.pos;
      float d = length(to);
      float step = a.speed * c.dt;
      if (d <= step) { a.pos = a.path[a.path_i]; a.path_i++; }
      else a.pos += to * (step / d);
    }
  });
}
void sys_anim(SystemCtx &c) { // AnimState yazar
  c.world->each(mask_of(cid_anim()), [&](const ChunkView &v) {
    AnimState *as = v.col<AnimState>();
    for (uint32_t i = 0; i < v.count; i++) {
      as[i].time += c.dt;
      LocalPose pose{as[i].t, as[i].r, as[i].s, kJoints};
      ClipSampler::sample(g_ctx->clip, as[i].time, pose);
    }
  });
}
void sys_phys(SystemCtx &c) { // PhysMirror yazar; Jolt adimi bu job'in icinde (fiber -> Jolt job'lari ic ice)
  g_ctx->phys.step(c.dt, 1);
  c.world->each(mask_of(cid_phys()), [&](const ChunkView &v) {
    PhysMirror *pm = v.col<PhysMirror>();
    for (uint32_t i = 0; i < v.count; i++) {
      pm[i].pos = g_ctx->phys.position(pm[i].body);
      pm[i].rot = g_ctx->phys.rotation(pm[i].body);
    }
  });
}

bool make_clip(Arena &arena, const ClipHeader **out) {
  static Vec3 t[kJoints][30]; static Quat r[kJoints][30]; static Vec3 s[kJoints][30];
  static RawTrack tracks[kJoints];
  for (uint32_t j = 0; j < kJoints; j++) {
    for (int k = 0; k < 30; k++) {
      float ph = (float)k / 30 * 2 * kPi;
      t[j][k] = Vec3{j == 0 ? 0.0f : 1.0f, 0, 0};
      r[j][k] = Quat::axis_angle({0, 0, 1}, 0.4f * std::sin(ph + j));
      s[j][k] = Vec3{1, 1, 1};
    }
    tracks[j] = RawTrack{t[j], r[j], s[j]};
  }
  RawClip raw{tracks, kJoints, 30, 30.0f};
  *out = ClipBuilder::build(arena, raw, 1e-5f, nullptr);
  return *out != nullptr;
}

// Sahneyi kur, kos, ozeti dondur. jobs: null = seri zamanlayici.
uint64_t run_scene(Arena &arena, JobSystem *jobs, uint64_t *allocs_after_warmup, uint32_t *off_mesh, float *sec,
                   float *box_y_min, float *box_y_max) {
  static SceneCtx ctx; // Jolt/navmesh statik; her kosumda yeniden kurulur
  g_ctx = &ctx;
  if (!build_nav(ctx.nav)) return 0;
  PhysicsConfig pc; pc.jobs = jobs;
  if (!ctx.phys.init(arena, pc)) return 0;
  ctx.phys.add_box({50, 1, 50}, {0, -1, 0}, Quat::identity(), false);
  if (!make_clip(arena, &ctx.clip)) return 0;
  World w;
  WorldConfig wc; wc.max_entities = 256; wc.max_chunks = 32;
  if (!w.init(arena, wc)) return 0;
  for (uint32_t i = 0; i < kAgents; i++) {
    Entity e = w.create(mask_of(cid_agent()) | mask_of(cid_anim()));
    Agent *a = w.get<Agent>(e);
    a->seed = 1000 + i * 7919;
    a->speed = 1.5f + (i % 4) * 0.5f;
    Vec3 start{-8.0f + (float)(i % 4) * 0.7f, 0, -8.0f + (float)(i / 4) * 0.7f};
    ctx.nav.nearest_point(start, &a->pos);
    a->path_n = 0; a->path_i = 0;
    w.get<AnimState>(e)->time = i * 0.1f;
  }
  for (uint32_t i = 0; i < kBoxes; i++) {
    Entity e = w.create(mask_of(cid_phys()));
    PhysMirror *pm = w.get<PhysMirror>(e);
    pm->body = ctx.phys.add_box({0.4f, 0.4f, 0.4f}, {3.0f + (i % 5) * 0.9f, 2.0f + (i / 5) * 1.0f, -6.0f + (i % 3) * 0.9f},
                                Quat::identity(), true);
  }
  Schedule s;
  if (!s.init(arena, jobs)) return 0;
  s.add(SystemDesc{"nav", sys_nav, 0, mask_of(cid_agent())});
  s.add(SystemDesc{"anim", sys_anim, 0, mask_of(cid_anim())});
  s.add(SystemDesc{"phys", sys_phys, 0, mask_of(cid_phys())});
  s.build();
  uint64_t worst = 0;
  uint64_t t0 = platform::now_ns();
  for (uint32_t t = 0; t < kTicks; t++) {
    AllocGate::begin_frame();
    s.run(w, ctx.dt, nullptr, t);
    uint64_t a = AllocGate::end_frame();
    if (t >= 30 && a > worst) worst = a;
  }
  if (sec) *sec = (platform::now_ns() - t0) / 1e9f;
  if (allocs_after_warmup) *allocs_after_warmup = worst;
  // Ajanlar navmesh'te mi? (en yakin nokta 0.3'ten yakin olmali)
  uint32_t off = 0;
  w.each(mask_of(cid_agent()), [&](const ChunkView &v) {
    const Agent *ag = v.col<Agent>();
    for (uint32_t i = 0; i < v.count; i++) {
      Vec3 np;
      if (!ctx.nav.nearest_point(ag[i].pos, &np) || length(np - ag[i].pos) > 0.3f) off++;
    }
  });
  if (off_mesh) *off_mesh = off;
  // Fizik gercekten kostu mu? Kutular dusup zemine/ustuste oturmus olmali.
  float ymin = 1e9f, ymax = -1e9f;
  w.each(mask_of(cid_phys()), [&](const ChunkView &v) {
    const PhysMirror *pm = v.col<PhysMirror>();
    for (uint32_t i = 0; i < v.count; i++) { if (pm[i].pos.y < ymin) ymin = pm[i].pos.y; if (pm[i].pos.y > ymax) ymax = pm[i].pos.y; }
  });
  if (box_y_min) *box_y_min = ymin;
  if (box_y_max) *box_y_max = ymax;
  uint64_t h = w.content_hash() ^ (ctx.phys.state_hash() * 0x9E3779B97F4A7C15ull);
  ctx.phys.shutdown();
  ctx.nav.shutdown();
  return h;
}
} // namespace

ENGINE_TEST(faz2_scene_walks_animates_and_falls_deterministically) {
  static SystemArena a1, a2;
  CHECK(a1.reserve(32u << 20, "sc1") && a2.reserve(32u << 20, "sc2"));
  uint64_t allocs_s = 0, allocs_p = 0;
  uint32_t off_s = 0, off_p = 0;
  float sec_s = 0, sec_p = 0;
  float ymin = 0, ymax = 0;
  uint64_t hs = run_scene(a1, nullptr, &allocs_s, &off_s, &sec_s, &ymin, &ymax);
  CHECK(ymin > 0.3f && ymin < 0.5f); // en alttaki kutu zeminde (yarim kenar 0.4)
  CHECK(ymax < 6.0f);                // hepsi dustu (baslangic 2..5)
  CHECK(hs != 0);
  static SystemArena jsys;
  CHECK(jsys.reserve(8u << 20, "sc-js"));
  JobSystem js;
  CHECK(js.init(jsys, JobSystemConfig{}));
  uint64_t hp = run_scene(a2, &js, &allocs_p, &off_p, &sec_p, nullptr, nullptr);
  js.shutdown();
  CHECK(hp == hs); // seri = paralel (Jolt fiber job'larda, sistemler ayni asamada)
  CHECK(allocs_s == 0 && allocs_p == 0); // tick icinde operator new yok (Jolt/Detour malloc kancali, sayilmaz)
  CHECK(off_s == 0 && off_p == 0);       // ajanlar navmesh'te
  std::printf("    [bilgi] %u tick, %u ajan + %u kutu + %u eklem/ajan; seri %.1f ms, paralel %.1f ms; kutu y %.2f..%.2f; ozet %016llx\n",
              kTicks, kAgents, kBoxes, kJoints, sec_s * 1000, sec_p * 1000, ymin, ymax, (unsigned long long)hs);
}
