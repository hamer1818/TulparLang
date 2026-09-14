// Faz 2: Jolt sarmalayicisi — dusen kutular, belirlenimlilik (ayni surec iki
// kosum + ALTIN ozet: platformlar arasi bit esitligi iddiasi CI'da
// Linux x86_64 <-> macOS arm64 ile sinanir), adim icinde ayirma.
#include <cstdio>
#include <cstdlib>

#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "platform/time.hpp"
#include "sim/physics.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
// 4x4x4 kutu yigini zemine dusuyor; 300 adim @ 60 Hz.
uint64_t run_scene(Physics &ph, uint32_t threads, uint64_t *step_allocs_after_warmup, Vec3 *first_box_pos,
                   JobSystem *fiber_jobs = nullptr, uint64_t *elapsed_ns = nullptr) {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(16u << 20, "phys");
  PhysicsConfig cfg;
  cfg.threads = threads;
  cfg.jobs = fiber_jobs;
  uint64_t t0 = platform::now_ns();
  if (!ph.init(sys, cfg)) return 0;
  ph.add_box({50, 1, 50}, {0, -1, 0}, Quat::identity(), false); // zemin
  BodyId first{};
  for (int x = 0; x < 4; x++)
    for (int y = 0; y < 4; y++)
      for (int z = 0; z < 4; z++) {
        // Baslangic donusu SABIT bitlerle: axis_angle sin/cos (libm) kullanir ve
        // glibc ile Apple libm son ulp'ta farkli olabilir — o zaman altin ozet
        // Jolt'u degil libm'i olcer (Tuzaklar 8d). Dort donus, elle normalize.
        static const Quat kRot[4] = {{0, 0, 0, 1}, {0, 0.0998334f, 0, 0.9950042f},
                                     {0, 0.1986693f, 0, 0.9800666f}, {0, 0.2955202f, 0, 0.9553365f}};
        BodyId b = ph.add_box({0.5f, 0.5f, 0.5f}, {x * 1.1f - 1.65f, 2.0f + y * 1.2f, z * 1.1f - 1.65f + x * 0.05f},
                              kRot[x], true);
        if (!first.valid()) first = b;
      }
  uint64_t worst = 0;
  for (int i = 0; i < 300; i++) {
    ph.step(1.0f / 60.0f, 1);
    if (i >= 60 && ph.stats().allocs_last_step > worst) worst = ph.stats().allocs_last_step;
  }
  if (step_allocs_after_warmup) *step_allocs_after_warmup = worst;
  if (first_box_pos) *first_box_pos = ph.position(first);
  uint64_t h = ph.state_hash();
  ph.shutdown();
  if (elapsed_ns) *elapsed_ns = platform::now_ns() - t0;
  return h;
}
} // namespace

ENGINE_TEST(physics_boxes_settle_on_floor) {
  Physics ph;
  uint64_t allocs = 0;
  Vec3 p;
  uint64_t h = run_scene(ph, 0, &allocs, &p);
  CHECK(h != 0);
  // Kutular zemine oturmus olmali: y ~ 0.5 (yarim kenar) ile ~4.5 (yiginin ustu) arasi
  CHECK(p.y > 0.3f && p.y < 6.0f);
  CHECK(p.x > -30 && p.x < 30 && p.z > -30 && p.z < 30);
  std::printf("    [bilgi] 65 govde, 300 adim; ilk kutu y=%.3f; adim ici Jolt ayirmasi (isinma sonrasi en cok)=%llu\n",
              p.y, (unsigned long long)allocs);
  // A2: Jolt adim icinde KUCUK ve SINIRLI ayirma yapiyor (olculdu 2026-09-14:
  // 300 adimda 76 x (256 B + 1024 B) cifti, ~her 3 adimda bir; kaynak henuz
  // bulunmadi — FAZ2.md acik is). Ucuncu parti: surucu gibi olculur,
  // SINIRLI ve kararli oldugu iddia edilir; sifir iddiasi bizim koda ait.
  CHECK(allocs <= 2);
}

// FAZ 2 KAPISI (fizik): ayni sahne, ayni surec, farkli thread sayisi -> ayni ozet.
ENGINE_TEST(faz2_gate_physics_is_deterministic_across_thread_counts) {
  Physics a, b, c;
  uint64_t h1 = run_scene(a, 1, nullptr, nullptr);
  uint64_t h2 = run_scene(b, 4, nullptr, nullptr);
  uint64_t h3 = run_scene(c, 0, nullptr, nullptr);
  CHECK(h1 != 0 && h1 == h2 && h2 == h3);
  std::printf("    [bilgi] fizik ozeti %016llx (1, 4 ve otomatik thread: ayni)\n", (unsigned long long)h1);
}

// PLATFORMLAR ARASI iddia (JPH_CROSS_PLATFORM_DETERMINISTIC): altin ozet
// x86_64'te (SSE4.2, FMA yok) hesaplandi; macOS arm64 (NEON) CI'da AYNI cikmali.
// Cikmazsa bu bir bulgu: iddia bayraklarimizla tutmuyor, PLAN.md REV 8 gecerli.
ENGINE_TEST(physics_cross_platform_golden_hash) {
  Physics ph;
  uint64_t h = run_scene(ph, 1, nullptr, nullptr);
  const uint64_t golden = PHYSICS_GOLDEN_HASH;
  if (golden == 0) {
    std::printf("    [bilgi] altin ozet henuz yok; bu makinede: %016llx\n", (unsigned long long)h);
    return;
  }
  if (h != golden)
    std::printf("    [bilgi] platformlar arasi FARK: bu makine %016llx, altin %016llx\n", (unsigned long long)h,
                (unsigned long long)golden);
  CHECK(h == golden);
}

// Jolt job'lari BIZIM fiber job sisteminde: ayni ozet, ayirma sinirli, sure bilgi.
ENGINE_TEST(physics_runs_on_fiber_job_system_same_hash) {
  static SystemArena jsys;
  CHECK(jsys.reserve(8u << 20, "phys-js"));
  JobSystem js;
  JobSystemConfig jc;
  // Jolt'un carpisma job'lari yigin-ac (ProcessBodyPair: buyuk yerel
  // yapilar). 64 KB fiber yigini bekci sayfasina carpip SIGSEGV verdi
  // (olculdu 2026-09-14, gdb: ProcessBodyPair). Gereken boyut asagida
  // olculdu; TULPAR_ENGINE_FIBER_STACK_KB ile denenebilir.
  const char *kb = std::getenv("TULPAR_ENGINE_FIBER_STACK_KB");
  if (kb) jc.fiber_stack_bytes = (uint32_t)std::atoi(kb) * 1024u; // varsayilan 256 KB (job_system.hpp)
  CHECK(js.init(jsys, jc));
  Physics a, b;
  uint64_t t_pool = 0, t_fiber = 0, allocs = 0;
  uint64_t h_pool = run_scene(a, 0, nullptr, nullptr, nullptr, &t_pool);
  uint64_t h_fiber = run_scene(b, 0, &allocs, nullptr, &js, &t_fiber);
  CHECK(h_fiber != 0 && h_fiber == h_pool);
  CHECK(allocs <= 2);
  std::printf("    [bilgi] Jolt havuzu %.1f ms, fiber job sistemi %.1f ms (300 adim); ozet ayni\n",
              t_pool / 1e6, t_fiber / 1e6);
  js.shutdown();
}

