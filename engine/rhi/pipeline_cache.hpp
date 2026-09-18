// L2 RHI — Kalici boru hatti (PSO) onbellegi + on isinma/kurulum olcumu.
//
// SORUN (PLAN Faz 6'nin acik kalemi): SPIR-V derleme zamaninda uretiliyor
// (rhi/shaders/*_spv.h), ama PIPELINE NESNESI hala ilk kullanimda kuruluyor.
// Surucu o anda shader'i cihazin ISA'sina derler; mobilde bu, oyunun ortasinda
// gorunen bir takilmadir. Iki ayri eksik var ve ikisi de burada olculur:
//   1. Kurulan pipeline surecin omruyle sinirliydi — ikinci acilista surucu
//      ayni derlemeyi bastan yapiyordu. Cozum: VkPipelineCache'i diske yaz/oku.
//   2. Bazi pipeline'lar ILK KARE ICINDE kuruluyor (tembel yol). Cozum once
//      OLCMEK: kac pipeline, ne kadar surede, hangi karede kuruldu.
//
// ONBELLEK DOSYASI SURUCUYE OZGUDUR. Yabanci ya da bayat bir dosyayi
// vkCreatePipelineCache'e vermek tanimsiz davranistir; "calisiyor gibi gorunup"
// cokmek tam olarak bu projenin sessiz-yanlis sinifidir. Bu yuzden yukleme uc
// katmanda dogrular ve reddi GORUNUR yapar (PsoCacheStats::reject):
//   a. bizim sarmalayici basligimiz — magic, surum, boyut, yukun FNV-1a ozeti
//      (kesik/kurcalanmis dosya),
//   b. cihaz kimligi — vendorID, deviceID, driverVersion, pipelineCacheUUID
//      (baska GPU ya da surucu guncellemesi),
//   c. Vulkan'in kendi basligi (VkPipelineCacheHeaderVersionOne).
// Biri bile uymazsa dosya KULLANILMAZ; onbellek bos kurulur ve motor dogru
// calismaya devam eder.
//
// BAGLAMA (interposition): motorun geri kalani pipeline'i dogrudan VkApi
// tablosundaki vkCreateGraphicsPipelines ile ve cache olarak VK_NULL_HANDLE
// vererek kuruyor (renderer.cpp'de 18 cagri yeri, editor'un ImGui arka ucu,
// offscreen). Device acilinca tabloya bir ara yordam takilir: cagiran cache
// vermediyse cihazin kalici onbellegi kullanilir, kurulum sayilir ve suresi
// olculur. Boylece tek bir cagri yeri bile degistirmeden butun pipeline'lar
// onbellekten faydalanir ve sayaca girer.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>

#include "platform/fs.hpp"

#include "platform/time.hpp"
#include "rhi/vk_api.hpp"

