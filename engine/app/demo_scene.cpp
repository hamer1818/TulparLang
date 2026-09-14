#include "app/demo_scene.hpp"

#include <cmath>

namespace tulpar::engine::app {

using namespace tulpar::engine::sim;

namespace {
struct Agent { Vec3 pos; Vec3 target; Vec3 path[16]; int path_n; int path_i; uint32_t seed; float speed; float hue; };
struct AnimState { float time; Vec3 t[DemoScene::kJoints]; Quat r[DemoScene::kJoints]; Vec3 s[DemoScene::kJoints]; };
struct PhysMirror { BodyId body; Vec3 pos; Quat rot; float hue; };
ComponentId cid_agent() { return component_id<Agent>("DemoAgent"); }
ComponentId cid_anim() { return component_id<AnimState>("DemoAnim"); }
ComponentId cid_phys() { return component_id<PhysMirror>("DemoPhys"); }
DemoScene *g_scene = nullptr;

uint32_t lcg(uint32_t &s) { s = s * 1664525u + 1013904223u; return s; }
Vec3 random_target(uint32_t &seed) {
  float x = (float)(lcg(seed) % 1800) / 100.0f - 9.0f;
  float z = (float)(lcg(seed) % 1800) / 100.0f - 9.0f;
  return Vec3{x, 0, z};
}
Vec3 hue_color(float h) { // 0..1 -> canli renk
  float r = std::fabs(h * 6 - 3) - 1, g = 2 - std::fabs(h * 6 - 2), b = 2 - std::fabs(h * 6 - 4);
  auto c = [](float v) { return v < 0 ? 0.f : v > 1 ? 1.f : v; };
  return Vec3{c(r), c(g), c(b)};
}

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
  return nm.build(v, nv, t, nt, cfg) && nm.init_query(2048);
}
} // namespace

