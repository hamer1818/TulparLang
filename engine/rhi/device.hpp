// L2 RHI — Device: instance + fiziksel cihaz secimi + mantiksal cihaz +
// kuyruk + bellek alt-ayiricisi + zaman damgasi. Yuzey (pencere) YOK:
// Faz 1 ilk pikseli offscreen cizer (pencere acmadan dogrulanir; pencere
// Android host'la gelir). Userland tipi yok (Mesh/Material/Camera bilmez).
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "rhi/vk_api.hpp"

namespace tulpar::engine::rhi {

// Cihaz yetenekleri: plan L2 "zorunlu feature" listesi + Faz 1 uzantilari.
// Hepsi RAPORLANIR (cihaz matrisi verisi); zorunlu olanlar eksikse init
// basarisiz ve neden yazilir.
struct DeviceCaps {
  char device_name[256] = {0};
  uint32_t api_version = 0;       // fiziksel cihazin destekledigi
  uint32_t driver_version = 0;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  VkPhysicalDeviceType device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
  float timestamp_period_ns = 0;  // 0 = zaman damgasi yok
  bool timestamps = false;
  bool descriptor_indexing = false;   // zorunlu (bindless)
  bool timeline_semaphore = false;    // zorunlu
  bool buffer_device_address = false; // zorunlu
  bool draw_indirect = false;         // zorunlu (multiDrawIndirect DEGIL)
  char missing_mandatory[96] = {0};   // eksik "zorunlu"lar (bos = tam); cihaz matrisi verisi
  bool lazily_allocated_memory = false; // transient attachment icin (TBDR'da var)
  bool texture_compression_astc_ldr = false; // ASTC LDR (Mali/Adreno var, masaustu NVIDIA yok -> CPU coz)
  bool ext_subpass_merge_feedback = false;
  bool ext_graphics_pipeline_library = false;
  bool graphics_pipeline_library = false; // uzanti + feature acik (kullanilabilir)
  bool validation_layer = false;          // VK_LAYER_KHRONOS_validation etkin
  bool best_practices = false;            // katmanin BestPractices + Arm kurallari acik (Mali linter)
  bool debug_messenger = false;           // mesajlar sayiliyor (yoksa "0 hata" hicbir sey demek degil)
  bool ext_host_image_copy = false;
  bool khr_fragment_shading_rate = false;
  bool khr_portability_subset = false; // MoltenVK
};

struct DeviceConfig {
  // "cpu" = lavapipe/CPU cihazini tercih et (CI belirlenimli olsun);
  // "" = ayrik > tumlesik > sanal > cpu.
  const char *prefer = "";
  // Plan L2 "zorunlu" listesi (descriptorIndexing, timelineSemaphore, BDA) 1.2
  // cekirdek; Dusuk sinif (Mali-G72, 2018 surucusu, Vulkan 1.1) hicbirini vermiyor
  // (Huawei P20 Pro, 2026-09-14). Varsayilan: RAPORLA (caps.missing_mandatory),
  // reddetme — eksik yol yedegiyle calisir. true: eskisi gibi reddet.
  bool require_mandatory = false;
  // Dogrulama katmani (VK_LAYER_KHRONOS_validation) varsa etkinlestir; hatalar
  // sayilir (validation_errors) ve ilk birkaci basilir. Testler 0 bekler.
  bool validation = false;
  // Mali "en iyi uygulama" denetimi: PerfDoc'un (Arm, arsivlendi) ardili olan
  // Khronos katmani BestPractices + Arm satici kurallari (validate_best_practices_arm).
  // Vulkan'in KOTU kullanimi test kapisini kirar (layer_check.py'nin GPU esi).
  // validation ile birlikte; katman yoksa caps.best_practices=false (bilgi).
  bool best_practices = false;
  // Pencere varsa: VK_KHR_surface + platform uzantilari (Window verir).
  const char *const *instance_extensions = nullptr;
  uint32_t instance_extension_count = 0;
};

// Bellek: tur basina buyuk blok, bump; serbest birakma yok (cihaz omru).
// vkAllocateMemory sayisi sabit ve az (plan L2). Kare icinde cagrilmaz.
struct MemoryBlock {
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint32_t type_index = 0;
  VkDeviceSize size = 0;
  VkDeviceSize used = 0;
  void *mapped = nullptr;
};

struct MemoryAlloc {
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize offset = 0;
  VkDeviceSize size = 0;
  void *mapped = nullptr; // host-visible ise
};

class Device {
public:
  // Tek adim (yuzeysiz, offscreen/test).
  bool init(Arena &arena, VkApi &api, const DeviceConfig &cfg);
  // Iki adim (pencere): once instance, uygulama yuzeyi kurar, sonra cihaz.
  bool init_instance(VkApi &api, const DeviceConfig &cfg);
  bool init_device(VkSurfaceKHR surface);
  void shutdown();
  bool ok() const { return device_ != VK_NULL_HANDLE; }
  const DeviceCaps &caps() const { return caps_; }
  const char *last_error() const { return err_; }
  uint32_t validation_errors() const { return validation_errors_; }
  void count_validation_error() { validation_errors_++; }
  // BestPractices (performans) uyarilari: kimlik basina sayilir, ilk 8 benzersizi basilir.
  // "-Arm-" kimlikleri Mali'ye ozgu; kapi bunlarin 0 olmasini bekler.
  uint32_t validation_warnings() const { return validation_warnings_; }
  uint32_t best_practice_warnings() const { return bp_warnings_; }
  uint32_t best_practice_arm_warnings() const { return bp_arm_warnings_; }
  static constexpr uint32_t kBpIds = 32;
  struct BpId { char name[96]; uint32_t count; bool arm; };
  uint32_t best_practice_id_count() const { return bp_id_n_; }
  const BpId &best_practice_id(uint32_t i) const { return bp_ids_[i]; }
  uint32_t best_practice_count(const char *id_substring) const; // kimlik altdizgisiyle toplam
  void on_validation_message(uint32_t severity, const char *id, const char *message);