namespace tulpar::engine::rhi {

// Dosya neden kullanilmadi. "Dosya yok" da bir sebeptir: sessiz false yok.
enum class PsoReject : uint8_t {
  None = 0,
  Disabled,      // yol verilmedi / kapatildi
  NoFile,        // dosya acilamadi (ilk calistirma)
  ReadError,     // acildi ama okunamadi
  TooSmall,      // baslik bile sigmiyor
  Magic,         // bizim sarmalayici degil
  WrapVersion,   // sarmalayici surumu farkli
  SizeMismatch,  // baslik yuk boyutu dosyayla tutmuyor (kesik)
  PayloadHash,   // yuk ozeti tutmuyor (kurcalanmis/bozuk)
  VendorId,      // baska satici
  DeviceId,      // baska GPU
  DriverVersion, // surucu guncellenmis
  CacheUuid,     // pipelineCacheUUID degismis (surucunun kendi gecersizlestirmesi)
  VkHeader,      // Vulkan'in kendi basligi bozuk
  Empty,         // yuk bos
};

inline const char *pso_reject_str(PsoReject r) {
  switch (r) {
  case PsoReject::None: return "-";
  case PsoReject::Disabled: return "kapali (yol yok)";
  case PsoReject::NoFile: return "dosya yok (ilk calistirma)";
  case PsoReject::ReadError: return "dosya okunamadi";
  case PsoReject::TooSmall: return "dosya baslik kadar bile degil";
  case PsoReject::Magic: return "magic tutmuyor (bizim dosya degil)";
  case PsoReject::WrapVersion: return "sarmalayici surumu farkli";
  case PsoReject::SizeMismatch: return "boyut tutmuyor (kesik dosya)";
  case PsoReject::PayloadHash: return "yuk ozeti tutmuyor (bozuk/kurcalanmis)";
  case PsoReject::VendorId: return "baska satici (vendorID)";
  case PsoReject::DeviceId: return "baska GPU (deviceID)";
  case PsoReject::DriverVersion: return "surucu surumu degismis (driverVersion)";
  case PsoReject::CacheUuid: return "pipelineCacheUUID degismis";
  case PsoReject::VkHeader: return "Vulkan onbellek basligi bozuk";
  case PsoReject::Empty: return "yuk bos";
  }
  return "?";
}

constexpr uint32_t kPsoWrapVersion = 1;
static constexpr char kPsoMagic[8] = {'T', 'L', 'P', 'R', 'P', 'S', 'O', '1'};

// 64 bayt; alanlar sabit genislikte ve dolgusuz (dosya formati).
struct PsoFileHeader {
  char magic[8];
  uint32_t wrap_version;
  uint32_t vendor_id;
  uint32_t device_id;
  uint32_t driver_version;
  uint32_t api_version;
  uint32_t payload_bytes;
  uint64_t payload_hash; // FNV-1a 64, yalniz yuk uzerinde
  uint8_t cache_uuid[VK_UUID_SIZE];
  uint32_t reserved[2];
};
static_assert(sizeof(PsoFileHeader) == 64, "PSO onbellek basligi 64 bayt (dosya formati)");

inline uint64_t pso_fnv1a(const void *p, size_t n) {
  const uint8_t *b = static_cast<const uint8_t *>(p);
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

// Onbellegin ait oldugu cihazin kimligi (DeviceCaps'ten doldurulur).
struct PsoDeviceId {
  uint32_t vendor_id = 0, device_id = 0, driver_version = 0, api_version = 0;
  uint8_t cache_uuid[VK_UUID_SIZE] = {};
};

struct PsoCacheStats {
  bool loaded = false;                    // disk verisi KULLANILDI
  PsoReject reject = PsoReject::Disabled; // kullanilmadiysa neden
  uint64_t file_bytes = 0;                // diskteki dosya (baslik dahil)
  uint64_t payload_bytes = 0;             // yuklenen surucu verisi
  uint64_t saved_bytes = 0;               // son yazmada diske giden yuk
  uint32_t created = 0;             // bu cihazda kurulan pipeline (toplam)
  uint64_t create_ns = 0;           // toplam kurulum suresi
  uint32_t frame_created = 0;       // ACIK olan kare icinde kurulan
  uint32_t first_frame_created = 0; // 1. karede kurulan (kapi: 0)
  uint32_t frames = 0;
};

// --- Serbest yordamlar (PsoCache de bunlari kullanir) ------------------------
// Ham VkPipelineCache tutan cagiranlar (ornegin offscreen) ayni dogrulayiciyi
// kullansin diye ayri: dosya bicimi ve reddetme kurallari TEK yerde.

// Dosyayi acar, UC KATMANDA dogrular ve yuku malloc'lu bir tampona okur.
// Donus None ise *payload cagiranin (std::free ile birakir); aksi halde nullptr.
inline PsoReject pso_cache_read_file(const char *path, const PsoDeviceId &id, void **payload, size_t *payload_n,
                                     uint64_t *file_bytes) {
  *payload = nullptr;
  *payload_n = 0;
  if (file_bytes) *file_bytes = 0;
  if (!path || !*path) return PsoReject::Disabled;
  FILE *f = std::fopen(path, "rb");
  if (!f) return PsoReject::NoFile;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < (long)sizeof(PsoFileHeader)) {
    std::fclose(f);
    return PsoReject::TooSmall;
  }
  if (file_bytes) *file_bytes = (uint64_t)n;
  PsoFileHeader h{};
  if (std::fread(&h, 1, sizeof h, f) != sizeof h) {
    std::fclose(f);
    return PsoReject::ReadError;
  }
  // (a) sarmalayici: bizim dosyamiz mi, kesik mi?
  PsoReject bad = PsoReject::None;
  if (std::memcmp(h.magic, kPsoMagic, sizeof h.magic) != 0) bad = PsoReject::Magic;
  else if (h.wrap_version != kPsoWrapVersion) bad = PsoReject::WrapVersion;
  else if ((uint64_t)h.payload_bytes + sizeof(PsoFileHeader) != (uint64_t)n) bad = PsoReject::SizeMismatch;
  else if (h.payload_bytes == 0) bad = PsoReject::Empty;
  // (b) cihaz kimligi: bunlardan biri degistiyse dosya BASKA bir surucunun.
  else if (h.vendor_id != id.vendor_id) bad = PsoReject::VendorId;
  else if (h.device_id != id.device_id) bad = PsoReject::DeviceId;
  else if (h.driver_version != id.driver_version) bad = PsoReject::DriverVersion;
  else if (std::memcmp(h.cache_uuid, id.cache_uuid, VK_UUID_SIZE) != 0) bad = PsoReject::CacheUuid;
  if (bad != PsoReject::None) {
    std::fclose(f);
    return bad;
  }
  // Kurulum zamani, bir kerelik: malloc + hemen free. Kare icinde cagrilmaz
  // (AllocGate global operator new'i sayar, malloc'u degil — alloc_gate.hpp).
  void *buf = std::malloc(h.payload_bytes);
  if (!buf) {
    std::fclose(f);
    return PsoReject::ReadError;
  }
  size_t got = std::fread(buf, 1, h.payload_bytes, f);
  std::fclose(f);
  if (got != h.payload_bytes) {
    std::free(buf);
    return PsoReject::ReadError;
  }
  if (pso_fnv1a(buf, h.payload_bytes) != h.payload_hash) {
    std::free(buf);
    return PsoReject::PayloadHash;
  }
  // (c) Vulkan'in kendi basligi: sarmalayici saglam olsa bile (ornegin elle
  // yeniden ozetlenmis bir dosya) yuk surucuye ait degilse verilmez.
  if (h.payload_bytes < sizeof(VkPipelineCacheHeaderVersionOne)) {
    std::free(buf);
    return PsoReject::VkHeader;
  }
  VkPipelineCacheHeaderVersionOne vh{};
  std::memcpy(&vh, buf, sizeof vh);
  if (vh.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE || vh.headerSize < sizeof vh ||
      vh.headerSize > h.payload_bytes || vh.vendorID != id.vendor_id || vh.deviceID != id.device_id ||
      std::memcmp(vh.pipelineCacheUUID, id.cache_uuid, VK_UUID_SIZE) != 0) {
    std::free(buf);
    return PsoReject::VkHeader;
  }
  *payload = buf;
  *payload_n = h.payload_bytes;
  return PsoReject::None;
}

// "a/b/c.bin" icin a ve a/b yaratilir (varsa dokunulmaz).
inline void pso_make_parent_dirs(const char *path) {
  char buf[600];
  std::snprintf(buf, sizeof buf, "%s", path);
  for (char *p = buf + 1; *p; p++) {
    if (*p != '/') continue;
    *p = 0;
    platform::fs_mkdir_one(buf);
    *p = '/';
  }
}

// vkGetPipelineCacheData -> gecici dosya -> rename. Yarim dosya BIRAKILMAZ:
// kesilmis bir onbellegi bir sonraki acilista okumak (ozet yakalasa bile)
// gereksiz bir ret ve bosa giden kurulum demektir.
inline bool pso_cache_write_file(VkApi &api, VkDevice dev, VkPipelineCache cache, const PsoDeviceId &id,
                                 const char *path, uint64_t *saved_bytes) {
  if (saved_bytes) *saved_bytes = 0;
  if (!cache || !path || !*path || !api.vkGetPipelineCacheData) return false;
  size_t n = 0;
  if (api.vkGetPipelineCacheData(dev, cache, &n, nullptr) != VK_SUCCESS || n == 0) return false;
  void *buf = std::malloc(n);
  if (!buf) return false;
  if (api.vkGetPipelineCacheData(dev, cache, &n, buf) != VK_SUCCESS || n == 0) {
    std::free(buf);
    return false;
  }
  PsoFileHeader h{};
  std::memcpy(h.magic, kPsoMagic, sizeof h.magic);
  h.wrap_version = kPsoWrapVersion;
  h.vendor_id = id.vendor_id;
  h.device_id = id.device_id;
  h.driver_version = id.driver_version;
  h.api_version = id.api_version;
  h.payload_bytes = (uint32_t)n;
  h.payload_hash = pso_fnv1a(buf, n);
  std::memcpy(h.cache_uuid, id.cache_uuid, VK_UUID_SIZE);
  char tmp[600];
  std::snprintf(tmp, sizeof tmp, "%s.tmp", path);
  pso_make_parent_dirs(path);
  FILE *f = std::fopen(tmp, "wb");
  if (!f) {
    std::free(buf);
    return false;
  }
  bool ok = std::fwrite(&h, 1, sizeof h, f) == sizeof h && std::fwrite(buf, 1, n, f) == n;
  std::fclose(f);
  std::free(buf);
  // Windows rename hedef varsa duser: PSO onbellegi ikinci kayitta
  // guncellenmezdi (platform/fs.hpp::fs_replace_file).
  if (!ok || platform::fs_replace_file(tmp, path) != 0) {
    std::remove(tmp);
    return false;
  }
  if (saved_bytes) *saved_bytes = n;
  return true;
}

// Dosyadan (varsa ve GECERLIYSE) beslenmis bir VkPipelineCache. Ret sebebi
// *reject'e yazilir; reddedilen dosya kullanilmaz ama onbellek yine kurulur.
inline VkPipelineCache pso_cache_create(VkApi &api, VkDevice dev, const PsoDeviceId &id, const char *path,
                                        PsoReject *reject, uint64_t *loaded_bytes, uint64_t *file_bytes = nullptr) {
  void *payload = nullptr;
  size_t payload_n = 0;
  PsoReject r = pso_cache_read_file(path, id, &payload, &payload_n, file_bytes);
  if (reject) *reject = r;
  if (loaded_bytes) *loaded_bytes = r == PsoReject::None ? payload_n : 0;
  VkPipelineCacheCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  ci.initialDataSize = r == PsoReject::None ? payload_n : 0;
  ci.pInitialData = r == PsoReject::None ? payload : nullptr;
  VkPipelineCache out = VK_NULL_HANDLE;
  if (api.vkCreatePipelineCache(dev, &ci, nullptr, &out) != VK_SUCCESS) out = VK_NULL_HANDLE;
  std::free(payload);
  return out;
}

// ON ISINMA: N varyanti TEK cagrida kurar. Iki kazanci var ve ikisi de
// olculebilir: (1) surucu ortak isi (shader derlemesi, onbellek kilidi) bir kez
// yapar — N ayri cagri yerine bir cagri; (2) — asil nokta — bu cagri ILK KARE
// CIZILMEDEN once dondugu icin maliyet kareye degil ACILISA yazilir.
// Varyant listesini RHI uydurmaz: onu bilen katman (renderer, offscreen) verir.
inline VkResult pso_warmup_pipelines(VkApi &api, VkDevice dev, VkPipelineCache cache, uint32_t n,
                                     const VkGraphicsPipelineCreateInfo *ci, VkPipeline *out, uint64_t *ns) {
  const uint64_t t0 = platform::now_ns();
  VkResult r = api.vkCreateGraphicsPipelines(dev, cache, n, ci, nullptr, out);
  if (ns) *ns = platform::now_ns() - t0;
  return r;
}

// Tek cihazin kalici pipeline onbellegi. Kurulum zamani nesnesi: kare icinde
// hicbir yolu cagrilmaz (yalniz sayaclari begin_frame/end_frame ile isaretlenir).
class PsoCache {
public:
  PsoCache() = default;
  PsoCache(const PsoCache &) = delete;
  PsoCache &operator=(const PsoCache &) = delete;

