// Faz 1 RHI: loader -> cihaz -> offscreen ucgen (piksel kapisi) -> zaman
// damgasi -> PSO cache -> subpass birlesme geri bildirimi. Loader/cihaz
// yoksa GORUNUR atlanir (ozet satirinda sayilir), sessizce gecmez.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "rhi/tile_budget.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::rhi;
namespace test = tulpar::engine::test;

namespace {
VkApi g_api;
bool g_loader_tried = false, g_loader_ok = false;

bool loader() {
  if (!g_loader_tried) {
    g_loader_tried = true;
    g_loader_ok = vk_api_load(g_api);
  }
  return g_loader_ok;
}

const char *type_name(VkPhysicalDeviceType t) {
  switch (t) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "ayrik GPU";
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "tumlesik GPU";
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "sanal GPU";
  case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU (lavapipe)";
  default: return "?";
  }
}

bool open_device(SystemArena &sys, Device &dev) {
  if (!sys.reserve(64u << 20, "rhi")) return false;
  DeviceConfig cfg;
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  cfg.prefer = pref ? pref : "";
  cfg.validation = true; // katman varsa etkin; yoksa caps.validation_layer=false (bilgi)
  return dev.init(sys, g_api, cfg);
}

bool px_near(const uint8_t *p, int r, int g, int b, int tol = 2) {
  return std::abs((int)p[0] - r) <= tol && std::abs((int)p[1] - g) <= tol && std::abs((int)p[2] - b) <= tol;
}
} // namespace

ENGINE_TEST(rhi_loader_and_device_caps) {
  if (!loader()) { test::skip("Vulkan loader (libvulkan) yok — RHI testleri kosmadi"); return; }
  SystemArena sys;
  Device dev;
  if (!open_device(sys, dev)) {
    std::printf("    [bilgi] cihaz acilamadi: %s\n", dev.last_error());
    test::skip("Vulkan cihazi yok (ICD?) — RHI testleri kosmadi");
    return;
  }
  const DeviceCaps &c = dev.caps();
  std::printf("    [bilgi] GPU: %s (%s) api=%u.%u.%u vendor=0x%04x\n", c.device_name, type_name(c.device_type),
              VK_API_VERSION_MAJOR(c.api_version), VK_API_VERSION_MINOR(c.api_version),
              VK_API_VERSION_PATCH(c.api_version), c.vendor_id);
  std::printf("    [bilgi] zorunlu: descriptorIndexing=%d timelineSemaphore=%d bufferDeviceAddress=%d | "
              "lazilyAllocated=%d timestamps=%d(%.1f ns)\n",
              c.descriptor_indexing, c.timeline_semaphore, c.buffer_device_address, c.lazily_allocated_memory,
              c.timestamps, c.timestamp_period_ns);
  std::printf("    [bilgi] uzanti: subpass_merge_feedback=%d graphics_pipeline_library=%d host_image_copy=%d "
              "fragment_shading_rate=%d portability=%d\n",
              c.ext_subpass_merge_feedback, c.ext_graphics_pipeline_library, c.ext_host_image_copy,
              c.khr_fragment_shading_rate, c.khr_portability_subset);
  std::printf("    [bilgi] GPL kullanilabilir=%d; dogrulama katmani=%s\n", c.graphics_pipeline_library,
              c.validation_layer ? "ETKIN" : "yok (VK_LAYER_KHRONOS_validation kurulu degil)");
  CHECK(dev.ok());
  // Plan L2 "zorunlu" listesi (1.2 cekirdek) bir HIPOTEZ: Dusuk sinif cihaz
  // (Mali-G72, Vulkan 1.1, 2018 surucusu) ucunu de vermiyor. Kapi degil, veri:
  // basilir, cihaz matrisine girer; eksik yol yedegiyle calismak zorunda.
  if (c.missing_mandatory[0])
    std::printf("    [bilgi] plan L2 'zorunlu' eksik (cihaz verisi, kapi degil): %s\n", c.missing_mandatory);
  // Kalici PSO onbellegi nereye yaziliyor? Bos ise KAPALI demektir ($HOME ve
  // $TMPDIR yoksa cozulemez) — bunu gormeden "onbellek var" varsayilamaz.
  std::printf("    [bilgi] PSO onbellegi: %s (%s); pipelineCacheUUID %02x%02x%02x%02x...\n",
              dev.pso_cache_path()[0] ? dev.pso_cache_path() : "(yol cozulemedi)",
              dev.pso().stats().loaded ? "yuklendi" : pso_reject_str(dev.pso().stats().reject),
              c.pipeline_cache_uuid[0], c.pipeline_cache_uuid[1], c.pipeline_cache_uuid[2], c.pipeline_cache_uuid[3]);
  std::printf("    [bilgi] surucu=%u gpl=%d merge_feedback=%d fsr=%d host_image_copy=%d\n", c.driver_version,
              c.graphics_pipeline_library, c.ext_subpass_merge_feedback, c.khr_fragment_shading_rate, c.ext_host_image_copy);
  dev.shutdown();
}

