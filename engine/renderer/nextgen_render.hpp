// L3 RENDERER — Iki BAGIMSIZ, gercekten implemente edilmis parca:
//   1) OcclusionCuller  : Intel MaskedOcclusionCulling sarmalayicisi (CPU,
//                         SIMD hiyerarsik derinlik tamponu).
//   2) VirtualTexturePageTable : sanal doku (megatexture) SAYFA TABLOSU --
//                         saf CPU defter tutma, LRU tahliye. GPU'ya bagimsiz,
//                         bu yuzden TAM olarak test edilebilir.
//
// **content/occlusion.hpp ile iliski (ikisi de "occlusion culling" -- hangisi
// ne zaman):** content/occlusion.hpp motorun KENDI, bagimliliksiz, ~100
// satirlik yazilim derinlik tamponudur (kucuk sahne, birkac occluder, sifir
// ucuncu-parti). Burasi Intel'in uretim-seviyesi kutuphanesini kullanir
// (AVX2/AVX512/NEON, hiyerarsik maskeleme, cok daha hizli) -- BUYUK sahnede
// ve ucuncu-parti bagimliligin kabul edilebilir oldugu masaustu/yuksek
// segment cihazda dogru secim odur. Ikisi AYNI soruyu cevaplar, farkli
// maliyet/bagimlilik dengesiyle; cagiran hangisini kullandigini SECER.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

// --- 1) CPU okluzyon kirpma (Intel MaskedOcclusionCulling) --------------
enum class OcclusionResult : uint8_t { kVisible, kOccluded, kViewCulled };

class OcclusionCuller {
 public:
  OcclusionCuller() = default;
  ~OcclusionCuller();
  OcclusionCuller(const OcclusionCuller &) = delete;
  OcclusionCuller &operator=(const OcclusionCuller &) = delete;

  // width/height: MaskedOcclusionCulling kurali -- genislik 8'in, yukseklik
  // 4'un KATI olmali (kutuphane boyle istiyor), aksi halde false doner.
  // Cozunurluk ekran cozunurlugunden DUSUK olabilir (yaygin secim: yarim
  // ya da ceyrek) -- kirpma konservatiftir, kucuk tampon YANLIS POZITIF
  // (gorunur sayma) verir, YANLIS NEGATIF (yanlislikla eleme) vermez.
  bool init(uint32_t width, uint32_t height);
  void shutdown();
  bool valid() const { return moc_ != nullptr; }

  // Kare basi: derinlik tamponunu temizler.
  void begin_frame();

  // Occluder (kapatici) ucgenleri cizer. vertices: XYZW (4 float) HOMOJEN
  // KLIP uzayinda -- yani cagiran model*viewproj'u ZATEN uygulamis olmali
  // (ya da model_to_clip veriyorsa XYZ yeterlidir ve kutuphane carpar).
  // model_to_clip: nullptr degilse 4x4 sutun-major matris, vertices o zaman
  // XYZ(W=1 varsayilir) model uzayindadir.
  void render_occluders(const float *vertices, const uint32_t *indices, uint32_t triangle_count,
                        const float *model_to_clip = nullptr);

  // Dikdortgen testi: NDC'de (x/w, y/w) [-1,1] kutusu + w_min (en yakin
  // derinlik). AABB'sini ekrana projekte etmis cagiran icin en ucuz test.
  OcclusionResult test_rect(float xmin, float ymin, float xmax, float ymax, float w_min) const;

  // Dunya-uzayi AABB'yi verilen viewproj ile projekte edip test_rect cagirir
  // (8 koseyi donusturur, NDC kutusunu ve en yakin w'yi cikarir). Kamera
  // KUTUNUN ICINDEYSE (koselerden biri yakin duzlemin arkasinda) guvenli
  // taraf secilir: kVisible.
  OcclusionResult test_aabb(const Mat4 &viewproj, Aabb box) const;

 private:
  void *moc_ = nullptr; // MaskedOcclusionCulling* (basligi burada SIZDIRMAYIZ)
  uint32_t width_ = 0, height_ = 0;
};

// --- 2) Sanal doku sayfa tablosu ---------------------------------------
// Sanal doku: devasa (ör. 128k x 128k) mantiksal dokunun yalniz GOREN
// sayfalari fiziksel bir onbellekte tutulur. Bu sinif SAF DEFTER TUTMADIR:
// hangi sanal sayfa hangi fiziksel yuvada, hangisi en uzun suredir
// kullanilmiyor (LRU), yeni istek hangi yuvayi tahliye etmeli. GPU kopyasi/
// doku yuklemesi CAGIRANIN isi -- bu sayede tamamen test edilebilir.
constexpr uint16_t kVtInvalidSlot = 0xFFFF;

struct VtPageRequest {
  uint32_t page_id = 0;            // istenen sanal sayfa
  uint16_t slot = kVtInvalidSlot;  // yerlestirildigi fiziksel yuva
  uint32_t evicted_page = UINT32_MAX; // bu yuvadan ATILAN sayfa (yoksa UINT32_MAX)
  bool newly_loaded = false;       // true: cagiran bu sayfayi GERCEKTEN yuklemeli
};

class VirtualTexturePageTable {
 public:
  // page_count: sanal sayfa sayisi (mantiksal). slot_count: fiziksel
  // onbellek yuvasi sayisi (page_count'tan cok daha kucuk olmali).
  // Cagiranin ayirdigi iki dizi: page_to_slot[page_count], slot_to_page[slot_count]
  // + slot_last_used[slot_count] (LRU zaman damgalari).
  bool init(uint32_t page_count, uint16_t slot_count, uint16_t *page_to_slot, uint32_t *slot_to_page,
            uint64_t *slot_last_used);

  // Bir sayfayi ISTER. Zaten resident ise yalniz LRU zaman damgasi tazelenir
  // (newly_loaded=false). Degilse: bos yuva varsa oraya, yoksa EN ESKI
  // kullanilan yuva tahliye edilir (evicted_page doldurulur).
  VtPageRequest request(uint32_t page_id);

  // Sayfanin fiziksel yuvasi (resident degilse kVtInvalidSlot). GPU'daki
  // yonlendirme (indirection) dokusunu doldurmak icin.
  uint16_t slot_of(uint32_t page_id) const;
  uint32_t resident_count() const { return resident_count_; }
  uint64_t tick() const { return tick_; }

 private:
  uint16_t *page_to_slot_ = nullptr;
  uint32_t *slot_to_page_ = nullptr;
  uint64_t *slot_last_used_ = nullptr;
  uint32_t page_count_ = 0;
  uint16_t slot_count_ = 0;
  uint32_t resident_count_ = 0;
  uint64_t tick_ = 0; // monoton artan "kullanim zamani" (LRU icin)
};

} // namespace tulpar::engine::renderer