  // path == nullptr / "" -> yalniz bellek ici onbellek (disk yok, yine de surec
  // icinde paylasim saglar). Dosya reddedilirse false DONMEZ: onbellek bos
  // kurulur, sebep stats().reject'te durur ve motor dogru calisir.
  bool init(VkApi &api, VkDevice dev, const PsoDeviceId &id, const char *path) {
    shutdown(false);
    api_ = &api;
    dev_ = dev;
    id_ = id;
    path_[0] = 0;
    if (path && *path) std::snprintf(path_, sizeof path_, "%s", path);
    cache_ = pso_cache_create(api, dev, id_, path_, &reject_, &loaded_bytes_, &file_bytes_);
    return cache_ != VK_NULL_HANDLE;
  }

  // Ayni cihazda onbellegi BASKA bir dosyadan yeniden kur. Kapi soguk/sicak/
  // bozuk kosumlarini bununla tek cihaz uzerinde olcer: her kosum icin yeni
  // VkInstance acmak sonraki kapilari sessizce ATLANDI'ya dusurur (Tuzaklar 8al)
  // ve ayni VkApi tablosuyla ikinci cihaz acmak giris noktalarini ezer (8an).
  bool reinit(const char *path, bool save_current) {
    if (!api_ || !dev_) return false;
    VkApi &a = *api_;
    VkDevice d = dev_;
    PsoDeviceId id = id_;
    if (save_current) save();
    return init(a, d, id, path);
  }

