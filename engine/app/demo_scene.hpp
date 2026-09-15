// L5 APP — Faz 2 entegre sahnesi, pencereli/headless demo icin: navmesh
// ajanlari + Jolt kutulari + animasyonlu eklem zinciri, ECS + zamanlayici.
// (tests/test_scene_faz2.cpp ile ayni kurulum; burada cizimi de var.)
#pragma once
#include <cstdint>

#include "core/jobs/job_system.hpp"
#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "sim/animation.hpp"
#include "sim/ecs.hpp"
#include "sim/navmesh.hpp"
#include "sim/physics.hpp"
#include "sim/schedule.hpp"

namespace tulpar::engine::app {

class DemoScene {
public:
  static constexpr uint32_t kAgents = 24, kBoxes = 40, kJoints = 4;
  bool init(Arena &arena, JobSystem *jobs);
  void shutdown();
  void tick(float dt, uint32_t tick_index);
  struct DrawSet {
    renderer::MeshHandle cube, plane;
    renderer::MaterialHandle ground;   // dama zemin
    renderer::MeshHandle box_mesh;     // glTF kup (varsa), yoksa cube
    renderer::MaterialHandle box_mat;  // glTF malzemesi (varsa)
  };
  // Cizim listesine ekler (renderer.begin_frame sonra, record oncesi).
  void draw(renderer::Renderer &r, const DrawSet &d);
  uint64_t content_hash() const;
  uint32_t entities() const { return world_.stats().entities; }
  sim::Physics &physics() { return phys_; } // editor: sahne govdeleri ayni dunyaya
  const sim::Physics &physics() const { return phys_; }
  // Oyuncu: dinamik Jolt kutusu. Komut karede latch'lenir, her tick uygulanir
  // (deterministik: ayni komut dizisi = ayni ozet).
  void set_player_command(Vec2 move_world_xz, bool jump);
  Vec3 player_position() const;

private:
  sim::World world_;
  sim::Schedule sched_;
  sim::NavMesh nav_;
  sim::Physics phys_;
  const sim::ClipHeader *clip_ = nullptr;
  sim::BodyId player_{};
  Vec2 player_cmd_{0, 0};
  bool player_jump_ = false;
  float player_speed_ = 5.0f;
  sim::Joint joints_[kJoints];
  friend void demo_sys_nav(sim::SystemCtx &);
  friend void demo_sys_anim(sim::SystemCtx &);
  friend void demo_sys_phys(sim::SystemCtx &);
};

} // namespace tulpar::engine::app