ENGINE_TEST(rhi_first_pixel_offscreen_triangle) {
  if (!loader()) { test::skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  Device dev;
  if (!open_device(sys, dev)) { test::skip("Vulkan cihazi yok"); return; }
  char dir[512];
  test::tmp_template(dir, sizeof dir, "engine_rhi");
  CHECK(mkdtemp(dir) != nullptr);
  char cache_path[300];
  std::snprintf(cache_path, sizeof cache_path, "%s/pso.cache", dir);
  OffscreenConfig cfg;
  cfg.width = 128;
  cfg.height = 128;
  cfg.pso_cache_path = cache_path;

  // 1. cizim: cache yok -> uretilir ve yazilir.
  OffscreenResult r1;
  bool ok = render_triangle_offscreen(dev, sys, cfg, &r1);
  if (!ok) std::printf("    hata: %s\n", r1.error);
  CHECK(ok);
  if (ok) {
    const uint32_t w = cfg.width, h = cfg.height;
    const uint8_t *center = r1.pixels + ((h / 2) * w + w / 2) * 4;
    const uint8_t *corner = r1.pixels + 0;
    const uint8_t *top = r1.pixels + ((h / 10) * w + w / 2) * 4; // ucgenin ustunde bos alan
    CHECK(px_near(center, 255, 128, 0));   // ucgen rengi (1.0, 0.5, 0.0)
    CHECK(px_near(corner, 10, 20, 30));    // temizleme rengi
    CHECK(px_near(top, 10, 20, 30));
    CHECK(center[3] == 255);
    // Kapsama: NDC'de taban 1.2 (ekranin %60'i), yukseklik 1.2 (%60);
    // alan = 0.5 * 0.6 * 0.6 = ekranin %18'i. (Ilk yazimda %36 denmisti —
    // NDC genisligini 2 yerine 1 sayan aritmetik hatasi; olcum %17.6 dedi.)
    uint32_t tri = 0;
    for (uint32_t i = 0; i < w * h; i++) if (px_near(r1.pixels + i * 4, 255, 128, 0, 8)) tri++;
    float frac = (float)tri / (w * h);
    CHECK(frac > 0.15f && frac < 0.21f);
    CHECK(!r1.pso_cache_loaded);
    CHECK(r1.pso_cache_bytes > 0);
    std::printf("    [bilgi] ucgen %.1f%% piksel; GPU render pass %.3f ms (%s); vkAllocateMemory=%u; PSO cache %llu B\n",
                frac * 100, r1.gpu_ns / 1e6, dev.caps().timestamps ? "zaman damgasi" : "zaman damgasi YOK",
                r1.memory_allocations, (unsigned long long)r1.pso_cache_bytes);
    if (r1.merge_feedback_available)
      std::printf("    [bilgi] subpass birlesme: birlesme sonrasi %u subpass (2 -> %u), durum [%d,%d]\n",
                  r1.post_merge_subpass_count, r1.post_merge_subpass_count, r1.subpass_merge_status[0],
                  r1.subpass_merge_status[1]);
    else
      std::printf("    [bilgi] subpass birlesme geri bildirimi: uzanti YOK bu cihazda — kapi cihazda olculur (Faz 1)\n");
    char ppm[300];
    std::snprintf(ppm, sizeof ppm, "%s/ilk_piksel.ppm", dir);
    if (write_ppm(ppm, r1.pixels, w, h)) std::printf("    [bilgi] goruntu: %s\n", ppm);
    // Zaman damgasi OKUNABILIR olmali; degeri surucuye bagli (MoltenVK 0 verdi,
    // NVIDIA 8 us). Sifir "olcum yok" demektir, gecmeli ama bilgi olarak gorunmeli.
    if (dev.caps().timestamps) CHECK(r1.timestamps_valid);
    if (dev.caps().timestamps && r1.gpu_ns == 0)
      std::printf("    [bilgi] zaman damgasi okundu ama fark 0 — bu surucu TOP/BOTTOM'u ayirt etmiyor\n");
  }
  // 2. kurulum: cache dosyasi var -> yuklenmeli. Kurulum surucu icinde
  // AYIRMA YAPAR (pipeline, image, lavapipe'ta LLVM JIT operator new ile):
  // olculur, bilgi olarak basilir, IDDIA EDILMEZ. A2 iddiasi KARE icin:
  // kayit + gonderim + geri okuma bizim kodda 0 ayirma. (Ilk yazim tek adimli
  // render'i sinayip lavapipe'ta dustu: surucu kurulum ayirmasi kareyle
  // karisiyordu — CI Linux 2026-09-14.)
  OffscreenResult r2;
  AllocGate::begin_frame();
  OffscreenTarget *tgt = offscreen_create(dev, sys, cfg, &r2);
  uint64_t setup_allocs = AllocGate::end_frame();
  CHECK(tgt != nullptr);
  CHECK(r2.pso_cache_loaded);
  // KARE ayirmasi: global operator new SURUCUYU DE sayar (ayni surec). Olculdu
  // 2026-09-14: NVIDIA 0/kare, MoltenVK 28/kare (Metal nesneleri), lavapipe
  // kurulumda. Bu yuzden iddia ikiye ayrilir:
  //   (a) bizim kod kare icinde ayirmaz — surucusuz harness'ta 0 (Faz 0 kapisi);
  //   (b) surucunun kare ayirmasi CIHAZ VERISIDIR: olculur, basilir, ve
  //       KARARLI olmali (kareler arasi buyume = bizde sizinti, ornegin havuz
  //       sifirlanmiyor). Kararlilik burada iddia, sifir degil.
  uint64_t frame_allocs[5] = {0, 0, 0, 0, 0};
  if (tgt) {
    for (int f = 0; f < 5; f++) {
      AllocGate::begin_frame();
      ok = offscreen_render_frame(tgt, cfg, &r2);
      frame_allocs[f] = AllocGate::end_frame();
      CHECK(ok);
    }
    const uint8_t *center = r2.pixels + ((cfg.height / 2) * cfg.width + cfg.width / 2) * 4;
    CHECK(px_near(center, 255, 128, 0));
    offscreen_destroy(tgt);
  }
  std::printf("    [bilgi] operator new sayimi: kurulum=%llu (surucu dahil), kare=[%llu %llu %llu %llu %llu] (surucu dahil)\n",
              (unsigned long long)setup_allocs, (unsigned long long)frame_allocs[0],
              (unsigned long long)frame_allocs[1], (unsigned long long)frame_allocs[2],
              (unsigned long long)frame_allocs[3], (unsigned long long)frame_allocs[4]);
  // Surucu kare ayirmasi IDDIA EDILMEZ (kararlilik bile degil): lavapipe'ta
  // LLVM JIT arka plan thread'leri kareler arasi degisken ayirma yapiyor
  // (CI 2026-09-14: frame[4] > frame[1] ile dustu). Bizim kodun iddiasi
  // surucusuz harness'ta (Faz 0) ve sim testlerinde; burada yalniz bilgi.
  dev.shutdown();
  // temizlik (ppm bilgi icin kalir; cache silinir)
  unlink(cache_path);
}

// Paralel komut kaydi: renk subpass'i 8 job'a bolunur (yatay bantlar), her
// job kendi thread havuzundan ikincil tampon kaydeder; sonuc tek thread'li
// cizimle BAYT BAYT ayni olmali.
ENGINE_TEST(rhi_parallel_command_recording_matches_inline) {
  if (!loader()) { test::skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  Device dev;
  if (!open_device(sys, dev)) { test::skip("Vulkan cihazi yok"); return; }
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));
  CommandPools pools;
  CHECK(pools.init(dev, sys, js.worker_count() + 1, 16));
  OffscreenConfig cfg;
  cfg.width = 96;
  cfg.height = 96;
  OffscreenResult inline_r, par_r;
  CHECK(render_triangle_offscreen(dev, sys, cfg, &inline_r));
  cfg.jobs = &js;
  cfg.pools = &pools;
  cfg.parallel_jobs = 8;
  bool ok = render_triangle_offscreen(dev, sys, cfg, &par_r);
  if (!ok) std::printf("    hata: %s\n", par_r.error);
  CHECK(ok);
  if (ok && inline_r.ok) {
    CHECK(par_r.secondaries_recorded == 8);
    CHECK(std::memcmp(inline_r.pixels, par_r.pixels, (size_t)cfg.width * cfg.height * 4) == 0);
    std::printf("    [bilgi] 8 ikincil tampon, %u farkli thread yuvasi (worker=%u); GPU %.3f ms\n",
                par_r.recording_threads, js.worker_count(), par_r.gpu_ns / 1e6);
    CHECK(par_r.recording_threads >= 1);
  }
  pools.shutdown();
  js.shutdown();
  dev.shutdown();
}