void demo_sys_nav(SystemCtx &c) {
  DemoScene *sc = g_scene;
  c.world->each(mask_of(cid_agent()), [&](const ChunkView &v) {
    Agent *ag = v.col<Agent>();
    for (uint32_t i = 0; i < v.count; i++) {
      Agent &a = ag[i];
      if (a.path_i >= a.path_n) {
        Vec3 tgt;
        if (!sc->nav_.nearest_point(random_target(a.seed), &tgt)) continue;
        a.target = tgt;
        a.path_n = sc->nav_.find_path(a.pos, a.target, a.path, 16);
        a.path_i = 1;
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
void demo_sys_anim(SystemCtx &c) {
  DemoScene *sc = g_scene;
  c.world->each(mask_of(cid_anim()), [&](const ChunkView &v) {
    AnimState *as = v.col<AnimState>();
    for (uint32_t i = 0; i < v.count; i++) {
      as[i].time += c.dt;
      LocalPose pose{as[i].t, as[i].r, as[i].s, DemoScene::kJoints};
      ClipSampler::sample(sc->clip_, as[i].time, pose);
    }
  });
}
void demo_sys_phys(SystemCtx &c) {
  DemoScene *sc = g_scene;
  // Oyuncu komutu: yatay hiz dogrudan, dusey (yercekimi) korunur; zipla = dusey darbe.
  if (sc->player_.valid()) {
    Vec3 v = sc->phys_.linear_velocity(sc->player_);
    v.x = sc->player_cmd_.x * sc->player_speed_;
    v.z = sc->player_cmd_.y * sc->player_speed_;
    if (sc->player_jump_ && std::fabs(v.y) < 0.05f) v.y = 5.5f;
    sc->player_jump_ = false;
    sc->phys_.set_linear_velocity(sc->player_, v);
  }
  sc->phys_.step(c.dt, 1);
  c.world->each(mask_of(cid_phys()), [&](const ChunkView &v) {
    PhysMirror *pm = v.col<PhysMirror>();
    for (uint32_t i = 0; i < v.count; i++) {
      pm[i].pos = sc->phys_.position(pm[i].body);
      pm[i].rot = sc->phys_.rotation(pm[i].body);
    }
  });
}

bool DemoScene::init(Arena &arena, JobSystem *jobs) {
  g_scene = this;
  if (!build_nav(nav_)) return false;
  PhysicsConfig pc; pc.jobs = jobs;
  if (!phys_.init(arena, pc)) return false;
  // Zemin gorsel arena kadar (20x20) + gorunmez kenar duvarlari: oyuncu ve
  // kutular arenayi terk edemez (telefonda olculdu: oyuncu -25 m'ye yurudu).
  phys_.add_box({10, 1, 10}, {0, -1, 0}, Quat::identity(), false);
  phys_.add_box({0.5f, 3, 11}, {-10.5f, 3, 0}, Quat::identity(), false);
  phys_.add_box({0.5f, 3, 11}, {10.5f, 3, 0}, Quat::identity(), false);
  phys_.add_box({11, 3, 0.5f}, {0, 3, -10.5f}, Quat::identity(), false);
  phys_.add_box({11, 3, 0.5f}, {0, 3, 10.5f}, Quat::identity(), false);
  phys_.add_box({0.5f, 1.5f, 8}, {0, 1.5f, -2}, Quat::identity(), false); // gorsel duvar (nav engeli) fizikte de
  player_ = phys_.add_box({0.4f, 0.5f, 0.4f}, {-4.0f, 0.6f, 4.0f}, Quat::identity(), true);
  // Klip: 4 eklemli sallanan zincir
  static Vec3 t[kJoints][30]; static Quat r[kJoints][30]; static Vec3 s[kJoints][30];
  static RawTrack tracks[kJoints];
  for (uint32_t j = 0; j < kJoints; j++) {
    joints_[j].parent = j == 0 ? -1 : (int16_t)(j - 1);
    for (int k = 0; k < 30; k++) {
      float ph = (float)k / 30 * 2 * kPi;
      t[j][k] = Vec3{0, j == 0 ? 0.0f : 0.35f, 0};
      r[j][k] = Quat::axis_angle({0, 0, 1}, 0.45f * std::sin(ph + j * 0.9f));
      s[j][k] = Vec3{1, 1, 1};
    }
    tracks[j] = RawTrack{t[j], r[j], s[j]};
  }
  RawClip raw{tracks, kJoints, 30, 30.0f};
  clip_ = ClipBuilder::build(arena, raw, 1e-5f, nullptr);
  if (!clip_) return false;
  WorldConfig wc; wc.max_entities = 512; wc.max_chunks = 64;
  if (!world_.init(arena, wc)) return false;
  for (uint32_t i = 0; i < kAgents; i++) {
    Entity e = world_.create(mask_of(cid_agent()) | mask_of(cid_anim()));
    Agent *a = world_.get<Agent>(e);
    a->seed = 1000 + i * 7919;
    a->speed = 1.5f + (i % 4) * 0.5f;
    a->hue = (float)i / kAgents;
    Vec3 start{-8.0f + (float)(i % 6) * 0.8f, 0, -8.0f + (float)(i / 6) * 0.8f};
    nav_.nearest_point(start, &a->pos);
    a->path_n = 0; a->path_i = 0;
    world_.get<AnimState>(e)->time = i * 0.1f;
  }
  for (uint32_t i = 0; i < kBoxes; i++) {
    Entity e = world_.create(mask_of(cid_phys()));
    PhysMirror *pm = world_.get<PhysMirror>(e);
    pm->hue = 0.05f + 0.1f * (i % 3);
    pm->body = phys_.add_box({0.4f, 0.4f, 0.4f}, {3.0f + (i % 5) * 0.9f, 2.0f + (i / 5) * 1.0f, -6.0f + (i % 3) * 0.9f + (i / 15) * 3.0f},
                             Quat::identity(), true);
  }
  if (!sched_.init(arena, jobs)) return false;
  sched_.add(SystemDesc{"nav", demo_sys_nav, 0, mask_of(cid_agent())});
  sched_.add(SystemDesc{"anim", demo_sys_anim, 0, mask_of(cid_anim())});
  sched_.add(SystemDesc{"phys", demo_sys_phys, 0, mask_of(cid_phys())});
  sched_.build();
  return true;
}

void DemoScene::shutdown() {
  phys_.shutdown();
  nav_.shutdown();
}

void DemoScene::tick(float dt, uint32_t tick_index) { sched_.run(world_, dt, nullptr, tick_index); }

void DemoScene::set_player_command(Vec2 move, bool jump) {
  player_cmd_ = move;
  if (jump) player_jump_ = true;
}
Vec3 DemoScene::player_position() const { return player_.valid() ? phys_.position(player_) : Vec3{0, 0, 0}; }

uint64_t DemoScene::content_hash() const { return world_.content_hash() ^ (phys_.state_hash() * 0x9E3779B97F4A7C15ull); }

void DemoScene::draw(renderer::Renderer &r, const DrawSet &d) {
  const renderer::MeshHandle cube = d.cube, plane = d.plane;
  r.draw(plane, d.ground, Mat4::scale({20, 1, 20}), {0.62f, 0.64f, 0.67f});
  r.draw(cube, Mat4::translate({0, 1.5f, -2}) * Mat4::scale({1, 3, 16}), {0.55f, 0.5f, 0.45f});
  Skeleton sk{joints_, kJoints};
  if (player_.valid()) // oyuncu: beyaz kutu, hafif buyuk
    r.draw(cube, Mat4::translate(phys_.position(player_)) * to_mat4(phys_.rotation(player_)) * Mat4::scale({0.8f, 1.0f, 0.8f}), {0.95f, 0.95f, 0.9f});
  const renderer::MeshHandle bm = d.box_mesh.valid() ? d.box_mesh : cube;
  world_.each(mask_of(cid_phys()), [&](const ChunkView &v) {
    const PhysMirror *pm = v.col<PhysMirror>();
    for (uint32_t i = 0; i < v.count; i++) {
      Mat4 m = Mat4::translate(pm[i].pos) * to_mat4(pm[i].rot) * Mat4::scale({0.8f, 0.8f, 0.8f});
      if (d.box_mat.valid()) r.draw(bm, d.box_mat, m, hue_color(pm[i].hue) * 0.5f + Vec3{0.5f, 0.5f, 0.5f});
      else r.draw(bm, m, hue_color(pm[i].hue));
    }
  });
  world_.each(mask_of(cid_agent()) | mask_of(cid_anim()), [&](const ChunkView &v) {
    const Agent *ag = v.col<Agent>();
    const AnimState *as = v.col<AnimState>();
    for (uint32_t i = 0; i < v.count; i++) {
      Vec3 col = hue_color(ag[i].hue);
      r.draw(cube, Mat4::translate(ag[i].pos + Vec3{0, 0.3f, 0}) * Mat4::scale({0.6f, 0.6f, 0.6f}), col);
      LocalPose pose{const_cast<Vec3 *>(as[i].t), const_cast<Quat *>(as[i].r), const_cast<Vec3 *>(as[i].s), kJoints};
      Mat4 m[kJoints];
      ClipSampler::to_model(sk, pose, m);
      Mat4 base = Mat4::translate(ag[i].pos + Vec3{0, 0.75f, 0});
      for (uint32_t j = 0; j < kJoints; j++)
        r.draw(cube, base * m[j] * Mat4::scale({0.18f, 0.18f, 0.18f}), col * 0.8f + Vec3{0.2f, 0.2f, 0.2f});
    }
  });
}

} // namespace tulpar::engine::app
