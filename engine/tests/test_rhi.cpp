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
  CHECK(dev.ok());
  CHECK(c.descriptor_indexing && c.timeline_semaphore && c.buffer_device_address);
  dev.shutdown();
}

ENGINE_TEST(rhi_first_pixel_offscreen_triangle) {
  if (!loader()) { test::skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  Device dev;
  if (!open_device(sys, dev)) { test::skip("Vulkan cihazi yok"); return; }
  char dir[] = "/tmp/engine_rhi_XXXXXX";
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
    if (dev.caps().timestamps) CHECK(r1.gpu_ns > 0);
  }
  // 2. cizim: cache dosyasi var -> yuklenmeli; bizim kodumuz kare icinde new yapmaz.
  OffscreenResult r2;
  AllocGate::begin_frame();
  ok = render_triangle_offscreen(dev, sys, cfg, &r2);
  uint64_t allocs = AllocGate::end_frame();
  CHECK(ok);
  CHECK(r2.pso_cache_loaded);
  CHECK(allocs == 0); // surucu ici malloc sayilmaz; bizim new yok
  if (ok) {
    const uint8_t *center = r2.pixels + ((cfg.height / 2) * cfg.width + cfg.width / 2) * 4;
    CHECK(px_near(center, 255, 128, 0));
  }
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

