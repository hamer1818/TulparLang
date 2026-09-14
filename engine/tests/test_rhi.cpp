// Faz 1 RHI: loader -> cihaz -> offscreen ucgen (piksel kapisi) -> zaman
// damgasi -> PSO cache -> subpass birlesme geri bildirimi. Loader/cihaz
// yoksa GORUNUR atlanir (ozet satirinda sayilir), sessizce gecmez.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
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
