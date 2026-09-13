// FAZ 0 KAPISI: "karede ayirma sayisi = 0, DOGRULANMIS" (plan §7 Faz 0).
// Bos pencere yerine headless kare dongusu: arena + job + profiler bir arada,
// 240 kare, her karede AllocGate penceresi. Pencere Faz 1'in (Vulkan yuzeyi)
// isi; pencere acilmadan da bu iddia olculebilir (pencere acmak yasak — bkz.
// bellek notu no-raylib-windows-verify).
#include <atomic>
#include <cstdio>

#include "core/jobs/job_system.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
struct Work {
  std::atomic<uint64_t> *done;
  uint32_t seed;
};
void sim_job(void *d) {
  Work *w = static_cast<Work *>(d);
  // Biraz gercek is: kucuk bir LCG dongusu (ayirma yok).
  uint32_t x = w->seed;
  for (int i = 0; i < 2000; i++) x = x * 1664525u + 1013904223u;
  if (x == 0xFFFFFFFFu) std::printf("imkansiz\n"); // optimize edilmesin
  w->done->fetch_add(1, std::memory_order_relaxed);
}

struct Harness {
  SystemArena sys;
  FrameArena frame;
  SceneArena scene;
  JobSystem js;
  Profiler prof;
  std::atomic<uint64_t> done{0};
  bool init() {
    if (!sys.reserve(32u << 20, "system")) return false;
    if (!sys.carve(frame, 4u << 20, "frame")) return false;
    if (!sys.carve(scene, 4u << 20, "scene")) return false;
    if (!js.init(sys, JobSystemConfig{})) return false;
    ProfilerConfig pc;
    pc.frame_capacity = 600;
    pc.zone_capacity = 16384;
    if (!prof.init(sys, pc)) return false;
    prof.watch_arena(&sys);
    prof.watch_arena(&scene);
    prof.watch_arena(&frame);
    return true;
  }
  // Bir kare. Donus: pencere icindeki ayirma sayisi.
  uint64_t run_frame(uint32_t f, bool inject_alloc) {
    AllocGate::begin_frame();
    prof.begin_frame();
    frame.begin_frame();
    {
      ENGINE_ZONE("sim");
      Work *w = frame.alloc_array<Work>(64);
      JobDecl *jobs = frame.alloc_array<JobDecl>(64);
      for (uint32_t i = 0; i < 64; i++) {
        w[i] = Work{&done, f * 64 + i};
        jobs[i] = JobDecl{sim_job, &w[i]};
      }
      Counter c;
      js.run(jobs, 64, &c);
      {
        ENGINE_ZONE("wait");
        js.wait(c);
      }
    }
    {
      ENGINE_ZONE("scratch");
      uint64_t *tmp = frame.alloc_array<uint64_t>(1024);
      tmp[0] = f;
    }
    if (inject_alloc) {
      int *leak = new int(1); // ENJEKSIYON: kapi bunu gormeli
      tulpar::engine::test::escape(leak); // elenmesin
      delete leak;
    }
    prof.end_frame();
    return AllocGate::end_frame();
  }
};
} // namespace

ENGINE_TEST(faz0_gate_zero_allocations_per_frame) {
  static Harness h; // buyuk: statik
  CHECK(h.init());
  const uint32_t kFrames = 240;
  uint64_t violations = 0;
  for (uint32_t f = 0; f < kFrames; f++)
    if (h.run_frame(f, false) != 0) violations++;
  CHECK(violations == 0);
  CHECK(h.done.load() == (uint64_t)kFrames * 64);
  CHECK(h.frame.stats().overflow_count == 0);
  CHECK(h.frame.stats().reset_count == kFrames);
  CHECK(h.sys.check() && h.frame.check() && h.scene.check());
  CHECK(h.prof.frames_recorded() == kFrames);
  CHECK(h.prof.zones_dropped() == 0);
  // Zamanlar makineye bagli: BILGI olarak basilir, ASSERT EDILMEZ (Tuzaklar 1l).
  static uint64_t scratch[1200];
  h.prof.dump_text(stdout, Span<uint64_t>(scratch, 1200));
  h.js.shutdown();
}

// POZITIF KONTROL: kapi enjekte edilen tek ayirmayi yakaliyor mu?
ENGINE_TEST(faz0_gate_catches_injected_allocation) {
  static Harness h;
  CHECK(h.init());
  uint64_t v = 0;
  for (uint32_t f = 0; f < 10; f++) v += h.run_frame(f, f == 5) != 0 ? 1 : 0;
  CHECK(v == 1);
  h.js.shutdown();
}