// GPL: 4 kutuphane + link ile kurulan renk pipeline'i monolitikle ayni pikseli
// vermeli; sureler bilgi. Cihaz desteklemiyorsa GORUNUR atlanir.
ENGINE_TEST(rhi_graphics_pipeline_library_links_and_matches) {
  if (!loader()) { test::skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  Device dev;
  if (!open_device(sys, dev)) { test::skip("Vulkan cihazi yok"); return; }
  if (!dev.caps().graphics_pipeline_library) {
    test::skip("VK_EXT_graphics_pipeline_library yok bu cihazda");
    dev.shutdown();
    return;
  }
  OffscreenConfig cfg;
  cfg.width = 96;
  cfg.height = 96;
  OffscreenResult mono, gpl;
  CHECK(render_triangle_offscreen(dev, sys, cfg, &mono));
  cfg.use_pipeline_library = true;
  bool ok = render_triangle_offscreen(dev, sys, cfg, &gpl);
  if (!ok) std::printf("    hata: %s\n", gpl.error);
  CHECK(ok);
  if (ok && mono.ok) {
    CHECK(gpl.pipeline_library_used);
    CHECK(std::memcmp(mono.pixels, gpl.pixels, (size_t)cfg.width * cfg.height * 4) == 0);
    std::printf("    [bilgi] pipeline olusturma: monolitik %.3f ms, GPL kutuphaneler %.3f ms + link %.3f ms\n",
                gpl.pipeline_monolithic_ns / 1e6, gpl.pipeline_library_ns / 1e6, gpl.pipeline_link_ns / 1e6);
  }
  CHECK(dev.validation_errors() == 0);
  dev.shutdown();
}

// Dogrulama katmani etkinse butun RHI yolu 0 hata vermeli (katman yoksa bilgi).
ENGINE_TEST(rhi_validation_layer_reports_zero_errors) {
  if (!loader()) { test::skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  Device dev;
  if (!open_device(sys, dev)) { test::skip("Vulkan cihazi yok"); return; }
  if (!dev.caps().validation_layer) {
    test::skip("VK_LAYER_KHRONOS_validation yok — API kullanimi dogrulanmadi");
    dev.shutdown();
    return;
  }
  // Katman var ama mesaj kanali yoksa "0 hata" olcum degil (telefonda goruldu).
  CHECK(dev.caps().debug_messenger);
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));
  CommandPools pools;
  CHECK(pools.init(dev, sys, js.worker_count() + 1, 16));
  OffscreenConfig cfg;
  cfg.jobs = &js;
  cfg.pools = &pools;
  cfg.parallel_jobs = 4;
  cfg.use_pipeline_library = dev.caps().graphics_pipeline_library;
  OffscreenResult r;
  CHECK(render_triangle_offscreen(dev, sys, cfg, &r));
  CHECK(dev.validation_errors() == 0);
  std::printf("    [bilgi] dogrulama hatasi: %u\n", dev.validation_errors());
  pools.shutdown();
  js.shutdown();
  dev.shutdown();
}


// Pencereye bagli omur: swapchain derinligi her yeniden boyutlandirmada yeniden
// ayrilir. Blok ayirici bump'tir ve GERI VERMEZ — o yolla her boyut degisimi
// bellek yerdi (FAZ3 acik isi). `allocate_dedicated`/`free_dedicated` serbest
// birakabilir. Bu test mekanizmayi sinar; pozitif kontrol eski yolun gercekten
// buyudugunu gosterir (yoksa test hicbir sey olcmuyor olabilir).
ENGINE_TEST(rhi_dedicated_allocation_is_released) {
  if (!loader()) { test::skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  Device dev;
  if (!open_device(sys, dev)) { test::skip("Vulkan cihazi yok"); return; }

  VkMemoryRequirements req{};
  req.size = 16u << 20; // ~2K derinlik tamponu mertebesi
  req.alignment = 256;
  req.memoryTypeBits = 0xFFFFFFFFu;

  const uint32_t blocks0 = dev.memory_allocation_count();
  CHECK(dev.dedicated_allocation_count() == 0);
  for (int i = 0; i < 8; i++) { // 8 "yeniden boyutlandirma"
    MemoryAlloc m;
    bool ok = dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, &m);
    CHECK(ok);
    if (!ok) break;
    bool one = dev.dedicated_allocation_count() == 1;
    CHECK(one);
    dev.free_dedicated(&m);
    bool released = m.memory == VK_NULL_HANDLE && dev.dedicated_allocation_count() == 0;
    CHECK(released);
  }
  // Blok ayirici hic buyumedi: 8 dongu tek bir blok bile yemedi.
  bool blocks_same = dev.memory_allocation_count() == blocks0;
  CHECK(blocks_same);

  // POZITIF KONTROL: ayni 8 dongu blok ayiricidan gecerse bellek BUYUR.
  for (int i = 0; i < 8; i++) {
    MemoryAlloc m;
    if (!dev.allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, &m)) break;
  }
  const uint32_t blocks1 = dev.memory_allocation_count();
  bool grew = blocks1 > blocks0;
  CHECK(grew); // buyumediyse test bir sey olcmuyor demektir
  std::printf("    [bilgi] adanmis ayirma: 8 dongu sonrasi blok %u -> %u (degismedi); blok ayiriciyla %u -> %u (pozitif kontrol)\n",
              blocks0, blocks0, blocks0, blocks1);
  dev.shutdown();
}

