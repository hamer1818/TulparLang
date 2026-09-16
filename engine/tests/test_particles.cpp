// content/particles.hpp: hareket entegrasyonu ELLE iz surulerek (0.1s
// adimlarla, sabit yercekimi), takas-ile-silmenin DIGER canli parcaciklari
// BOZMADIGI, boyut-omur enterpolasyonunun tam degerleri, VE hiz jitter'inin
// hem SINIRLI hem de AYNI tohumla TEKRARLANABILIR (determinizm) oldugu
// kanitlanir.
#include "content/particles.hpp"

#include "core/memory/arena.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

ENGINE_TEST(particles_emit_stops_silently_at_capacity) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "particles_cap"));
  ParticleSystem ps;
  CHECK(ps.init(sys, 2, Vec3{0, 0, 0}));

  ParticleEmitterConfig cfg;
  cfg.spawn_pos = Vec3{1, 2, 3};
  cfg.lifetime_min = cfg.lifetime_max = 1.0f;
  Rng rng(1234);

  CHECK(ps.emit(cfg, 3, rng) == 2); // kapasite 2 -- ucuncusu SESSIZCE kesildi
  CHECK(ps.alive_count() == 2);
  CHECK(nearly_equal(ps.particle(0).pos, Vec3{1, 2, 3}));
  CHECK(nearly_equal(ps.particle(0).lifetime, 1.0f));
}

ENGINE_TEST(particles_gravity_integration_matches_hand_trace) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "particles_grav"));
  ParticleSystem ps;
  CHECK(ps.init(sys, 1, Vec3{0, -10.0f, 0}));

  ParticleEmitterConfig cfg;
  cfg.base_velocity = Vec3{0, 5, 0};
  cfg.lifetime_min = cfg.lifetime_max = 0.25f;
  Rng rng(42);
  CHECK(ps.emit(cfg, 1, rng) == 1);

  // adim 1: vel=(0,5,0)+(0,-10,0)*0.1=(0,4,0); pos=(0,0,0)+(0,4,0)*0.1=(0,0.4,0); age=0.1<0.25
  ps.update(0.1f);
  CHECK(ps.alive_count() == 1);
  CHECK(nearly_equal(ps.particle(0).vel, Vec3{0, 4.0f, 0}));
  CHECK(nearly_equal(ps.particle(0).pos, Vec3{0, 0.4f, 0}));

  // adim 2: vel=(0,3,0); pos=(0,0.7,0); age=0.2<0.25
  ps.update(0.1f);
  CHECK(ps.alive_count() == 1);
  CHECK(nearly_equal(ps.particle(0).vel, Vec3{0, 3.0f, 0}));
  CHECK(nearly_equal(ps.particle(0).pos, Vec3{0, 0.7f, 0}));

  // adim 3: age=0.3>=0.25 -> olur, havuzdan cikar
  ps.update(0.1f);
  CHECK(ps.alive_count() == 0);
}

ENGINE_TEST(particles_swap_removal_preserves_surviving_particles) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "particles_swap"));
  ParticleSystem ps;
  CHECK(ps.init(sys, 3, Vec3{0, 0, 0})); // yercekimi yok -- konum sabit kalmali

  Rng rng(7);
  ParticleEmitterConfig cfg_a;
  cfg_a.spawn_pos = Vec3{1, 0, 0};
  cfg_a.lifetime_min = cfg_a.lifetime_max = 1.0f;
  CHECK(ps.emit(cfg_a, 1, rng) == 1); // index 0

  ParticleEmitterConfig cfg_b;
  cfg_b.spawn_pos = Vec3{2, 0, 0};
  cfg_b.lifetime_min = cfg_b.lifetime_max = 0.05f; // kisa omurlu -- ilk update'te olecek
  CHECK(ps.emit(cfg_b, 1, rng) == 1); // index 1

  ParticleEmitterConfig cfg_c;
  cfg_c.spawn_pos = Vec3{3, 0, 0};
  cfg_c.lifetime_min = cfg_c.lifetime_max = 1.0f;
  CHECK(ps.emit(cfg_c, 1, rng) == 1); // index 2

  ps.update(0.1f); // b (index1) olur -> c (index2) index1'e TAKAS edilir
  CHECK(ps.alive_count() == 2);
  // a hic tasinmadi (index0), c takasla index1'e geldi -- ikisi de BOZULMAMIS olmali.
  CHECK(nearly_equal(ps.particle(0).pos, Vec3{1, 0, 0}));
  CHECK(nearly_equal(ps.particle(1).pos, Vec3{3, 0, 0}));
}

