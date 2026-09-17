// Faz 2: Jolt sarmalayicisi — dusen kutular, belirlenimlilik (ayni surec iki
// kosum + ALTIN ozet: platformlar arasi bit esitligi iddiasi CI'da
// Linux x86_64 <-> macOS arm64 ile sinanir), adim icinde ayirma.
#include <cmath>
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


// Isin testi (Faz 4 / ses okluzyonu): ANALITIK — bilinen konumdaki kure ve
// kutuya atilan isinin mesafesi hesaplanabilir. KONTROL: iskalayan isin false
// doner ve hicbir govdeyi isaretlemez. Altin ozet SAHNESINE dokunmaz: ayri
// dunya, salt okunur sorgu.
ENGINE_TEST(physics_raycast_hits_known_geometry) {
  static SystemArena rsys;
  CHECK(rsys.reserve(16u << 20, "phys-ray"));
  Physics ph;
  PhysicsConfig cfg;
  cfg.threads = 1;
  CHECK(ph.init(rsys, cfg));
  if (!ph.ok()) return;
  // Kure: merkez (0,0,-10), r=1 -> on yuzey z=-9 (mesafe 9).
  // Kutu: merkez (5,0,0), yari kenar 1 -> on yuzey x=4 (mesafe 4).
  BodyId sphere = ph.add_sphere(1.0f, {0, 0, -10}, false);
  BodyId box = ph.add_box({1, 1, 1}, {5, 0, 0}, Quat::identity(), false);
  CHECK(sphere.valid() && box.valid());
  ph.step(1.0f / 60.0f); // genis faz agaci guncellensin

  RayHit hit;
  CHECK(ph.raycast({0, 0, 0}, {0, 0, -1}, 50.0f, &hit));
  std::printf("    [bilgi] isin -Z: mesafe %.4f (beklenen 9), normal (%.2f %.2f %.2f), nokta z=%.3f\n", hit.distance,
              hit.normal.x, hit.normal.y, hit.normal.z, hit.point.z);
  CHECK(std::fabs(hit.distance - 9.0f) < 0.01f);
  CHECK(hit.body.v == sphere.v);
  CHECK(hit.normal.z > 0.99f); // kureye onden carpti

  RayHit hb;
  CHECK(ph.raycast({0, 0, 0}, {1, 0, 0}, 50.0f, &hb));
  std::printf("    [bilgi] isin +X: mesafe %.4f (beklenen 4), normal x=%.2f\n", hb.distance, hb.normal.x);
  CHECK(std::fabs(hb.distance - 4.0f) < 0.01f);
  CHECK(hb.body.v == box.v && hb.normal.x < -0.99f);

  // Normalize edilmemis yon ayni sonucu vermeli (dir icerde normalize edilir).
  RayHit hn;
  CHECK(ph.raycast({0, 0, 0}, {0, 0, -37.5f}, 50.0f, &hn));
  CHECK(std::fabs(hn.distance - 9.0f) < 0.01f);

  // KONTROL 1: yukari atilan isin hicbir seye carpmaz.
  RayHit miss;
  const bool up = ph.raycast({0, 0, 0}, {0, 1, 0}, 50.0f, &miss);
  std::printf("    [bilgi] KONTROL isin +Y: %s (mesafe %.2f, govde gecerli %s)\n", up ? "CARPTI" : "iskaladi",
              miss.distance, miss.body.valid() ? "evet" : "hayir");
  CHECK(!up && !miss.body.valid() && miss.distance == 0.0f);
  // KONTROL 2: menzil kisa -> kure menzil disinda.
  CHECK(!ph.raycast({0, 0, 0}, {0, 0, -1}, 5.0f, &miss));
  // KONTROL 3: sifir yon / sifir menzil.
  CHECK(!ph.raycast({0, 0, 0}, {0, 0, 0}, 50.0f, &miss));
  CHECK(!ph.raycast({0, 0, 0}, {0, 0, -1}, 0.0f, &miss));
  ph.shutdown();
}