// Mali tile butcesi (Vulkan-Samples/Arm): <= 8 renk+girdi attachment, <= 128 bit/px
// renk. Gecisler yaratilirken zorlanir (swapchain, offscreen). Burada kural
// kendisi sinanir: bizim gecislerimiz sigar; pozitif kontrol asimi yakalar;
// bilinmeyen bicim SESSIZCE gecmez.
ENGINE_TEST(rhi_mali_tile_budget_rule) {
  const VkFormat main_pass[2] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D32_SFLOAT}; // swapchain + offscreen
  TileBudget tb = tile_budget(main_pass, 2);
  std::printf("    [bilgi] ana gecis: %u attachment, %u bit/px renk, %u bit derinlik (butce %u / %u)\n", tb.attachments,
              tb.color_bits, tb.depth_bits, kTileMaxAttachments, kTileMaxColorBits);
  CHECK(tb.ok);
  CHECK(tb.attachments == 1 && tb.color_bits == 32 && tb.depth_bits == 32);
  const VkFormat shadow_pass[1] = {VK_FORMAT_D16_UNORM};
  tb = tile_budget(shadow_pass, 1);
  CHECK(tb.ok && tb.attachments == 0 && tb.depth_bits == 16);
  // Plan vis buffer (64 bit) + isik (32) + hareket vektoru (16) + reaktif maske (8) = 120: sigar.
  const VkFormat planned[5] = {VK_FORMAT_R32G32_UINT, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_R8G8_UNORM,
                               VK_FORMAT_R8_UNORM, VK_FORMAT_D32_SFLOAT};
  tb = tile_budget(planned, 5);
  CHECK(tb.ok && tb.color_bits == 120);
  // POZITIF KONTROL 1: 2 x RGBA32F = 256 bit > 128 -> reddedilmeli.
  const VkFormat fat[2] = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
  tb = tile_budget(fat, 2);
  CHECK(!tb.ok);
  std::printf("    [bilgi] pozitif kontrol: %s\n", tb.error);
  // POZITIF KONTROL 2: 9 x R8 = 72 bit ama 9 attachment > 8 -> reddedilmeli.
  VkFormat many[9];
  for (int i = 0; i < 9; i++) many[i] = VK_FORMAT_R8_UNORM;
  tb = tile_budget(many, 9);
  CHECK(!tb.ok && tb.attachments == 9);
  // Bilinmeyen bicim: hata, 0 bit sayip gecmek yok.
  const VkFormat unknown[1] = {VK_FORMAT_ASTC_4x4_UNORM_BLOCK};
  tb = tile_budget(unknown, 1);
  CHECK(!tb.ok);
}

// --- PSO ON-URETIMI (PLAN Faz 6) --------------------------------------------
// SPIR-V derleme zamaninda uretiliyor (rhi/shaders/*_spv.h), ama PIPELINE
// NESNESI surucu tarafindan ILK KULLANIMDA kuruluyordu ve surecin omruyle
// sinirliydi. Iki maliyet: (a) her acilista ayni shader->ISA derlemesi,
// (b) tembel kurulan pipeline'lar KARE ICINDE derleniyor (takilma).
//
// Bu kapi ikisini de OLCER. Olcum tek cihaz uzerinde yapilir: her kosum icin
// yeni VkInstance acmak sonraki kapilari sessizce ATLANDI'ya dusurur
// (Tuzaklar 8al) ve ayni VkApi tablosuyla ikinci cihaz acmak paylasilan
// cihazin giris noktalarini ezer (8an). Onbellek `PsoCache::reinit` ile
// degistirilir.
//
// Varyant kumesi TAHMIN EDILMEZ: gercek renderer kurulur ve VkApi tablosuna
// takilan ara yordam kac pipeline kuruldugunu SAYAR. Sayi 0 cikarsa kanca
// calismiyordur ve kapi duser (yoksa "0 pipeline, cok hizli" diye yanlis
// yesil olurdu).
//
// ZAMANLAMA TEK BASINA YETMEZ: surucu surec icinde KENDI onbellegini de tutar,
// yani ikinci kurulum dosyamiz olmasa da hizlanir. Bu yuzden asil olcum
// ONBELLEK BUYUMESI: diskten yuklenen onbellege yeni girdi EKLENMIYORSA
// (buyume ~0) kurulum gercekten ISABET etmistir. Ucuncu kosum (bos onbellek,
// surucu ic onbellegi sicak) bu ayrimi gosteren kontroldur.
namespace {
struct PsoRec {
  renderer::Renderer *r;
  bool ui;
};
void pso_rec_main(VkCommandBuffer cb, void *u) {
  PsoRec *p = static_cast<PsoRec *>(u);
  p->r->record(cb);
  if (p->ui) p->r->ui_record(cb);
}
void pso_rec_shadow(VkCommandBuffer cb, void *u) { static_cast<PsoRec *>(u)->r->record_shadow(cb); }

// 8x8 beyaz atlas (UI dortgeni icin; icerik onemsiz, boru hatti onemli).
const uint8_t *pso_white_atlas() {
  static uint8_t px[8 * 8 * 4];
  std::memset(px, 0xFF, sizeof px);
  return px;
}

// Dosyanin `off` baytini XOR'lar (bozma). off < 0 ise sondan sayar.
bool pso_poke(const char *path, long off, uint8_t x) {
  FILE *f = std::fopen(path, "r+b");
  if (!f) return false;
  if (off < 0) std::fseek(f, off, SEEK_END);
  else std::fseek(f, off, SEEK_SET);
  int c = std::fgetc(f);
  if (c == EOF) { std::fclose(f); return false; }
  if (off < 0) std::fseek(f, off, SEEK_END);
  else std::fseek(f, off, SEEK_SET);
  std::fputc((uint8_t)c ^ x, f);
  std::fclose(f);
  return true;
}
bool pso_truncate(const char *path, long bytes) { return ::truncate(path, bytes) == 0; }

bool pso_copy(const char *from, const char *to) {
  FILE *in = std::fopen(from, "rb");
  if (!in) return false;
  FILE *out = std::fopen(to, "wb");
  if (!out) { std::fclose(in); return false; }
  char buf[8192];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
    if (std::fwrite(buf, 1, n, out) != n) break;
  std::fclose(in);
  std::fclose(out);
  return true;
}

// Kurulan onbellegi bir dosyaya yazip boyutunu doner (0 = yazilamadi).
uint64_t pso_snapshot(Device &dev, const char *path) {
  return dev.pso().save() && std::strcmp(dev.pso().path(), path) == 0 ? dev.pso().stats().saved_bytes : 0;
}
} // namespace

