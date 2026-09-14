// L2 RHI — Faz 1 "ilk piksel": pencere ACMADAN, subpass zincirli (depth
// prepass -> renk) bir render pass ile ucgen cizip pikselleri geri okur.
// Kapilar burada olculur: piksel dogrulugu, GPU zaman damgasi, PSO cache
// yukleme/kaydetme, VK_EXT_subpass_merge_feedback (varsa) birlesme durumu.
// Depth transient (STORE_OP_DONT_CARE, mumkunse LAZILY_ALLOCATED): TBDR'da
// tile'da kalir, DRAM'e inmez — plan §1.2 "zorunlu" maddesinin ilk kullanimi.
#pragma once
#include <cstdint>

#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "rhi/command_pools.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::rhi {

struct OffscreenConfig {
  uint32_t width = 256;
  uint32_t height = 256;
  uint8_t clear[4] = {10, 20, 30, 255};
  const char *pso_cache_path = nullptr; // varsa yukle, sonunda yaz
  // Paralel kayit: renk subpass'i `parallel_jobs` job'a bolunur, her job
  // kendi thread'inin havuzundan ikincil tampon kaydeder (yatay bant), ana
  // thread vkCmdExecuteCommands ile calistirir. 0 = satir ici (tek thread).
  JobSystem *jobs = nullptr;
  CommandPools *pools = nullptr;
  uint32_t parallel_jobs = 0;
  // Renk pipeline'ini VK_EXT_graphics_pipeline_library ile 4 kutuphaneden
  // LINKLE (plan Faz 1: PSO yukleme ucuz olmali). Cihaz desteklemiyorsa
  // monolitik'e duser ve bunu raporlar.
  bool use_pipeline_library = false;
  // Renk hedefi sRGB (R8G8B8A8_SRGB): shader dogrusal yazar, donanim kodlar; okunan
  // bayt ekrandaki gibi kodludur. false: UNORM (RHI ucgen testleri tam renk bekler).
  bool srgb = false;
};

struct OffscreenResult {
  bool ok = false;
  char error[512] = {0};
  uint8_t *pixels = nullptr; // RGBA8, arenadan, width*height*4
  uint64_t gpu_ns = 0;       // render pass suresi (zaman damgasi); 0 olabilir (MoltenVK iki damgayi ayni verir)
  bool timestamps_valid = false; // sorgu basariyla okundu (deger 0 olsa da)
  // Subpass birlesme geri bildirimi (VK_EXT_subpass_merge_feedback):
  bool merge_feedback_available = false;
  uint32_t post_merge_subpass_count = 0; // 1 = iki subpass birlesti
  int32_t subpass_merge_status[2] = {-1, -1}; // VkSubpassMergeStatusEXT
  // PSO cache:
  bool pso_cache_loaded = false; // dosya vardi ve baslik cihazla eslesti
  uint64_t pso_cache_bytes = 0;  // yazilan
  uint32_t memory_allocations = 0; // vkAllocateMemory sayisi (cihaz toplam)
  uint32_t secondaries_recorded = 0; // paralel kayitta kullanilan ikincil tampon
  uint32_t recording_threads = 0;    // kac farkli thread yuvasi kayit yapti
  // Pipeline olusturma sureleri (kurulum, bilgi):
  uint64_t pipeline_monolithic_ns = 0; // renk pipeline'i monolitik
  uint64_t pipeline_library_ns = 0;    // 4 kutuphane (GPL)
  uint64_t pipeline_link_ns = 0;       // kutuphanelerden link
  bool pipeline_library_used = false;
};

// Tek adim: kurulum + kare + yikim (kolaylik). A2 iddiasi icin asagidaki
// ayrik API kullanilir: kurulum surucu icinde ayirma yapar (pipeline, image,
// JIT — lavapipe'ta operator new ile gorunur), KARE yapmamali.
bool render_triangle_offscreen(Device &dev, Arena &arena, const OffscreenConfig &cfg,
                               OffscreenResult *out);

// Ayrik API: kurulum bir kez, kare N kez.
struct OffscreenTarget;
OffscreenTarget *offscreen_create(Device &dev, Arena &arena, const OffscreenConfig &cfg,
                                  OffscreenResult *out);
// Bir kare: kayit + gonderim + bekleme + piksel geri okuma. Bizim kodda
// ayirma YOK (AllocGate ile test edilir); out->pixels arenadan (kurulumda).
bool offscreen_render_frame(OffscreenTarget *t, const OffscreenConfig &cfg, OffscreenResult *out);
void offscreen_destroy(OffscreenTarget *t);
VkRenderPass offscreen_render_pass(OffscreenTarget *t);
// Ozel kayit: render pass baslatilir (subpass 0, viewport/scissor ayarli),
// `record(cb, user)` cagrilir (prepass cizimleri, vkCmdNextSubpass, renk),
// sonra bitirilip pikseller okunur. Renderer'in headless dogrulamasi icin.
typedef void (*OffscreenRecordFn)(VkCommandBuffer cb, void *user);
// `before` (varsa) render pass BASLAMADAN once cagrilir: kendi pass'i olan
// isler (golge haritasi) buraya kaydedilir.
bool offscreen_render_custom(OffscreenTarget *t, const OffscreenConfig &cfg, OffscreenRecordFn record, void *user,
                             OffscreenResult *out, OffscreenRecordFn before = nullptr);

// Basit PPM (P6) yazici — insan gozu icin; test artefakti.
bool write_ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h);

} // namespace tulpar::engine::rhi