// TEMAS OLAYLARI — fizik adiminda olusan carpmalar kuyruga dusuyor mu.
//
// NEDEN: koprunun oyun tarafindaki en buyuk boslugu "neye carptim" idi. Bugunku
// FFI callback tasimadigi icin cozum kuyruk; bu kapi kuyrugun GERCEKTEN dolup
// dogru sayilari tasidigini olcuyor. Kontrolsuz bir "olay geldi" iddiasi,
// Jolt'un her adimda urettigi gurultuyu de yesil sayardi.
ENGINE_TEST(physics_contact_events_fire_with_speed) {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(32u << 20, "contact");

  // --- URUN: yukaridan birakilan kure zemine carpiyor --------------------
  Physics ph;
  PhysicsConfig cfg;
  cfg.threads = 1; // belirlenimli sayi icin tek parcacik
  CHECK(ph.init(sys, cfg));
  ph.add_box({20, 1, 20}, {0, -1, 0}, Quat::identity(), false); // zemin
  BodyId top = ph.add_sphere(0.5f, {0, 4.0f, 0}, true);
  CHECK(top.valid());

  uint32_t ilk_kare = 0;
  float carpma_hizi = 0;
  Vec3 carpma_noktasi{};
  Vec3 carpma_normali{};
  for (int i = 0; i < 240 && ilk_kare == 0; i++) {
    ph.clear_contacts();
    ph.step(1.0f / 60.0f);
    if (ph.contact_count() > 0) {
      ilk_kare = (uint32_t)i + 1;
      const ContactEvent e = ph.contact(0);
      carpma_hizi = e.speed;
      carpma_noktasi = e.point;
      carpma_normali = e.normal;
    }
  }
  CHECK(ilk_kare > 0);

  // Serbest dusus: 4 m yukseklikten 0.5 yaricapli kure ~2.5 m duser.
  // v = sqrt(2*g*h) ~= sqrt(2*9.81*2.5) ~= 7.0 m/s. Genis ama KOR OLMAYAN
  // aralik: sifir ya da sacma bir sayi gecemez.
  bool hiz_makul = carpma_hizi > 3.0f && carpma_hizi < 12.0f;
  CHECK(hiz_makul);
  // Temas zeminin ustunde (y ~= 0) ve normal dusey olmali.
  bool nokta_makul = carpma_noktasi.y > -0.6f && carpma_noktasi.y < 0.6f;
  CHECK(nokta_makul);
  const float ny = carpma_normali.y < 0 ? -carpma_normali.y : carpma_normali.y;
  CHECK(ny > 0.9f);
  std::printf("    [bilgi] ilk temas kare %u: hiz %.2f m/s (serbest dusus ~7.0), nokta y %.3f, normal y %.3f\n",
              ilk_kare, (double)carpma_hizi, (double)carpma_noktasi.y, (double)ny);

  // --- KONTROL 1: hicbir seye degmeyen govde OLAY URETMEMELI -------------
  // Bu olmadan kapi "adim kostu" ile "carpma oldu"yu ayirt edemezdi.
  Physics bos;
  PhysicsConfig bcfg;
  bcfg.threads = 1;
  CHECK(bos.init(sys, bcfg));
  bos.add_sphere(0.5f, {0, 50.0f, 0}, true); // zemin YOK, serbest dusuyor
  uint32_t bos_olay = 0;
  for (int i = 0; i < 120; i++) {
    bos.clear_contacts();
    bos.step(1.0f / 60.0f);
    bos_olay += bos.contact_count();
  }
  CHECK(bos_olay == 0);
  std::printf("    [bilgi] KONTROL zeminsiz serbest dusus: %u olay (0 olmali)\n", bos_olay);

  // --- KONTROL 2: halka dolunca DUSEN olay SAYILIYOR mu ------------------
  // Sessiz kirpilma, oyunun "carpma gelmedi" sanmasi demek olurdu.
  Physics dar;
  PhysicsConfig dcfg;
  dcfg.threads = 1;
  dcfg.max_contact_events = 1; // bilerek yetersiz
  CHECK(dar.init(sys, dcfg));
  dar.add_box({20, 1, 20}, {0, -1, 0}, Quat::identity(), false);
  for (int i = 0; i < 12; i++) dar.add_sphere(0.4f, {i * 1.0f - 5.5f, 1.0f, 0}, true);
  uint32_t tasma = 0, gorulen = 0;
  for (int i = 0; i < 180 && tasma == 0; i++) {
    dar.clear_contacts();
    dar.step(1.0f / 60.0f);
    gorulen = dar.contact_count();
    tasma = dar.contact_overflow();
  }
  CHECK(tasma > 0);
  CHECK(gorulen <= 1);
  std::printf("    [bilgi] KONTROL halka 1 yuva: gorulen %u, DUSEN %u (tasma gorunur)\n", gorulen, tasma);

  // --- BELIRLENIMLILIK: ayni kurulum ayni sayiyi vermeli -----------------
  Physics tekrar;
  PhysicsConfig rcfg;
  rcfg.threads = 1;
  CHECK(tekrar.init(sys, rcfg));
  tekrar.add_box({20, 1, 20}, {0, -1, 0}, Quat::identity(), false);
  tekrar.add_sphere(0.5f, {0, 4.0f, 0}, true);
  uint32_t r_kare = 0;
  float r_hiz = 0;
  for (int i = 0; i < 240 && r_kare == 0; i++) {
    tekrar.clear_contacts();
    tekrar.step(1.0f / 60.0f);
    if (tekrar.contact_count() > 0) { r_kare = (uint32_t)i + 1; r_hiz = tekrar.contact(0).speed; }
  }
  bool ayni = r_kare == ilk_kare && r_hiz == carpma_hizi;
  CHECK(ayni);
  std::printf("    [bilgi] belirlenimlilik: kare %u==%u, hiz bitleri %s\n", r_kare, ilk_kare,
              r_hiz == carpma_hizi ? "AYNI" : "FARKLI");
  ph.shutdown();
  bos.shutdown();
  dar.shutdown();
  tekrar.shutdown();
}