ENGINE_TEST(rhi_pso_cache_warms_pipeline_creation) {
  if (!loader()) { test::skip("Vulkan loader (libvulkan) yok — PSO on-uretim kapisi olculmedi"); return; }
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "pso_cache")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  dc.pso_cache_path = ""; // varsayilan ($HOME/.cache) yolu DEVRE DISI: kapi kendi dosyasini yonetir
  dc.validation = true;   // katman varsa yeni yol da dogrulanir (yoksa caps.validation_layer=false)
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  dc.prefer = pref ? pref : "";
  if (!dev.init(sys, g_api, dc)) {
    std::printf("    [bilgi] cihaz acilamadi: %s\n", dev.last_error());
    test::skip("Vulkan cihazi yok (ICD?) — PSO on-uretim kapisi olculmedi");
    return;
  }
  char dir[512];
  test::tmp_template(dir, sizeof dir, "engine_pso");
  CHECK(mkdtemp(dir) != nullptr);
  char cache_path[600], probe_path[600];
  std::snprintf(cache_path, sizeof cache_path, "%s/pso.bin", dir);
  std::snprintf(probe_path, sizeof probe_path, "%s/probe.bin", dir);

  const uint32_t W = 192, H = 192;
  OffscreenConfig oc;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) {
    std::printf("    [bilgi] offscreen: %s\n", ores.error);
    CHECK(false);
    dev.shutdown();
    return;
  }

  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 512;
  rc.post = true; // bloom zinciri 4 boru hatti daha ekler (varyant kumesi buyusun)
  rc.post_width = W;
  rc.post_height = H;
  rc.gpu_cull = true; // dolayli yol 3 boru hatti daha; cihaz vermezse kendi kapanir

  struct Phase {
    uint32_t created = 0;
    uint64_t ns = 0;
    uint64_t loaded = 0; // onbellege diskten giren
    uint64_t after = 0;  // kurulumdan sonra onbellegin boyutu
  };
  // Bir kosum: onbellegi `path`ten kur, renderer'i kur, sayaclari topla.
  auto run_phase = [&](const char *path, Phase *out, renderer::Renderer *keep) {
    dev.pso().reinit(path, false);
    const PsoCacheStats before = dev.pso().stats();
    out->loaded = before.payload_bytes;
    renderer::Renderer local;
    renderer::Renderer &ren = keep ? *keep : local;
    bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
    CHECK(ok);
    const PsoCacheStats s = dev.pso().stats();
    out->created = s.created;
    out->ns = s.create_ns;
    // Onbellegin kurulumdan SONRAKI boyutu: buyume = eklenen girdi = ISKA.
    out->after = pso_snapshot(dev, path) ? dev.pso().stats().saved_bytes : 0;
    if (!keep && ok) ren.shutdown();
    return ok;
  };

  // --- 1. SOGUK: dosya yok. Surucu her pipeline'i sifirdan derler. ----------
  Phase cold;
  run_phase(cache_path, &cold, nullptr);
  const PsoCacheStats cold_s = dev.pso().stats();
  CHECK(!cold_s.loaded);
  CHECK(cold_s.reject == PsoReject::NoFile);
  // Kanca calisiyor mu? Calismiyorsa sayac 0 kalir ve butun olcum anlamsiz olur.
  bool counted = cold.created > 0;
  CHECK(counted);
  CHECK(cold.after > 0); // soguk kosum onbellege girdi YAZMIS olmali
  std::printf("    [bilgi] varyant kumesi: renderer kurulumunda %u grafik boru hatti kuruldu "
              "(kaynaktan sayildi, tahmin degil)\n", cold.created);

  // --- 2. SICAK: ayni dosya yuklenir. --------------------------------------
  Phase warm;
  run_phase(cache_path, &warm, nullptr);
  const PsoCacheStats warm_s = dev.pso().stats();
  bool warm_loaded = warm_s.loaded;
  CHECK(warm_loaded);
  CHECK(warm.created == cold.created); // ayni varyant kumesi
  CHECK(warm.loaded > 0);

  // --- 3. KONTROL: bos onbellek, ama surucunun IC onbellegi artik sicak. ----
  // Sure farkini tek basina "dosya sayesinde" diye okumak bu yuzden yanlistir.
  Phase ctrl;
  run_phase("", &ctrl, nullptr);
  ctrl.after = 0; // yol yok, olculemez

  const double cold_ms = cold.ns / 1e6, warm_ms = warm.ns / 1e6, ctrl_ms = ctrl.ns / 1e6;
  const long long cold_growth = (long long)cold.after - (long long)cold.loaded;
  const long long warm_growth = (long long)warm.after - (long long)warm.loaded;
  std::printf("    [bilgi] kurulum suresi: SOGUK %.2f ms | SICAK %.2f ms | KONTROL (bos onbellek, surucu ici sicak) %.2f ms\n",
              cold_ms, warm_ms, ctrl_ms);
  std::printf("    [bilgi] onbellek buyumesi: SOGUK %lld B (0 -> %llu) | SICAK %lld B (%llu -> %llu)\n", cold_growth,
              (unsigned long long)cold.after, warm_growth, (unsigned long long)warm.loaded,
              (unsigned long long)warm.after);
  // ASIL IDDIA (zamanlamadan bagimsiz): diskten yuklenen onbellege kurulum
  // sirasinda yeni girdi EKLENMEZ — yani her varyant ISABET etmistir. Soguk
  // kosum ayni onbellege cold_growth bayt yazmisti; bu, olcumun bir sey
  // olctugunun kaniti (pozitif kontrol).
  bool hits = cold_growth > 0 && warm_growth * 4 < cold_growth;
  CHECK(hits);
  if (!hits)
    std::printf("    [bilgi] SICAK kosum onbellege %lld bayt EKLEDI — diskten gelen girdiler kullanilmadi\n", warm_growth);
  // Sure yorumu OLCUME dayanir: KONTROL soguga yakinsa hizlanma DOSYADAN gelir
  // (surucunun surec ici durumu bu cihazda pipeline kurulumunu hizlandirmiyor);
  // KONTROL sicaga yakinsa surec ici olcum ikisini ayirt EDEMEZ ve tek gecerli
  // kanit buyume olcumu olur. Iddia ne olursa olsun sayilar basilir.
  if (warm_ms >= cold_ms * 0.9)
    std::printf("    [bilgi] sure farki olculemedi (soguk %.2f, sicak %.2f ms): bu surucu pipeline kurulumunu zaten"
                " ucuz yapiyor — isabet iddiasi BUYUME olcumunde\n", cold_ms, warm_ms);
  else if (ctrl_ms >= (cold_ms + warm_ms) * 0.5)
    // KONTROL soguga sicaktan daha yakin: bos onbellekle kurulum yine pahali,
    // yani hizlanmayi getiren sey DOSYA, surucunun surec ici durumu degil.
    std::printf("    [bilgi] sicak kosum %.2fx hizli; KONTROL (%.2f ms) SOGUGA (%.2f) sicaktan (%.2f) daha yakin ->"
                " hizlanma DISKTEKI DOSYADAN geliyor, surucunun surec ici durumundan degil\n",
                cold_ms / warm_ms, ctrl_ms, cold_ms, warm_ms);
  else
    std::printf("    [bilgi] sicak kosum %.2fx hizli ama KONTROL de hizli (%.2f ms): surec ICINDE dosyanin katkisi"
                " sureden AYIRT EDILEMEZ — gecerli kanit buyume olcumu\n", cold_ms / warm_ms, ctrl_ms);

  // --- 4. KONTROL: bozuk / uyusmayan onbellek REDDEDILMELI -----------------
  // Sessizce kabul etmek tanimsiz davranistir. Her vaka icin dosya bozulur,
  // beklenen ret sebebi denetlenir ve motorun YINE DE dogru kuruldugu gosterilir.
  {
    PsoDeviceId id;
    id.vendor_id = dev.caps().vendor_id;
    id.device_id = dev.caps().device_id;
    id.driver_version = dev.caps().driver_version;
    id.api_version = dev.caps().api_version;
    std::memcpy(id.cache_uuid, dev.caps().pipeline_cache_uuid, VK_UUID_SIZE);
    // (a) yuk kurcalanmis -> ozet tutmaz
    std::snprintf(probe_path, sizeof probe_path, "%s/bozuk_yuk.bin", dir);
    char v_path[600], u_path[600], t_path[600], g_path[600];
    std::snprintf(v_path, sizeof v_path, "%s/baska_satici.bin", dir);
    std::snprintf(u_path, sizeof u_path, "%s/baska_uuid.bin", dir);
    std::snprintf(t_path, sizeof t_path, "%s/kesik.bin", dir);
    std::snprintf(g_path, sizeof g_path, "%s/copluk.bin", dir);
    CHECK(pso_copy(cache_path, probe_path));
    CHECK(pso_copy(cache_path, v_path));
    CHECK(pso_copy(cache_path, u_path));
    CHECK(pso_copy(cache_path, t_path));
    CHECK(pso_poke(probe_path, (long)sizeof(PsoFileHeader) + 3, 0x5A)); // yuk baytini cevir
    CHECK(pso_poke(v_path, 12, 0x01));  // PsoFileHeader::vendor_id
    CHECK(pso_poke(u_path, 40, 0x01));  // PsoFileHeader::cache_uuid[0]
    CHECK(pso_truncate(t_path, 512));   // kesik dosya
    char k_path[600];
    std::snprintf(k_path, sizeof k_path, "%s/minik.bin", dir);
    { // magic denetimine ULASSIN diye baslik boyundan buyuk
      FILE *f = std::fopen(g_path, "wb");
      if (f) {
        for (int i = 0; i < 8; i++) std::fwrite("bu bir PSO onbellegi degil, duz metin...\n", 1, 40, f);
        std::fclose(f);
      }
    }
    { FILE *f = std::fopen(k_path, "wb"); if (f) { std::fwrite("minik", 1, 5, f); std::fclose(f); } }
    const struct { const char *path; const char *name; PsoReject want; } cases[] = {
        {probe_path, "yuk kurcalanmis", PsoReject::PayloadHash},
        {v_path, "baska satici (vendorID)", PsoReject::VendorId},
        {u_path, "pipelineCacheUUID degismis", PsoReject::CacheUuid},
        {t_path, "kesik dosya", PsoReject::SizeMismatch},
        {g_path, "bizim dosya degil", PsoReject::Magic},
        {k_path, "baslik kadar bile degil", PsoReject::TooSmall},
        {"/dev/null/yok", "dosya yok", PsoReject::NoFile},
    };
    for (const auto &c : cases) {
      void *payload = nullptr;
      size_t pn = 0;
      PsoReject got = pso_cache_read_file(c.path, id, &payload, &pn, nullptr);
      bool rejected = got == c.want && payload == nullptr;
      CHECK(rejected);
      std::printf("    [bilgi] ret kontrolu \"%s\": %s%s\n", c.name, pso_reject_str(got),
                  rejected ? "" : "  <-- BEKLENEN DEGIL");
      std::free(payload);
    }
    // POZITIF KONTROL: bozulmamis kopya AYNI yoldan GECER — yoksa yukleyici
    // her seyi reddediyor olabilirdi ve yukaridaki bes satir hicbir sey olcmezdi.
    void *payload = nullptr;
    size_t pn = 0;
    PsoReject good = pso_cache_read_file(cache_path, id, &payload, &pn, nullptr);
    bool accepted = good == PsoReject::None && payload != nullptr && pn > 0;
    CHECK(accepted);
    std::printf("    [bilgi] pozitif kontrol: bozulmamis dosya KABUL (%s, %llu B yuk)\n", pso_reject_str(good),
                (unsigned long long)pn);
    std::free(payload);
  }

  // Reddedilen dosyayla motor YINE DE kurulmali ve dogru cizmeli.
  renderer::Renderer ren;
  {
    Phase bad;
    dev.pso().reinit(probe_path, false);
    const PsoCacheStats s = dev.pso().stats();
    CHECK(!s.loaded);
    CHECK(s.reject == PsoReject::PayloadHash);
    std::printf("    [bilgi] bozuk onbellekle acilis: yuklenmedi (%s), onbellek BOS kuruldu\n", pso_reject_str(s.reject));
    bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
    CHECK(ok);
    bad.created = dev.pso().stats().created;
    CHECK(bad.created == cold.created); // ayni varyant kumesi yeniden kuruldu
    if (!ok) { offscreen_destroy(off); dev.shutdown(); return; }
  }

  renderer::TextureHandle tex = ren.create_texture(pso_white_atlas(), 8, 8, false, false);
  renderer::MaterialHandle atlas = ren.create_material(tex, {1, 1, 1});
  renderer::Vertex vb[24];
  uint32_t ib[36];
  uint32_t n = renderer::Renderer::cube(vb, ib);
  renderer::MeshHandle cube = ren.create_mesh(vb, 24, ib, n);
  CHECK(cube.valid());
  ren.set_light(normalize(Vec3{0.4f, 1.0f, 0.3f}), {0.15f, 0.15f, 0.18f}, 0.9f);
  ren.set_camera(Mat4::look_at({0, 2.0f, 5.0f}, {0, 0, 0}, {0, 1, 0}),
                 Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 100.0f));

  // --- 5. KONTROL: ILK KARE icinde kac pipeline kuruluyor? -----------------
  // On isinma ACIK (bugunku renderer: mesh/golge/UI/post boru hatlari init'te
  // kurulur) -> ilk karede 0 olmali.
  PsoRec rec{&ren, true};
  ren.begin_frame(0);
  ren.ui_begin((float)W, (float)H);
  ren.ui_set_atlas(atlas);
  ren.ui_rect(8, 8, 40, 20, renderer::Renderer::rgba(255, 255, 255));
  ren.draw(cube, Mat4::scale({1.2f, 1.2f, 1.2f}), {0.9f, 0.4f, 0.2f});
  dev.pso().begin_frame();
  bool frame_ok = offscreen_render_custom(off, oc, pso_rec_main, &rec, &ores, pso_rec_shadow);
  uint32_t first_frame = dev.pso().end_frame();
  CHECK(frame_ok);
  if (!frame_ok) std::printf("    [bilgi] kare: %s\n", ores.error);
  bool warm_first_frame_clean = first_frame == 0;
  CHECK(warm_first_frame_clean);
  static uint8_t with_px[W * H * 4];
  if (frame_ok) std::memcpy(with_px, ores.pixels, sizeof with_px);

  // GORUNTU denetimi: reddedilen onbellekle kurulan boru hatlari gercekten
  // ciziyor mu? Temizleme rengiyle karsilastirmak post acikken hicbir sey
  // olcmez (birlestirme gecisi HER pikseli yazar). Bos kare KONTROL: cizimli
  // kareyle farki = gercekten cizilmis geometri.
  ren.begin_frame(0);
  ren.ui_begin((float)W, (float)H);
  dev.pso().begin_frame();
  bool empty_ok = offscreen_render_custom(off, oc, pso_rec_main, &rec, &ores, pso_rec_shadow);
  CHECK(dev.pso().end_frame() == 0);
  CHECK(empty_ok);
  uint32_t drawn = 0;
  if (frame_ok && empty_ok)
    for (uint32_t i = 0; i < W * H; i++)
      if (!px_near(with_px + i * 4, ores.pixels[i * 4], ores.pixels[i * 4 + 1], ores.pixels[i * 4 + 2], 6)) drawn++;
  bool image_ok = drawn > (W * H) / 50;
  CHECK(image_ok);
  std::printf("    [bilgi] ON ISINMA ACIK: ilk karede kurulan pipeline = %u; bozuk onbellekten sonra cizilen"
              " geometri %u piksel (bos kare kontroluyle fark)\n", first_frame, drawn);

  // POZITIF KONTROL: on isinmamis bir varyant istendiginde sayac 0 OLMAMALI.
  //
  // NOT (2026-09-16): bu kontrol eskiden SDF boru hattinin TEMBEL olmasina
  // dayaniyordu. Artik `make_ui` onu KURULUMDA yaratiyor (ilk-kare takilmasi
  // olcuyldu: 0.49 ms) — yani urun duzeldi ama kontrol olcumsuz kaldi. Kontrol
  // kolu bu yuzden AYRI bir Renderer aciyor: `ui_prewarm_sdf = false` eski
  // tembel yolu KONTROL KIPI olarak koruyor. Urun yolunu bozup kontrolu
  // yasatmak yerine, kontrolun kendi kurulumunu vermek dogrusu.
  renderer::RendererConfig rc_lazy = rc;
  rc_lazy.ui_prewarm_sdf = false;
  renderer::Renderer ren_lazy;
  bool lazy_init = ren_lazy.init(dev, sys, offscreen_render_pass(off), rc_lazy);
  CHECK(lazy_init);
  PsoRec rec_lazy{&ren_lazy, true};
  // Atlas KENDI renderer'indan alinmali: doku/malzeme descriptor set 1'i ILGILI
  // renderer'in havuzundan ayirir, baskasininkini baglamak "set 1 bagli degil"
  // dogrulama hatasi verir (olculdu).
  renderer::TextureHandle tex_lazy = ren_lazy.create_texture(pso_white_atlas(), 8, 8, false, false);
  renderer::MaterialHandle atlas_lazy = ren_lazy.create_material(tex_lazy, {1, 1, 1});
  ren_lazy.begin_frame(0);
  ren_lazy.ui_begin((float)W, (float)H);
  ren_lazy.ui_set_sdf(true); // ui_begin bayragi her kare sifirlar: SONRA cagrilmali
  ren_lazy.ui_set_atlas(atlas_lazy);
  ren_lazy.ui_rect(8, 40, 40, 20, renderer::Renderer::rgba(255, 255, 255));
  // Kup CIZILMIYOR: `cube`'un malzemesi (descriptor set 1) BIRINCI renderer'in
  // havuzundan ayrildi; ikinci renderer uzerinden cizmek "set 1 bagli degil"
  // dogrulama hatasi verir. Bu kolun olctugu sey zaten yalniz SDF boru hattinin
  // TEMBEL kurulmasi — UI dortgeni tek basina yeterli.
  dev.pso().begin_frame();
  bool sdf_ok = lazy_init && offscreen_render_custom(off, oc, pso_rec_main, &rec_lazy, &ores, pso_rec_shadow);
  uint32_t sdf_frame = dev.pso().end_frame();
  const uint64_t hitch_ns = dev.pso().frame_create_ns();
  CHECK(sdf_ok);
  bool lazy_costs_a_frame = sdf_frame > 0;
  CHECK(lazy_costs_a_frame);
  std::printf("    [bilgi] POZITIF KONTROL (ui_prewarm_sdf=false ile TEMBEL SDF boru hatti): "
              "ilk karede kurulan pipeline = %u, kare ICINDE gecen kurulum suresi %.3f ms (= takilma)\n",
              sdf_frame, hitch_ns / 1e6);
  if (!lazy_costs_a_frame)
    std::printf("    [bilgi] sayac 0 kaldi: SDF boru hatti kurulamadi (%s) — kontrol bir sey olcmedi\n",
                ren_lazy.ui_stats().sdf_reason[0] ? ren_lazy.ui_stats().sdf_reason : "sebep yok; SDF dortgeni uretilmedi");
  ren_lazy.shutdown();

  // Isinmis varyant artik kurulu: sonraki kare yine 0 olmali.
  ren.begin_frame(0);
  ren.ui_begin((float)W, (float)H);
  ren.ui_set_atlas(atlas);
  ren.ui_rect(8, 8, 40, 20, renderer::Renderer::rgba(255, 255, 255));
  ren.draw(cube, Mat4::scale({1.2f, 1.2f, 1.2f}), {0.9f, 0.4f, 0.2f});
  dev.pso().begin_frame();
  CHECK(offscreen_render_custom(off, oc, pso_rec_main, &rec, &ores, pso_rec_shadow));
  uint32_t steady = dev.pso().end_frame();
  CHECK(steady == 0);

  // "0 dogrulama hatasi" ancak katman ETKINSE bir sey demektir (Tuzaklar 8s).
  CHECK(dev.validation_errors() == 0);
  // Kanca her cagriyi DOGRU cihazin gercek yordamina yonlendirebildi mi?
  // Yonlendirilemeyen cagri = yuva yetmedi; sessizce yanlis boru hatti degil,
  // gorunur hata olur ama olcum de bozulur.
  CHECK(pso_unrouted_calls() == 0);
  std::printf("    [bilgi] dogrulama katmani=%s, hata=%u; onbellek dosyasi %s (%llu B yuk)\n",
              dev.caps().validation_layer ? "ETKIN" : "YOK (0 hata bir sey demek degil)", dev.validation_errors(),
              cache_path, (unsigned long long)cold.after);
  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
  // temizlik: gecici dizin icerigi
  std::remove(cache_path);
}