  void shutdown(bool save_to_disk = true) {
    if (cache_ && api_ && dev_) {
      if (save_to_disk) save();
      api_->vkDestroyPipelineCache(dev_, cache_, nullptr);
    }
    cache_ = VK_NULL_HANDLE;
    reset_counters();
    loaded_bytes_ = file_bytes_ = saved_bytes_ = 0;
    reject_ = PsoReject::Disabled;
  }

  bool ok() const { return cache_ != VK_NULL_HANDLE; }
  VkPipelineCache handle() const { return cache_; }
  const char *path() const { return path_; }

  bool save() {
    if (!api_ || !dev_) return false;
    return pso_cache_write_file(*api_, dev_, cache_, id_, path_, &saved_bytes_);
  }

  // ON ISINMA: bilinen varyantlarin HEPSI tek vkCreateGraphicsPipelines
  // cagrisinda kurulur (bkz. pso_warmup_pipelines). Cagiran, varyant listesini
  // BILEN katmandir; RHI onu uydurmaz.
  VkResult warmup(uint32_t n, const VkGraphicsPipelineCreateInfo *ci, VkPipeline *out, uint64_t *ns = nullptr) {
    if (!api_ || !dev_) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t dt = 0;
    VkResult r = pso_warmup_pipelines(*api_, dev_, cache_, n, ci, out, &dt);
    if (ns) *ns = dt;
    note_created(r == VK_SUCCESS ? n : 0, dt);
    return r;
  }