// --- PR #322: kinematik karakter denetleyicisi (Jolt CharacterVirtual) ----
ENGINE_TEST(physics_character_controller) {
  static SystemArena sys;
  sys.reserve(4u << 20, "phys-char");
  PhysicsConfig cfg;
  cfg.threads = 1;
  cfg.max_characters = 1;
  Physics ph;
  CHECK(ph.init(sys, cfg));

  // Zemin: 50x1x50 box at y=-1 (top surface is at y=0)
  ph.add_box({50, 1, 50}, {0, -1, 0}, Quat::identity(), false);

  // Karakter havada (y = 5) dogar
  CharacterConfig ccfg;
  ccfg.position = {0, 5, 0};
  CharacterId cid = ph.add_character(ccfg);
  CHECK(cid.valid());
  CHECK(!ph.character_grounded(cid));

  // Yere inene kadar adimla (tavanli). ONCEDEN tam 60 adim (1,00 s) bekleniyordu
  // ve kapi KIL PAYI dusuyordu: y=5'ten serbest dusus analitik olarak
  // sqrt(2*5/9,81) = 1,0096 s surer, yani 1,00 s'de karakter hala ~9,5 cm
  // yukarida. Sabit sayi yerine OLCUM: kacinci karede indigi basiliyor.
  int inis_kare = 0;
  for (int i = 0; i < 180 && inis_kare == 0; i++) {
    ph.step(1.0f / 60.0f, 1);
    if (ph.character_grounded(cid)) inis_kare = i + 1;
  }
  std::printf("    [bilgi] karakter %d. karede zemine indi (analitik serbest dusus ~%.1f kare)\n",
              inis_kare, 60.0f * 1.0096f);
  CHECK(inis_kare > 0);
  CHECK(ph.character_grounded(cid));
  Vec3 pos = ph.character_position(cid);
  CHECK(pos.y > -0.1f && pos.y < 0.1f); // zeminin ustu 0

  // saga yuru
  ph.set_character_input(cid, {2, 0, 0}, false);
  for (int i = 0; i < 30; i++) {
    ph.step(1.0f / 60.0f, 1);
  }

  pos = ph.character_position(cid);
  CHECK(pos.x > 0.5f); // saga hareket etmis olmali
  CHECK(pos.y > -0.1f && pos.y < 0.1f);

  // Ziplama tetikleyelim
  ph.set_character_input(cid, {0, 0, 0}, true);
  ph.step(1.0f / 60.0f, 1);
  
  // Havada olmali
  CHECK(!ph.character_grounded(cid));
  pos = ph.character_position(cid);
  CHECK(pos.y > 0.05f);

  ph.shutdown();
}