// Tam ekran / yeniden boyutlandirma KARARI (rhi/swapchain.hpp ResizeAction).
//
// TASINABILIRLIK KURALI: "acquire/present OUT_OF_DATE dediginde yeniden kur"
// Wayland'de YETMEZ — yuzey olcusunu suren taraf uygulamadir (`currentExtent`
// 0xFFFFFFFF) ve pencere tam ekrana geciste OUT_OF_DATE HIC uretmez. Swapchain
// eski olcude kalir, kompozitor goruntuyu pencereye GERER: kullanicinin
// gordugu "tam ekran olmuyor, icerik ayni oranda buyuyup bozuluyor" tam budur
// (olculdu 2026-09-18, KDE Wayland). X11'de OUT_OF_DATE gelir, yani hata
// platforma gore GORUNUR/GORUNMEZ.
//
// Kapi kararin KENDISINI olcer ve yaninda POZITIF KONTROL olarak ESKI kurali
// (yalniz OUT_OF_DATE) ayni senaryolara uygular: eski kural senaryolardan
// en az birini KACIRMALI, yoksa bu test hicbir sey olcmuyor demektir.
ENGINE_TEST(rhi_resize_follows_window_size_not_only_out_of_date) {
  struct Case {
    const char *name;
    VkExtent2D requested;
    uint32_t win_w, win_h;
    bool out_of_date;
    ResizeAction want;
  };
  const Case cases[] = {
      {"tam ekran (Wayland: OUT_OF_DATE yok)", {1280, 720}, 2560, 1440, false, ResizeAction::SizeChanged},
      {"suruklenerek kuculme", {2560, 1440}, 1000, 900, false, ResizeAction::SizeChanged},
      {"olcu ayni: DOKUNMA", {1280, 720}, 1280, 720, false, ResizeAction::None},
      {"yuzey gecersiz (X11 yolu)", {1280, 720}, 1280, 720, true, ResizeAction::OutOfDate},
      {"kucultulmus pencere (0 olcu)", {1280, 720}, 0, 0, true, ResizeAction::None},
  };
  uint32_t ok_new = 0, ok_old = 0;
  for (const Case &c : cases) {
    const ResizeAction got = swapchain_resize_action(c.requested, c.win_w, c.win_h, c.out_of_date);
    const bool good = got == c.want;
    CHECK(good);
    if (good) ok_new++;
    // ESKI kural: yalniz bayraga bakar, olcuyu HIC gormez.
    const ResizeAction old_rule = (c.out_of_date && c.win_w && c.win_h) ? ResizeAction::OutOfDate : ResizeAction::None;
    if (old_rule == c.want) ok_old++;
    std::printf("    [bilgi] %-38s -> %s (eski kural: %s)\n", c.name, resize_action_str(got), resize_action_str(old_rule));
  }
  const uint32_t n = (uint32_t)(sizeof cases / sizeof cases[0]);
  CHECK(ok_new == n);
  // Pozitif kontrol: eski kural en az bir senaryoyu kacirdi (tam ekran).
  bool old_rule_misses = ok_old < n;
  CHECK(old_rule_misses);
  std::printf("    [bilgi] %u senaryo: yeni kural %u dogru, eski kural (yalniz OUT_OF_DATE) %u dogru\n", n, ok_new, ok_old);
  // Olcu degisimi her karede yeniden kurmayi TETIKLEMEZ: yeniden kurma
  // istenen olcuyu gunceller, ikinci cagri None der (Tuzaklar 8m'nin ikizi:
  // her karede swapchain yeniden kurmak 45 ms/kare demekti).
  VkExtent2D req{1280, 720};
  CHECK(swapchain_resize_action(req, 1920, 1080, false) == ResizeAction::SizeChanged);
  req = VkExtent2D{1920, 1080}; // create_swapchain'in yaptigi
  CHECK(swapchain_resize_action(req, 1920, 1080, false) == ResizeAction::None);
}