ENGINE_TEST(particles_size_interpolates_linearly_over_life) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "particles_size"));
  ParticleSystem ps;
  CHECK(ps.init(sys, 1, Vec3{0, 0, 0}));

  ParticleEmitterConfig cfg;
  cfg.lifetime_min = cfg.lifetime_max = 1.0f;
  cfg.size_start = 2.0f;
  cfg.size_end = 10.0f;
  Rng rng(5);
  CHECK(ps.emit(cfg, 1, rng) == 1);
  CHECK(nearly_equal(ps.particle(0).size, 2.0f)); // dogum ani -- size_start

  ps.update(0.25f); // t=0.25 -> 2 + (10-2)*0.25 = 4
  CHECK(nearly_equal(ps.particle(0).size, 4.0f));

  ps.update(0.25f); // t=0.5 -> 2 + 8*0.5 = 6
  CHECK(nearly_equal(ps.particle(0).size, 6.0f));
}

ENGINE_TEST(particles_velocity_jitter_stays_within_configured_bounds) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "particles_jitter"));
  ParticleSystem ps;
  CHECK(ps.init(sys, 32, Vec3{0, 0, 0}));

  ParticleEmitterConfig cfg;
  cfg.velocity_jitter = Vec3{1.0f, 1.0f, 1.0f};
  cfg.lifetime_min = 0.5f;
  cfg.lifetime_max = 1.5f;
  Rng rng(777);
  CHECK(ps.emit(cfg, 32, rng) == 32);

  for (uint32_t i = 0; i < 32; i++) {
    const Particle &p = ps.particle(i);
    CHECK(p.vel.x >= -1.0f && p.vel.x <= 1.0f);
    CHECK(p.vel.y >= -1.0f && p.vel.y <= 1.0f);
    CHECK(p.vel.z >= -1.0f && p.vel.z <= 1.0f);
    CHECK(p.lifetime >= 0.5f && p.lifetime < 1.5f);
  }
}

ENGINE_TEST(particles_same_seed_yields_bit_identical_emit_sequence) {
  // Bu, VFX'in EK KOD OLMADAN replay/rollback ile uyumlu olmasinin
  // TEK gerekcesi: ayni tohum + ayni cagri sirasi -> ayni sonuc.
  SystemArena sys1;
  CHECK(sys1.reserve(1u << 16, "particles_det1"));
  SystemArena sys2;
  CHECK(sys2.reserve(1u << 16, "particles_det2"));
  ParticleSystem ps1, ps2;
  CHECK(ps1.init(sys1, 8, Vec3{0, -9.8f, 0}));
  CHECK(ps2.init(sys2, 8, Vec3{0, -9.8f, 0}));

  ParticleEmitterConfig cfg;
  cfg.base_velocity = Vec3{0, 2, 0};
  cfg.velocity_jitter = Vec3{0.5f, 0.5f, 0.5f};
  cfg.lifetime_min = 1.0f;
  cfg.lifetime_max = 2.0f;
  Rng rng1(999), rng2(999); // AYNI tohum

  CHECK(ps1.emit(cfg, 8, rng1) == 8);
  CHECK(ps2.emit(cfg, 8, rng2) == 8);
  for (uint32_t i = 0; i < 8; i++) {
    CHECK(nearly_equal(ps1.particle(i).vel, ps2.particle(i).vel));
    CHECK(nearly_equal(ps1.particle(i).lifetime, ps2.particle(i).lifetime));
  }
}