  // Kare penceresi: arada kurulan pipeline sayilir. "Ilk karede 0 pipeline"
  // iddiasi ancak bu sayacla olculur.
  void begin_frame() {
    in_frame_.store(true, std::memory_order_relaxed);
    frame_created_.store(0, std::memory_order_relaxed);
    frame_ns_.store(0, std::memory_order_relaxed);
  }
  uint32_t end_frame() {
    const uint32_t n = frame_created_.load(std::memory_order_relaxed);
    in_frame_.store(false, std::memory_order_relaxed);
    if (frames_++ == 0) {
      first_frame_created_ = n;
      first_frame_ns_ = frame_ns_.load(std::memory_order_relaxed);
    }
    return n;
  }
  uint64_t frame_create_ns() const { return frame_ns_.load(std::memory_order_relaxed); }
  uint64_t first_frame_create_ns() const { return first_frame_ns_; }

  // Ara yordamin (ve create_graphics'in) cagirdigi sayim noktasi.
  void note_created(uint32_t n, uint64_t ns) {
    if (n == 0) return;
    created_.fetch_add(n, std::memory_order_relaxed);
    create_ns_.fetch_add(ns, std::memory_order_relaxed);
    if (in_frame_.load(std::memory_order_relaxed)) {
      frame_created_.fetch_add(n, std::memory_order_relaxed);
      frame_ns_.fetch_add(ns, std::memory_order_relaxed);
    }
  }