  VkApi &api() { return *api_; }
  VkInstance instance() const { return instance_; }
  // Android: pencere yeniden yaratilinca (TERM_WINDOW -> INIT_WINDOW) yeni yuzey.
  // Once swapchain kapatilmis olmali. Eskisi yok edilir.
  void replace_surface(VkSurfaceKHR s);
  VkPhysicalDevice physical() const { return phys_; }
  VkDevice handle() const { return device_; }
  VkQueue queue() const { return queue_; }
  uint32_t queue_family() const { return queue_family_; }
  VkCommandPool command_pool() const { return cmd_pool_; }

  // required: bellek turu maskesi (VkMemoryRequirements.memoryTypeBits)
  // flags: istenen ozellikler; lazily_ok: LAZILY_ALLOCATED tercih edilsin
  bool allocate(const VkMemoryRequirements &req, VkMemoryPropertyFlags flags, bool lazily_ok,
                MemoryAlloc *out);
  // Kendi vkAllocateMemory'si olan, SERBEST BIRAKILABILIR ayirma. Blok ayirici
  // bump'tir ve geri vermez; omru sahnenin degil PENCERENIN olan seyler (swapchain
  // derinligi) her yeniden boyutlandirmada blok yerdi. Bunlar bununla ayrilir.
  bool allocate_dedicated(const VkMemoryRequirements &req, VkMemoryPropertyFlags flags, bool lazily_ok,
                          MemoryAlloc *out);
  void free_dedicated(MemoryAlloc *a);
  uint32_t memory_allocation_count() const { return block_count_; }
  uint32_t dedicated_allocation_count() const { return dedicated_count_; }

  // Tek seferlik komut tamponu: kaydet, gonder, bekle (Faz 1 offscreen).
  VkCommandBuffer begin_one_shot();
  bool end_one_shot_and_wait(VkCommandBuffer cb, uint64_t timeout_ns = 5000000000ull);

private:
  bool pick_physical(const DeviceConfig &cfg, VkSurfaceKHR surface);
  DeviceConfig cfg_{};
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  bool has_swapchain_ext_ = false;
  int find_memory_type(uint32_t mask, VkMemoryPropertyFlags flags) const;
  void fail(const char *msg, VkResult r);

  VkApi *api_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice phys_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = 0;
  uint32_t instance_version_ = 0;
  // 1.1 cihazda uzanti bicimleri (1.2 cekirdegi yoksa): init_device bunlari acar.
  bool ext_descriptor_indexing_ = false, ext_timeline_semaphore_ = false, ext_buffer_device_address_ = false;
  VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
  VkFence one_shot_fence_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  uint32_t validation_errors_ = 0;
  uint32_t validation_warnings_ = 0, bp_warnings_ = 0, bp_arm_warnings_ = 0, bp_id_n_ = 0;
  BpId bp_ids_[kBpIds]{};
  VkPhysicalDeviceMemoryProperties mem_props_{};
  DeviceCaps caps_{};
  static constexpr uint32_t kMaxBlocks = 16;
  static constexpr VkDeviceSize kBlockSize = 64ull << 20; // 64 MB
  MemoryBlock blocks_[kMaxBlocks];
  uint32_t block_count_ = 0;
  uint32_t dedicated_count_ = 0;
  char err_[256] = {0};
};

} // namespace tulpar::engine::rhi
