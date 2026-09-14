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
  // Cizim listesine ekler (renderer.begin_frame sonra, record oncesi).
  void draw(renderer::Renderer &r, renderer::MeshHandle cube, renderer::MeshHandle plane);
  uint64_t content_hash() const;
  uint32_t entities() const { return world_.stats().entities; }

private:
  sim::World world_;
  sim::Schedule sched_;
  sim::NavMesh nav_;
  sim::Physics phys_;
  const sim::ClipHeader *clip_ = nullptr;
  sim::Joint joints_[kJoints];
  friend void demo_sys_nav(sim::SystemCtx &);
  friend void demo_sys_anim(sim::SystemCtx &);
  friend void demo_sys_phys(sim::SystemCtx &);
};

} // namespace tulpar::engine::app