  void reset_counters() {
    created_.store(0, std::memory_order_relaxed);
    create_ns_.store(0, std::memory_order_relaxed);
    frame_created_.store(0, std::memory_order_relaxed);
    frame_ns_.store(0, std::memory_order_relaxed);
    in_frame_.store(false, std::memory_order_relaxed);
    frames_ = 0;
    first_frame_created_ = 0;
    first_frame_ns_ = 0;
  }

  PsoCacheStats stats() const {
    PsoCacheStats s;
    s.loaded = reject_ == PsoReject::None;
    s.reject = reject_;
    s.file_bytes = file_bytes_;
    s.payload_bytes = loaded_bytes_;
    s.saved_bytes = saved_bytes_;
    s.created = created_.load(std::memory_order_relaxed);
    s.create_ns = create_ns_.load(std::memory_order_relaxed);
    s.frame_created = frame_created_.load(std::memory_order_relaxed);
    s.first_frame_created = first_frame_created_;
    s.frames = frames_;
    return s;
  }

private:
  VkApi *api_ = nullptr;
  VkDevice dev_ = VK_NULL_HANDLE;
  PsoDeviceId id_{};
  VkPipelineCache cache_ = VK_NULL_HANDLE;
  char path_[512] = {0};
  PsoReject reject_ = PsoReject::Disabled;
  uint64_t file_bytes_ = 0, loaded_bytes_ = 0, saved_bytes_ = 0;
  std::atomic<uint32_t> created_{0}, frame_created_{0};
  std::atomic<uint64_t> create_ns_{0}, frame_ns_{0};
  std::atomic<bool> in_frame_{false};
  uint32_t frames_ = 0, first_frame_created_ = 0;
  uint64_t first_frame_ns_ = 0;
};

// --- VkApi ara yordami -------------------------------------------------------
// Motorun geri kalani vkCreateGraphicsPipelines'i cache olarak VK_NULL_HANDLE
// ile cagiriyor. Tabloya takilan bu yordam cihazin kalici onbellegini koyar ve
// kurulumu sayar. Cagiran kendi cache'ini verdiyse ona dokunulmaz.
namespace detail {

// Her yuva KENDI gercek isaretcisini tutar. GLOBAL tek bir "gercek" isaretci
// TUTULAMAZ: vkGetDeviceProcAddr'in verdigi yordam CIHAZA OZGUDUR (loader ya da
// dogrulama katmani zincirinde o cihazin gonderim tablosuna baglidir) ve cihaz
// yok edilince gecersizlesir. Ilk yazimda tek global vardi ve suite'i dusurdu:
// kendi cihazini acip kapatan bir test global isaretciyi KENDI (artik olu)
// cihazinin yordamiyla degistiriyor, sonra paylasilan cihaz uzerinden yapilan
// ilk vkCreateGraphicsPipelines SIGSEGV veriyordu. Tuzaklar 8an'in ayni ailesi.
struct PsoHookSlot {
  VkDevice dev = VK_NULL_HANDLE;
  PsoCache *cache = nullptr;
  PFN_vkCreateGraphicsPipelines real = nullptr;
};
inline PsoHookSlot g_pso_slots[4];
// Yuvasi olmayan bir cihaz icin cagri geldi: ileri gonderilemez (hangi gercek
// yordamin o cihaza ait oldugu bilinmiyor). Cokmek yerine HATA donulur ve
// sayilir — sessiz yanlis degil, gorunur basarisizlik.
inline uint32_t g_pso_unrouted = 0;

inline PsoHookSlot *pso_slot_of(VkDevice d) {
  for (PsoHookSlot &s : g_pso_slots)
    if (s.dev == d) return &s;
  return nullptr;
}

inline VKAPI_ATTR VkResult VKAPI_CALL pso_create_graphics_thunk(VkDevice d, VkPipelineCache c, uint32_t n,
                                                                const VkGraphicsPipelineCreateInfo *ci,
                                                                const VkAllocationCallbacks *alloc, VkPipeline *out) {
  PsoHookSlot *s = pso_slot_of(d);
  if (!s || !s->real) {
    g_pso_unrouted++;
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  VkPipelineCache use = c;
  if (use == VK_NULL_HANDLE && s->cache) use = s->cache->handle();
  const uint64_t t0 = platform::now_ns();
  VkResult r = s->real(d, use, n, ci, alloc, out);
  if (s->cache) s->cache->note_created(r == VK_SUCCESS ? n : 0, platform::now_ns() - t0);
  return r;
}

} // namespace detail

// Yonlendirilemeyen cagri sayisi (0 olmali; >0 = kanca yuvasi yetmedi).
inline uint32_t pso_unrouted_calls() { return detail::g_pso_unrouted; }

// vk_api_load_device() HER cihaz acilisinda tabloyu bastan doldurur (Tuzaklar
// 8an), yani kanca da her seferinde yeniden takilmali — ve tablodaki gercek
// yordam O ANDA acilan cihaza aittir, o yuzden yuvaya onunla birlikte yazilir.
inline void pso_install_hook(VkApi &api, VkDevice dev, PsoCache *cache) {
  PFN_vkCreateGraphicsPipelines real = api.vkCreateGraphicsPipelines;
  if (!real || real == &detail::pso_create_graphics_thunk) return; // tablo bu cihaz icin yuklenmemis
  detail::PsoHookSlot *slot = nullptr;
  for (detail::PsoHookSlot &s : detail::g_pso_slots)
    if (s.dev == dev || s.dev == VK_NULL_HANDLE) { slot = &s; break; }
  if (!slot) return; // 4'ten fazla es zamanli cihaz: kanca takilmaz, sayac 0 kalir
  slot->dev = dev;
  slot->cache = cache;
  slot->real = real;
  api.vkCreateGraphicsPipelines = &detail::pso_create_graphics_thunk;
}

inline void pso_remove_hook(VkApi &api, VkDevice dev) {
  PFN_vkCreateGraphicsPipelines removed = nullptr;
  bool any = false;
  for (detail::PsoHookSlot &s : detail::g_pso_slots) {
    if (s.dev == dev) {
      removed = s.real;
      s = detail::PsoHookSlot{};
    } else if (s.dev != VK_NULL_HANDLE) {
      any = true;
    }
  }
  // Baska cihaz kaldiysa tablo kancali kalir (thunk yuvaya gore dogru yordami
  // secer). Kalmadiysa tablo, son cihazin gercek yordamina geri konur.
  if (!any && removed && api.vkCreateGraphicsPipelines == &detail::pso_create_graphics_thunk)
    api.vkCreateGraphicsPipelines = removed;
}

} // namespace tulpar::engine::rhi
