// L5 APP — Faz 2 entegre sahnesi, pencereli/headless demo icin: navmesh
// ajanlari (artik sim/behavior_tree.hpp ile "oyuncu yakinsa kovala, yoksa
// devriye gez" kararı veriyor) + Jolt kutulari + animasyonlu eklem zinciri
// + ziplama tozu (content/particles.hpp) + ECS/zamanlayici. Bu dosya,
// motorun izole test edilmis modullerinin GERCEKTEN BIR ARADA, tek bir
// oynanabilir sahnede calistigini gosteren yer -- yalniz "bu fonksiyon
// dogru sonuc veriyor" degil, "bu parcalar birlikte bir OYUN olusturuyor".
// (tests/test_scene_faz2.cpp ile ayni kurulum; burada cizimi de var.)
#pragma once
#include <cstdint>

#include "content/particles.hpp"
#include "core/jobs/job_system.hpp"
#include "core/math/random.hpp"
#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "sim/animation.hpp"
#include "sim/behavior_tree.hpp"
#include "sim/ecs.hpp"
#include "sim/navmesh.hpp"
#include "sim/physics.hpp"
#include "sim/schedule.hpp"

namespace tulpar::engine::app {

class DemoScene {
public:
  static constexpr uint32_t kAgents = 24, kBoxes = 40, kJoints = 4;
  // with_content: demo ICERIGINI (arena zemini + duvarlari, oyuncu kutusu,
  // ajanlar, dusen kutular, parcaciklar, davranis agaci, zamanlayici)
  // kurar mi. false verilirse YALNIZ fizik dunyasi ve navmesh kurulur.
  //
  // Neden gerekli: editor bu siniftan SADECE bir sim::Physics dunyasi
  // aliyordu ama demo icerigi de o dunyaya 6 sabit kutu + 1 dinamik
  // govde ekliyordu. Bunlar sahne dosyasina ait DEGIL, seciliemez,
  // kaydedilemez -- ama kullanicinin dinamik govdelerini gorunmez bir
  // zeminde tutuyordu. Ayni sahne derlenip oynatildiginda (scene_runtime)
  // o zemin YOK, yani editor ile oyun FARKLI davraniyordu.
  bool init(Arena &arena, JobSystem *jobs, bool with_content = true);
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
  // PR #322'nin davranis agaci (bt_wander) navmesh'i SINIF DISINDAN okuyor.
  // `friend` ile cozulemez: o fonksiyon .cpp'nin isimsiz ad alaninda yasiyor
  // ve baslikta adlandirilamaz. Mevcut `physics()` kalibi zaten erisimci,
  // navmesh de ayni sekilde aciliyor (salt okunur sorgu).
  sim::NavMesh &nav() { return nav_; }
  const sim::NavMesh &nav() const { return nav_; }
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
  // Ajan AI: TEK PAYLASILAN agac tanimi (agent_bt_), calisma-zamani durumu
  // her Agent'in KENDI bt_state[] dizisinde -- sim/behavior_tree.hpp'nin
  // dokumante ettigi TAM olarak bu kullanim sekli ("yuzlerce ajan arasinda
  // bellek cogaltmadan paylasilabilir").
  sim::BehaviorTree agent_bt_;
  // Toz parcaciklari (ziplama efekti): content/particles.hpp -- dogum-ani
  // rastgeleligi SABIT tohumlu Rng ile, bu yuzden content_hash()'e KATILMASA
  // da (kozmetik, oynanis DURUMU tasimiyor) ayni girdi dizisiyle HER
  // platformda ayni gorunur.
  content::ParticleSystem particles_;
  Rng particle_rng_{0x50415254u}; // "PART" -- sabit, anlamli tohum
  friend void demo_sys_nav(sim::SystemCtx &);
  friend void demo_sys_anim(sim::SystemCtx &);
  friend void demo_sys_phys(sim::SystemCtx &);
};

} // namespace tulpar::engine::app
