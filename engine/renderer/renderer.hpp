// L3 RENDERER — Faz 3 ilk dilim: forward (Lambert) cizici. Depth prepass ->
// renk subpass (RHI render pass'i: swapchain ya da offscreen). Mesh'ler
// yukleme aninda (staging), cizim listesi kare basina sabit kapasite (A2),
// kare UBO ucuslu kare basina, push sabitiyle model+renk. Bindless/kume
// isiklandirma/CSM sonraki adimlar (PLAN.md Faz 3).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/cluster.hpp"
#include "renderer/cull.hpp"
#include "renderer/graph.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::renderer {

struct Vertex {
  Vec3 pos;
  Vec3 nrm;
  Vec2 uv;
};

// Iskeletli vertex: 4 eklem (u8) + 4 agirlik (unorm16) = 40 bayt.
// --- GPU vertex yerlesimi (PAKETLENMIS) ------------------------------------
// Yazar tarafi (Vertex / SkinnedVertex) duz float kalir; yukleme aninda GPU
// bicimine paketlenir: normal OKTAHEDRAL snorm16x2 (4 bayt, ~0.1 derece hata),
// UV yarim hassasiyet (4 bayt). 32 -> 20 bayt (%37 daha az vertex bant
// genisligi). Plan Faz 3 "packed format bit butcesi"; mobilde vertex okuma
// bant genisligi kare butcesinin gorunur bir kalemi.
struct GpuVertex {
  float pos[3];
  int16_t nrm[2];  // oktahedral, SNORM16
  uint16_t uv[2];  // yarim (SFLOAT16)
};
struct GpuSkinnedVertex {
  float pos[3];
  int16_t nrm[2];
  uint16_t uv[2];
  uint8_t joints[4];
  uint16_t weights[4];
};
static_assert(sizeof(GpuVertex) == 20, "paketlenmis vertex 20 bayt");
static_assert(sizeof(GpuSkinnedVertex) == 32, "paketlenmis iskeletli vertex 32 bayt");

struct SkinnedVertex {
  Vec3 pos;
  Vec3 nrm;
  Vec2 uv;
  uint8_t joints[4];
  uint16_t weights[4];
};
struct MeshHandle {
  uint32_t id = 0xFFFFFFFFu;
  bool valid() const { return id != 0xFFFFFFFFu; }
};
struct TextureHandle {
  uint32_t id = 0xFFFFFFFFu;
  bool valid() const { return id != 0xFFFFFFFFu; }
};
// Malzeme = albedo dokusu + renk carpani. Klasik descriptor set (set 1) — bindless
// YOK: Dusuk sinif cihaz descriptorIndexing vermiyor (PLAN.md REV-3), bu yol
// her cihazda calisan yedek. Malzeme degisiminde set baglanir; cizim listesi
// malzemeye gore siralanmaz (sonraki adim).
struct MaterialHandle {
  uint32_t id = 0xFFFFFFFFu;
  bool valid() const { return id != 0xFFFFFFFFu; }
};

// --- PBR (metallic-roughness, glTF 2.0) -------------------------------------
// Golgeleme modeli MALZEME basina secilir. create_material(doku, renk) eski
// Lambert modelini korur — mevcut butun kapilar (srgb gidis-donus, golge,
// icerik) BIT BIT ayni goruntuyu alir; PbrParams alan asiri yukleme
// Cook-Torrance'a gecer. Bindless yok: parametreler malzeme basina 32 baytlik
// bir UBO'dan (set 1, binding 1) okunur.
//
// Varsayilanlar bilerek Lambert'e yakinsar: metallic 0 + roughness 1'de GGX
// D = 1/PI'ye duser ve geriye yalniz dielektrigin %4'luk Fresnel'i kalir.
struct PbrParams {
  float metallic = 0.0f;    // 0 dielektrik, 1 metal (dagilimli terim yok)
  float roughness = 1.0f;   // ALGISAL puruzluluk (glTF); shader'da a = roughness^2
  float reflectance = 0.5f; // dielektrik F0 = 0.16 * reflectance^2 (0.5 -> %4)
  Vec3 emissive{0, 0, 0};   // YAZAR sRGB rengi; dogrusala cevrilip eklenir
  float emissive_strength = 1.0f; // isima gucu carpani
};

// --- PBR DOKULARI (set 1, binding 2/3/4) ------------------------------------
// glTF 2.0'in doku basina degisen malzeme kanallari. Hepsi OPSIYONEL: gecersiz
// tutamac = o kanal icin CARPAN yolu (bugunku goruntu). Shader'da her biri
// MALZEME BASINA TEKDUZE bir dal — yani dokusu olmayan malzeme ne ornekleme
// ne de ALU odemez (dal draw icinde sabit, dalga ici ayrisma yok). Bu, bindless
// olmayan klasik sette bir sampler daha eklemenin TBDR'daki tek makul sekli:
// ucret yalnizca dokuyu GERCEKTEN kullanan malzemede.
//
// KANAL SOZLESMESI (glTF 2.0 spec; cgltf SAF AYRISTIRICI, kanal esleme
// tasimaz — o yuzden burada yazili ve `content_gltf_orm_channel_mapping`
// kapisi olcuyor):
//   metallicRoughness dokusu: R = (kullanilmaz / occlusion), G = ROUGHNESS, B = METALLIC
//   occlusionTexture:         R = occlusion (cogu varlikta AYNI goruntu)
// Doku degerleri CARPANLARLA CARPILIR (spec: "multiplied with the texture values").
struct PbrTextures {
  TextureHandle orm{};      // metallicRoughness (G puruzluluk, B metal, R occlusion)
  TextureHandle normal{};   // teget uzayi normal haritasi (DOGRUSAL yuklenmeli)
  TextureHandle emissive{}; // isima dokusu (sRGB yuklenmeli), emissive carpaniyla carpilir
  float normal_scale = 1.0f;       // glTF normalTexture.scale (xy'yi olcekler)
  float occlusion_strength = 0.0f; // glTF occlusionTexture.strength; 0 = occlusion YOK
};

// GGX normal dagiliminin normalizasyonu. Unnormalized = KONTROL kipi
// (UiSortMode::BlendFirst ile ayni ruh): a^2 payini BILEREK dusurur, boylece
// enerji kapisinin gercekten olcup olcmedigi gosterilebilir. Urunde hep Ggx.
enum class NdfMode : uint8_t { Ggx = 0, Unnormalized = 1 };

// --- FAZ 5 ZAMANSAL (temporal) — hepsi VARSAYILAN KAPALI -------------------
// Yukseltici (upscaler) ARAYUZU. FSR SDK'si / saf GLSL entegrasyonu; referans yollar
// burada, gercek FSR entegrasyonu ayni arayuze bir kind ekleyerek girer
// (birlestirme gecisi tek dokunma noktasi).
enum class UpscalerKind : uint8_t {
  None = 0,     // nokta ornekleme (texel merkezine kenetlenir) — referans
  Bilinear = 1, // donanim dogrusal suzme (ucuz yol)
  Sharpen = 2,  // dogrusal + unsharp maske (CAS benzeri)
  FSR = 3,      // AMD FidelityFX Super Resolution 1.0 (Saf GLSL)
};

struct TemporalConfig {
  // Alt-piksel ornekleme kaydirmasi (Halton 2,3). Projeksiyona SON adimda,
  // clip uzayinda eklenir: on-dondurmeli (Android) projeksiyonda da dogru.
  bool jitter = false;
  uint32_t jitter_phases = 8; // dizi uzunlugu (kare basina doner)
  // Ekran uzayi hareket vektoru + reactive maske hedefi (kendi gecisi).
  bool motion_vectors = false;
  uint32_t width = 0, height = 0; // 0 = post_width/post_height
  // Dinamik cozunurluk: sahne ic HDR hedefinin KULLANILAN alt-dikdortgeni.
  // Hedefler EN BUYUK olcude bir kez kurulur; olcek degisimi yalniz viewport'u
  // degistirir (kare icinde ayirma YOK).
  float render_scale = 1.0f; // 0.5 .. 1.0
  UpscalerKind upscaler = UpscalerKind::None;
  float sharpness = 0.25f; // Sharpen icin unsharp agirligi
};

struct RendererConfig {
  // Hedef sRGB bicimli mi (swapchain/offscreen SRGB): donanim kodlar. false:
  // UNORM hedef, shader kodlar (yedek yol; ayni goruntu, biraz daha pahali).
  bool srgb_target = true;
  uint32_t max_meshes = 64;
  uint32_t max_textures = 64;
  uint32_t max_materials = 512;
  uint32_t max_draws = 8192;
  uint32_t frames_in_flight = 2;
  // Yonlu isik golge haritasi (tek kademe). 0 = golge yok. Mobil: D16 tercih
  // edilir (bant genisligi), cihaz vermezse D32'ye duser.
  // Kademeli golge (CSM): shadow_size ARTIK KADEME BASINA tile olcusudur; atlas
  // (size * cascades) x size tek goruntudur. cascades = 1 eski tek-kademe yolu.
  // Varsayilan 3 x 1024 = 6 MB (D16): bugunku tek 2048 haritadan (8 MB) DAHA AZ
  // bellek, yakin alanda ~3 kat daha ince texel (yakin kademe hacmin 1/9'u).
  uint32_t shadow_size = 1024;
  uint32_t shadow_cascades = 3;
  float shadow_bias = 0.0008f;         // derinlik uzayinda kucuk sabit egilim
  float shadow_normal_offset = 0.06f;  // DUNYA birimi: normal boyunca kaydirma (akne)
  uint32_t ui_max_vertices = 32768;
  // FAZ 4 UI: retained blok onbellegi (dortgen = 40 bayt). 0 = retained kapali.
  uint32_t ui_max_blocks = 8;
  uint32_t ui_block_max_quads = 512;
  // Opak dortgenleri one alma + harmanlamasiz boru hatti (GUVENLI yukseltme).
  bool ui_sort_opaque_first = true;
  // Ekranin bu kadarini kaplayan HARMANLI dortgen uyari sayar (TBDR'da butun
  // tile'lari doldurur — PLAN Faz 4 "tam ekran seffaf katman yasak").
  float ui_fullscreen_blend_ratio = 0.9f;
  // SDF ornekleme keskinligi (ozellestirme sabiti, boru hatti kurulumunda):
  // gecis genisligi = fwidth(mesafe) * bu carpan. Buyugu yumusak, kucugu keskin.
  float ui_sdf_sharpness = 1.0f;
  // SDF boru hattini KURULUMDA yarat (Faz 6: "runtime'da pipeline kurulumu
  // yok"). Eskiden ilk SDF dortgeninde, KAYIT yolunun icinde kuruluyordu ve o
  // karede 0.49-0.52 ms takilma uretiyordu (RTX 5080'de olculdu).
  // false = KONTROL kipi: eski TEMBEL davranis. Yalniz "ilk karede kurulan
  // pipeline = 0" kapisinin pozitif kontrolu icindir (kare ici kurulumun
  // gercekten sayilabildigini gostermek); urunde asla kapatilmaz.
  bool ui_prewarm_sdf = true;
  uint32_t max_skin_matrices = 4096;    // kare basina eklem matrisi (SSBO, set 0 binding 4)    // 2B arayuz: kare basina (ucgen listesi, 6/dortgen)

  // --- Derlenmis render graph + son islem (bloom) — VARSAYILAN KAPALI ------
  // false: bugunku yol BIT BIT ayni; sahne dogrudan cagiranin gecisine cizilir.
  // true: record_shadow() golgeden SONRA sahneyi ic HDR hedefine cizer ve bloom
  // zincirini kosturur; record() cagiranin gecisinde yalniz TAM EKRAN
  // birlestirmeyi cizer. Cagiran sozlesmesi (record_shadow -> [cagiranin
  // gecisi] -> record -> ui_record) AYNEN gecerli kalir — bu yuzden boyle.
  bool post = false;
  // Ic HDR hedefinin olcusu. post acikken ZORUNLU (0 = post kapanir, sebep
  // PostInfo::disabled_reason'da). Kare icinde degismez (yeniden boyutlandirma
  // init gerektirir; pencere yolu bunu swapchain yeniden yaratmasiyla yapar).
  // Ic hedefler TEK kopyadir (ucuslu kare basina degil): gecislerin disa
  // bagimliliklari onceki karenin ORNEKLEMESINI bu karenin yazmasindan once
  // beklettigi icin dogrudur, ama ucuslu kareler bu noktada serilesir. Kare
  // basina kopya, olculmus bir kazanc olursa eklenir (bugun bellek daha degerli).
  uint32_t post_width = 0, post_height = 0;
  uint32_t bloom_mips = 4;      // 2..kMaxBloomMips (zincir derinligi = yayilim genisligi)
  float bloom_threshold = 1.0f; // DOGRUSAL parlaklik esigi: altindaki yayilmaz (secici bloom)
  float bloom_soft_knee = 0.5f; // esikte sert kesme bantlasma yapar
  float bloom_intensity = 0.6f; // 0 = bloom yok (kapilarin kontrolu)
  float bloom_radius = 1.0f;    // yukari ornekleme cadir suzgecinin yaricap carpani
  float exposure = 1.0f;
  bool tonemap = false;         // Reinhard c/(1+c); kapali: yalniz kirpma
  Vec3 post_clear{0, 0, 0};     // HDR hedefinin temizleme rengi (DOGRUSAL)
  TemporalConfig temporal{};    // Faz 5: jitter / hareket vektoru / dinamik cozunurluk

  // --- FAZ 9: GPU gorunurluk kumeleme + dolayli cizim — VARSAYILAN KAPALI ---
  // false: bugunku yol BIT BIT ayni (ayni SPIR-V, ayni komut akisi). true:
  // cizim kayitlari SSBO'ya yazilir, compute gecisi frustum testini yapar ve
  // VkDrawIndexedIndirectCommand dizisini doldurur, CPU kume basina TEK
  // vkCmdDrawIndexedIndirect verir. Cihaz/kurulum vermezse CPU yoluna DUSER ve
  // sebebi CullInfo::disabled_reason'da yazar (sessiz kapanma yok).
  bool gpu_cull = false;
  // Golge kademeleri de ayni cull gecisinden (isik frustum'u) yararlansin.
  // gpu_cull kapaliyken anlamsiz.
  bool gpu_cull_shadow = true;
  // Kume (mesh+malzeme kosusu) kapasitesi. Asilirsa kalan cizimler CPU yoluna
  // duser ve CullInfo::cpu_draws'da sayilir — sessizce kaybolmaz.
  uint32_t max_cull_batches = 512;

  // --- PBR ----------------------------------------------------------------
  // false (varsayilan): create_material(doku, renk) LAMBERT malzeme uretir ve
  // bugunku goruntu bit bit korunur. true: ayni cagri PBR malzeme uretir
  // (metallic 0, roughness 1) — icerik tarafina dokunmadan butun sahneyi
  // Cook-Torrance'a cevirmek icin (A/B olcumu bunu kullanir).
  bool pbr_default = false;
  // Enerji kapisinin KONTROLU. Urun yolunda asla degistirilmez.
  NdfMode pbr_ndf = NdfMode::Ggx;

  // --- Stokastik tile isiklandirma (PLAN EK A.1) — VARSAYILAN KAPALI -------
  // 0: bugunku yol BIT BIT ayni (shader'daki kuyruk dali ozellestirme sabitiyle
  // tamamen elenir, u_stoch hic okunmaz). > 0: kume basina en cok bu kadar isik
  // degerlendirilir, geri kalani telafi agirligiyla ornekten kestirilir.
  // Gerekce ve zamansal kararlilik mekanizmasi: renderer/cluster.hpp.
  uint32_t stochastic_lights = 0;
  uint32_t stochastic_keep = 2;    // her kare tutulan en onemli isik (zamansal capa)
  uint32_t stochastic_phases = 128; // donme cozunurlugu (buyuk = kararli, yavas kapsama)
  // KONTROL kipi (UiSortMode::BlendFirst / NdfMode::Unnormalized ile ayni ruh):
  // ornekle ama TELAFI ETME. Urun yolunda asla degistirilmez.
  bool stochastic_compensate = true;
};

// Son islem yolunun DISARI VERDIGI durum: acik mi, degilse NEDEN, hangi bicim
// secildi, gecis tablosu ne. Sessiz kapanma yok — kapi bunu okur.
struct PostInfo {
  bool enabled = false;
  const char *disabled_reason = "";
  VkFormat hdr_format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0;
  uint32_t bloom_mips = 0, bloom_width = 0, bloom_height = 0;
  uint64_t target_bytes = 0; // ic hedeflerin toplam GPU baytI (olcum)
  uint32_t pass_count = 0;
  const char *pass_name[kMaxGraphPasses] = {};
};

// Zamansal yolun DISARI VERDIGI durum: ne acik, degilse NEDEN, hangi bicim,
// bu karenin jitter'i ne. Sessiz kapanma yok — kapi bunu okur.
struct TemporalInfo {
  bool jitter = false;
  float jitter_x = 0, jitter_y = 0; // BU karenin alt-piksel kaydirmasi (piksel)
  uint32_t jitter_index = 0;        // Halton dizisindeki faz
  bool motion = false;
  const char *motion_disabled_reason = "";
  VkFormat motion_format = VK_FORMAT_UNDEFINED;
  uint32_t motion_width = 0, motion_height = 0;
  uint64_t motion_bytes = 0;          // MV + transient derinlik GPU baytI
  uint32_t motion_skipped_skinned = 0; // son karede MV'siz kalan iskeletli cizim
  float render_scale = 1.0f;
  uint32_t scaled_width = 0, scaled_height = 0; // sahnenin GERCEK piksel olcusu
  UpscalerKind upscaler = UpscalerKind::None;
  const char *scale_disabled_reason = ""; // dinamik cozunurluk neden yok
};

// 2B arayuz koseleri: piksel uzayi, atlas uv, RGBA8. Immediate-mode: her kare
// yeniden uretilir, sabit kapasite (A2), ayirma yok.
struct UiVertex {
  float x, y, u, v;
  uint32_t rgba;
};

// --- FAZ 4 UI: siralama kipi ------------------------------------------------
// Kuyruk immediate-mode kalir; SIRA kayit aninda kurulur.
//   Source      : kuyruk sirasi (eski yol; komut akisi tek batch, hep harmanli)
//   OpaqueFirst : GUVENLI opak yukseltme — opak bir dortgen yalniz kendinden
//                 ONCEKI ve akista KALAN hicbir dortgenle ORTUSMUYORSA one
//                 alinir. Boyle bir dortgen harmanlamasiz cizilir (ucuz: tile
//                 belleginden okuma yok) ve piksel sonucu AYNIDIR.
//   BlendFirst  : KONTROL kipi — bilerek yanlis sira (harmanlananlar once).
//                 Kapinin "siralama gercekten onemli" iddiasini olcer.
enum class UiSortMode : uint8_t { Source = 0, OpaqueFirst = 1, BlendFirst = 2 };

// UI overdraw: OLCULMUS degerler (tahmini alan toplami DEGIL). Olcum yolu:
// ui_record_overdraw() ayni dortgen akisini "her fragment +1" boru hattiyla
// UNORM hedefe cizer, geri okunan R bayti o pikseldeki FRAGMENT SAYISIDIR.
struct UiOverdraw {
  uint32_t shaded = 0;    // rasterlanan fragment toplami
  uint32_t covered = 0;   // en az bir kez dokunulan piksel
  uint32_t saturated = 0; // 255'e doyan piksel (>0 ise sayim EKSIK)
  float ratio = 0;        // shaded / covered  (1.0 = ustuste binme yok)
  float screen = 0;       // shaded / (w*h)    (ekran bant genisligi payi)
};

struct UiStats {
  uint32_t vertices = 0, dropped = 0; // MEVCUT alanlar (cagiran sozlesmesi)
  uint32_t quads = 0;                 // kuyruktaki dortgen (uretilen + yeniden kullanilan)
  uint32_t generated = 0;             // cagiranin BU karede gercekten urettigi
  uint32_t reused = 0;                // retained blok onbelleginden gelen
  uint32_t opaque = 0, blended = 0;   // son karede cizilen dortgenin dagilimi
  uint32_t batches = 0;               // vkCmdDraw sayisi (tek batch hedefi)
  uint32_t hoisted = 0;               // opak gruba GUVENLE tasinan
  uint32_t blocked_hoists = 0;        // ortusme yuzunden tasinamayan opak
  uint32_t atlas_groups = 0;          // kuyruktaki ayrik atlas
  bool atlas_sorted = false;          // atlasa gore kararli siralama uygulandi mi
  uint32_t blocks_rebuilt = 0, blocks_reused = 0, blocks_overflow = 0;
  uint32_t sdf_quads = 0;             // SDF ornekleme yoluyla cizilen dortgen
  const char *sdf_reason = "";        // SDF istendi ama kosmadiysa NEDEN
  uint32_t fullscreen_blended = 0;    // TBDR yasagi: tam ekrani kaplayan HARMANLI katman
  float cpu_gen_ms = 0;               // ui_begin..ui_end (dortgen uretimi)
  bool gen_timed = false;             // ui_end cagrilmadiysa false (cpu_gen_ms anlamsiz)
  float cpu_build_ms = 0;             // ui_record icinde: sirala + vertex yaz + komut
  float gpu_ms = 0;                   // UI cizimlerinin GPU suresi (zaman damgasi)
  bool gpu_timing = false;
  const char *gpu_timing_reason = "";
  UiOverdraw overdraw{}; // ui_set_overdraw ile beslenir (olculdu, tahmin degil)
};

struct ShadowInfo {
  bool enabled = false;
  uint32_t size = 0;      // kademe basina tile
  uint32_t cascades = 1;  // atlas genisligi = size * cascades
  VkFormat format = VK_FORMAT_UNDEFINED;
  bool linear_filter = false; // donanim PCF (yoksa NEAREST)
  const char *disabled_reason = "";
};

struct RendererStats {
  uint32_t draws = 0;      // son kare
  uint32_t dropped = 0;    // kapasite asimi (sayilir, sessiz degil)
  uint32_t meshes = 0;
  uint32_t textures = 0;
  uint32_t materials = 0;
  uint32_t material_binds = 0; // son kare (siralama yoksa cizim sayisina yaklasir)
  ClusterStats clusters;       // son kare isik atamasi
};

class Renderer {
public:
  bool init(rhi::Device &dev, Arena &arena, VkRenderPass rp, const RendererConfig &cfg);
  void shutdown();

  // Yukleme: staging ile device-local. Kare icinde CAGRILMAZ.
  MeshHandle create_mesh(const Vertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices);
  MeshHandle create_skinned_mesh(const SkinnedVertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices);
  // Indeks araligi seyrek (Mali kurali, CPU'da olculur) mesh sayisi; kapi 0 bekler.
  uint32_t sparse_mesh_count() const { return sparse_mesh_count_; }
  // RGBA8, mip zinciri blit ile uretilir (yukleme aninda). Mobil asil yol ASTC (Faz 6).
  // srgb: renk verisi (albedo) -> R8G8B8A8_SRGB, ornekleme dogrusal dondurur.
  // false: veri (kaplama/alfa/normal) -> UNORM, oldugu gibi.
  TextureHandle create_texture(const uint8_t *rgba, uint32_t w, uint32_t h, bool mipmaps = true, bool srgb = true);
  // Hazir mip zinciri (sikistirilmis ASTC bloklari ya da RGBA8): seviye basina
  // bayt dizisi, blit yok. fmt: VK_FORMAT_ASTC_*_SRGB_BLOCK / R8G8B8A8_SRGB...
  TextureHandle create_texture_levels(VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, const uint8_t *const *data,
                                      const uint32_t *sizes);
  MaterialHandle create_material(TextureHandle albedo, Vec3 color = {1, 1, 1});
  // PBR malzeme (Cook-Torrance). Yukleme aninda cagrilir.
  MaterialHandle create_material(TextureHandle albedo, Vec3 color, const PbrParams &pbr);
  // PBR malzeme + doku basina kanallar (ORM / normal / isima). Yukleme aninda.
  MaterialHandle create_material(TextureHandle albedo, Vec3 color, const PbrParams &pbr, const PbrTextures &tex);
  // Dokulari yerinde degistirir (descriptor yazimi + UBO maskesi). KARE DISINDA.
  bool set_material_textures(MaterialHandle m, const PbrTextures &tex);
  PbrTextures material_textures(MaterialHandle m) const;
  // OLCUM: set 1'in baglama sayisi ve malzeme UBO'sunun cihaz hizasina
  // yuvarlanmis adim boyu. Butce kapisi bunlari basar (once/sonra karsilastirma).
  static constexpr uint32_t kMaterialBindings = 5; // albedo, UBO, ORM, normal, isima
  uint32_t material_ubo_stride() const { return mat_ubo_stride_; }
  static constexpr uint32_t kMaterialUboBytes = 64;
  // Parametreleri yerinde gunceller (malzeme UBO'suna yazar). KARE DISINDA
  // cagrilir: tampon host-visible ve ucuslu kare basina KOPYALANMAZ.
  bool set_material_pbr(MaterialHandle m, const PbrParams &pbr);
  PbrParams material_pbr(MaterialHandle m) const;
  bool material_is_pbr(MaterialHandle m) const;
  TextureHandle default_texture() const { return default_texture_; } // 1x1 beyaz
  MaterialHandle default_material() const { return default_material_; }

  void set_camera(const Mat4 &view, const Mat4 &proj);
  void set_light(Vec3 dir, Vec3 ambient, float diffuse_scale);
  Vec3 ambient() const { return ambient_; } // son set_light degeri (GI kapisi okur)
  // Kume gridi framebuffer uzayinda: hedefin olcusu (swapchain goruntusu / offscreen).
  void set_render_size(uint32_t width, uint32_t height);
  // Nokta isiklar (kare basina en cok 32; kumelenmis, CPU atamali). begin_frame'de atanir.
  static constexpr uint32_t kMaxPointLights = 32;
  // Golge kademesi sayisi (GLSL dizi boyutuyla AYNI olmali: mesh.frag/shadow.vert).
  static constexpr uint32_t kMaxCascades = 3;
  void clear_point_lights() { point_light_count_ = 0; }
  bool add_point_light(const PointLight &l);
  uint32_t point_light_count() const { return point_light_count_; }
  // Stokastik yol gercekten acik mi (kurulum basarili ve shader ozellestirildi).
  bool stochastic_lighting() const { return stoch_enabled_; }
  // Golge kutusu: isik yonu (isiga DOGRU), sahne merkezi, yaricap, derinlik.
  void set_shadow_volume(Vec3 center, float radius, float depth);
  // Yakin kademelerin merkezi (kamera hedefi / oyuncu). Kurulmazsa golge
  // hacminin merkezi kullanilir. Kademe kutulari texel'e KENETLENIR (titreme yok).
  void set_shadow_focus(Vec3 p) { shadow_focus_ = p; has_focus_ = true; }
  // i. kademenin dunya yaricapi (0 = en yakin); olcum/kapi icin.
  float cascade_radius(uint32_t i) const { return i < kMaxCascades ? cascade_radius_[i] : 0.0f; }
  ShadowInfo shadow() const { return shadow_info_; }
  // A/B ve dusuk segment icin: hedef durur, gecis ve ornekleme kapanir.
  void set_shadows_enabled(bool on) { shadow_info_.enabled = on && cfg_.shadow_size > 0; }
  // Egilim ayari (cihaza gore): derinlik uzayinda sabit + DUNYA biriminde normal
  // kaydirmasi. Kaydirma buyurse golge kacar (peter-panning), kucukse akne.
  void set_shadow_bias(float depth_bias, float normal_offset) {
    cfg_.shadow_bias = depth_bias;
    cfg_.shadow_normal_offset = normal_offset;
  }
  // Yonlu isik icin isik-uzayi viewproj (ortografik). Golge kutusu sahneyi kapsamali.
  static Mat4 directional_light_matrix(Vec3 dir, Vec3 center, float radius, float depth);

  // Kare: begin (UBO yaz) -> draw*N -> record(cb): subpass0 depth, next, subpass1 renk.
  void begin_frame(uint32_t frame_index);
  void draw(MeshHandle mesh, const Mat4 &model, Vec3 color); // varsayilan malzeme
  // prev_model: ONCEKI karenin model matrisi (hareket vektoru icin). nullptr =
  // duran nesne (prev = model, MV 0). Cagiran sozlesmesi BOZULMAZ: varsayilan
  // argumanlar, bugunku cagrilar aynen derlenir. Nesne KIMLIGI tutulmaz —
  // cizim listesi kare basina sifirlanir, onceki matrisi bilen cagirandir
  // (sahne/ECS zaten onu tutuyor); renderer'da kalici id = ikinci bir omur
  // yonetimi demekti.
  // reactive: 0..1 "hareket vektoruna GUVENME" maskesi (saydam, parcacik,
  // golge-olmayan kabuk). MV hedefinin alfa kanalina yazilir.
  void draw(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color = {1, 1, 1},
            const Mat4 *prev_model = nullptr, float reactive = 0.0f);
  // Iskeletli cizim: joints[n] = model uzayi eklem matrisi * ters bind (skin
  // matrisi). Kare SSBO'suna kopyalanir; kapasite asimi sayilir (dropped).
  void draw_skinned(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color, const Mat4 *joints,
                    uint32_t n, const Mat4 *prev_model = nullptr, float reactive = 0.0f);
  uint32_t skin_matrices_used() const { return skin_count_; }
  // Golge gecisi: KENDI render pass'ini acar/kapatir. Ana render pass BASLAMADAN
  // once cagrilir (cizim listesi dolu olmali).
  void record_shadow(VkCommandBuffer cb);
  void record(VkCommandBuffer cb);

  // --- Son islem (derlenmis graph) ----------------------------------------
  PostInfo post() const { return post_; }
  // Kare icinde degistirilebilir (boru hatti yeniden kurulmaz; push sabiti).
  void set_bloom(float threshold, float intensity) {
    cfg_.bloom_threshold = threshold;
    cfg_.bloom_intensity = intensity;
  }
  void set_bloom_shape(float soft_knee, float radius) {
    cfg_.bloom_soft_knee = soft_knee;
    cfg_.bloom_radius = radius;
  }
  void set_exposure(float e) { cfg_.exposure = e; }

  // --- Faz 5: zamansal (jitter / MV / dinamik cozunurluk / upscaler) -------
  TemporalInfo temporal() const { return temporal_; }
  // Dinamik cozunurluk (0.5..1.0). Hedefler EN BUYUK olcude kuruldugu icin
  // burada AYIRMA YOK: yalniz sahne gecisinin viewport/renderArea'si ve
  // birlestirmenin uv carpani degisir. SOZLESME: begin_frame'den ONCE cagir —
  // jitter'in piksel bolen'i o karenin olceginden gelir. post KAPALIYKEN
  // yoksayilir (yukseltme ic hedef ister), sebep TemporalInfo'da.
  void set_render_scale(float s);
  float render_scale() const { return scale_; }
  // Yukseltici (upscale) SECIMI — birlestirme gecisinde uygulanir.
  // GELISTIRILEBILIR: AMD FSR 1.0 (Saf Kod) entegrasyonu compose.frag uzerinden yapildi.
  // Harici SDK (ARM ASR vb.) ihtiyaci kaldirilip, dis bagimlilik 0'a indirgenmistir.
  void set_upscaler(UpscalerKind k, float sharpness) {
    cfg_.temporal.upscaler = k;
    cfg_.temporal.sharpness = sharpness;
    temporal_.upscaler = k;
  }
  void set_jitter(bool on) { cfg_.temporal.jitter = on; }
  // Jitter'siz (kamera) ve jitter'li projeksiyon — kapilar bunu karsilastirir.
  Mat4 jittered_proj() const { return jittered_proj_; }
  // Radikal ters (van der Corput) tabani `base`; Halton dizisinin i. terimi.
  // i = 0 -> 0; dizi 1'den baslatilir (0 kaymasi jitter'i etkisiz birakirdi).
  static float halton(uint32_t i, uint32_t base);
  // MV hedefini CPU'ya okur (OLCUM yolu; kare icinde cagrilmaz). dst piksel
  // basina 4 float: x/y piksel kaymasi, z ayrilmis, w reactive maske.
  bool read_motion(float *dst, uint32_t max_pixels);
  // Tablo (post kapaliyken de dolu): gecis sayisi ve adlari.
  uint32_t graph_pass_count() const { return graph_n_; }
  const char *graph_pass_name(uint32_t i) const { return i < graph_n_ ? graph_[i].name : ""; }

  // --- Faz 9: GPU cull + dolayli cizim ------------------------------------
  // GPU sayaclarini okur: KARE TAMAMLANDIKTAN SONRA (fence/queue idle) cagir,
  // yoksa counts_valid true ama sayilar onceki karenindir. Sayilar gecerliyse
  // stats().draws da GPU'nun gercekten cizdigi ornek sayisina guncellenir
  // (kayit aninda CPU bunu BILEMEZ; GPU cull'un tanimi bu).
  CullInfo cull();
  // Kayit aninda bilinen kisim (aday/kume/CPU'ya dusen); GPU sayaci okumaz.
  CullInfo cull_recorded() const { return cull_; }

  RendererStats stats() const { return stats_; }
  // Olcum: GPU'daki toplam vertex baytI ve vertex basina bayt (paketleme kazanci).
  uint64_t vertex_bytes() const { return vertex_bytes_; }
  static constexpr uint32_t gpu_vertex_bytes() { return (uint32_t)sizeof(GpuVertex); }
  static constexpr uint32_t author_vertex_bytes() { return (uint32_t)sizeof(Vertex); }
  // Normal kodek (GLSL oct_decode ile AYNI sozlesme; kapi ikisinin ustuste
  // dustugunu piksel uzerinden olcer).
  static void encode_normal(Vec3 n, int16_t out[2]);
  static Vec3 decode_normal(const int16_t in[2]);

  // --- 2B arayuz (HUD, editor). record() SONRA, ayni subpass'te ui_record(). ---
  // screen: MANTIKSAL (gorunen) olcu; rotation: on-dondurme acisi (Swapchain::rotation_radians).
  void ui_begin(float screen_w, float screen_h, float rotation_radians = 0.0f); // begin_frame sonrasi
  void ui_set_atlas(MaterialHandle atlas);                    // glif atlasi (texel 0,0 beyaz)
  void ui_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t rgba);
  void ui_rect(float x, float y, float w, float h, uint32_t rgba); // duz kutu (beyaz texel)
  void ui_record(VkCommandBuffer cb);
  UiStats ui_stats() const { return ui_stats_; }

  // --- FAZ 4 UI eklentileri (mevcut imzalar DEGISMEDI) --------------------
  // Cagiran kaplamanin OPAK oldugunu biliyorsa: dortgen harmanlamasiz cizilebilir.
  void ui_quad_opaque(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t rgba);
  // Dortgen uretimi bitti. Zorunlu DEGIL; cagrilirsa cpu_gen_ms olculur.
  void ui_end();
  void ui_set_sort_mode(UiSortMode m) { ui_sort_ = m; }
  // SDF metin ORNEKLEME yolu (VARSAYILAN KAPALI). Bundan sonraki dortgenler,
  // atlasin alfasini ISARETLI MESAFE ALANI sayan boru hattiyla cizilir: kenar
  // ekran uzayi turevinden kesilir, yani buyutunce bulaniklasmaz. Atlas URETIMI
  // (content) bu sinifin isi degil — bayrak, SDF atlas gelene kadar kapali durur.
  // Boru hatti TEMBEL yaratilir; kurulamazsa harmanli yola DUSER ve sebebi
  // UiStats::sdf_reason'a yazilir (sessiz kapanma yok).
  void ui_set_sdf(bool on) { ui_sdf_ = on; }
  bool ui_sdf() const { return ui_sdf_; }
  UiSortMode ui_sort_mode() const { return ui_sort_; }
  // Tam ekrani kaplayan HARMANLI katman gorulduyse sebebi (bos = temiz).
  const char *ui_warning() const { return ui_warning_; }

  // --- Retained yerlesim: degismeyen blok yeniden HESAPLANMAZ -------------
  // Kullanim:
  //   if (r.ui_block_begin(kHud, hash)) { ...dortgenleri uret... }
  //   r.ui_block_end();
  // Donus true = blok KIRLI (icerigi uret). false = onceki karenin dortgenleri
  // onbellekten kuyruga kopyalandi, ICERIDEKI KOD KOSMAZ (yerlesim hesabi yok).
  // hash cagiranin icerik ozeti: ne degisirse ona bagli (metin, sayi, olcu).
  // ui_begin'de ekran olcusu/dondurmesi degisirse butun bloklar gecersizlenir.
  bool ui_block_begin(uint32_t id, uint64_t content_hash);
  void ui_block_end();

  // --- Overdraw olcumu (GERCEK sayim) ------------------------------------
  // AYRI bir karede cagrilir: ui_record yerine bu kaydedilir ve hedef UNORM
  // olmalidir (blend toplami tamsayi kalsin). Sonra geri okunan pikseller
  // ui_overdraw_measure'a verilir.
  void ui_record_overdraw(VkCommandBuffer cb);
  static UiOverdraw ui_overdraw_measure(const uint8_t *rgba, uint32_t w, uint32_t h);
  void ui_set_overdraw(const UiOverdraw &o) { ui_stats_.overdraw = o; }

  // --- UI GPU suresi ------------------------------------------------------
  // Zaman damgasi havuzunu sifirlar. RENDER PASS DISINDA cagrilmali; record_shadow
  // bunu kendisi yapar, yani normal cagiran sozlesmesinde ekstra is yok.
  void ui_timing_reset(VkCommandBuffer cb);
  // Kare TAMAMLANDIKTAN sonra: zaman damgalarini okur ve ui_stats_'i tazeler.
  const UiStats &ui_fetch_stats();
  // Yazarin verdigi renkler (malzeme, cizim, UI) sRGB algisaldir; aydinlatma
  // DOGRUSAL uzayda (Filament PBR tarifi). Bu donusum CPU'da malzeme/cizim
  // rengine, UI'da shader'da uygulanir; dokular SRGB bicimiyle donanimda.
  static float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
  }
  static Vec3 srgb_to_linear(Vec3 c) { return {srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z)}; }
  static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
  }

  // Yerlesik mesh'ler (cagiranin dizilerine yazar): kup [-0.5,0.5]^3, duzlem 1x1 (y=0).
  static uint32_t cube(Vertex *v, uint32_t *idx);                         // 24 v, 36 idx (uv yuz basina 0..1)
  static uint32_t plane(Vertex *v, uint32_t *idx, float uv_repeat = 1.0f); // 4 v, 6 idx
  static uint32_t sphere(Vertex *v, uint32_t *idx, uint32_t seg_h = 32, uint32_t seg_v = 16);
  static uint32_t capsule(Vertex *v, uint32_t *idx, float radius = 0.5f, float half_height = 0.5f, uint32_t seg_h = 32, uint32_t seg_v = 16);
  static uint32_t cylinder(Vertex *v, uint32_t *idx, float radius = 0.5f, float half_height = 1.0f, uint32_t seg_h = 32);
  static uint32_t cone(Vertex *v, uint32_t *idx, float radius = 0.5f, float height = 2.0f, uint32_t seg_h = 32);
  static uint32_t quad(Vertex *v, uint32_t *idx); // 4 v, 6 idx
  static uint32_t torus(Vertex *v, uint32_t *idx, float r_main = 0.5f, float r_tube = 0.2f, uint32_t seg_main = 32, uint32_t seg_tube = 16);

private:
  struct Mesh {
    VkBuffer vbuf = VK_NULL_HANDLE, ibuf = VK_NULL_HANDLE;
    uint32_t index_count = 0;
    bool skinned = false;
    // Yerel (model uzayi) sinir kuresi: yukleme aninda vertex'lerden olculur.
    // GPU cull bunu model matrisiyle donusturur.
    Vec3 center{0, 0, 0};
    float radius = 0.0f;
  };
  struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0, mips = 0;
  };
  struct Material {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t texture = 0;
    Vec3 color{1, 1, 1};
    PbrParams pbr{};
    PbrTextures tex{};
    bool is_pbr = false;
  };
  // Malzeme basina GPU blogu (std140): set 1, binding 1 — mesh.frag MatBlock.
  struct MaterialUbo {
    float pbr[4];      // x metallic, y algisal puruzluluk, z yansitirlik, w model (0/1)
    float emissive[4]; // rgb DOGRUSAL isima
    // Doku maskeleri: shader'in MALZEME BASINA TEKDUZE dallari. 32 -> 64 bayt
    // buyume GPU'da BEDAVA: adim boyu zaten cihazin minUniformBufferOffsetAlignment'i
    // (masaustu 64, Mali 256) — olculen deger material_ubo_stride().
    float tex[4];  // x ORM var mi, y normal var mi, z isima dokusu var mi, w normal olcegi
    float tex2[4]; // x occlusion gucu (ORM.R; 0 = occlusion yok), y/z/w bos
  };
  static_assert(sizeof(MaterialUbo) == 64, "std140: MatBlock 64 bayt");
  static constexpr uint32_t kNoSkin = 0xFFFFFFFFu;
  static constexpr uint32_t kNoBatch = 0xFFFFFFFFu;
  struct Draw {
    uint32_t mesh;
    uint32_t material;
    Mat4 model;
    Vec3 color;
    uint32_t skin_offset; // kNoSkin = statik
    Mat4 prev_model;      // hareket vektoru icin (verilmezse = model)
    float reactive;       // 0..1 MV guvenilmezlik maskesi
    uint32_t batch;       // GPU cull kumesi; kNoBatch = CPU yolunda cizilir
  };
  // ui.vert'in push blogu: GLSL Push { vec2 screen; vec2 rot; float encode; }.
  // Eskiden burada ciplak bir `const float push[5]` vardi (renderer.cpp);
  // adsiz oldugu icin yerlesim denetimi onu KAPSAYAMIYORDU — GLSL blogu
  // degisirse hicbir sey uyarmazdi. Adlandirilmis struct denetlenebilir.
  struct UiPush { // = 20 bayt
    float screen[2]; // ui_w_, ui_h_
    float rot[2];    // cos(ui_rot_), sin(ui_rot_)
    float encode;    // hedef UNORM ise 1 (shader kodlar), SRGB ise 0
  };
  static_assert(sizeof(UiPush) == 20, "ui.vert push blogu 20 bayt");
  struct Push { // GLSL Push { mat4 model; vec4 color; uvec4 skin; } = 96 bayt
    Mat4 model;
    float color[4];
    uint32_t skin[4];
  };
  static_assert(sizeof(Push) == 96, "push sabiti 96 bayt");
  struct FrameUbo {
    Mat4 viewproj;
    Mat4 view;
    Mat4 light_viewproj[kMaxCascades]; // kademe basina isik matrisi (atlas tile'i)
    float light_dir[4];
    float ambient[4];
    float shadow_params[4];  // x: 1/tile, y: egilim, z: acik mi, w: normal kaydirma
    float cluster_params[4]; // x: dilim olcegi, y: dilim sapmasi, z: tile genisligi(px), w: tile yuksekligi(px)
    uint32_t cluster_grid[4]; // x, y, z, isik sayisi
    float cascade_params[4]; // x: kademe sayisi, y: 1/kademe, z: atlas texel x, w: atlas texel y
  };
  // The Forge SRT ilkesi (CPU-GPU tek kaynak tablosu): GLSL std140 blogu ile bu
  // struct'in ofsetleri DERLEME zamaninda eslesir; kayma = derleme hatasi.
  // mesh.frag/mesh.vert "Frame" blogu: 3 x mat4 (192) + 4 x vec4 (64) + uvec4 (16) = 272.
  static_assert(offsetof(FrameUbo, view) == 64, "std140: view");
  static_assert(offsetof(FrameUbo, light_viewproj) == 128, "std140: light_viewproj[]");
  static_assert(offsetof(FrameUbo, light_dir) == 128 + 64 * kMaxCascades, "std140: light_dir");
  static_assert(offsetof(FrameUbo, ambient) == offsetof(FrameUbo, light_dir) + 16, "std140: ambient");
  static_assert(offsetof(FrameUbo, shadow_params) == offsetof(FrameUbo, ambient) + 16, "std140: shadow_params");
  static_assert(offsetof(FrameUbo, cluster_params) == offsetof(FrameUbo, shadow_params) + 16, "std140: cluster_params");
  static_assert(offsetof(FrameUbo, cluster_grid) == offsetof(FrameUbo, cluster_params) + 16, "std140: cluster_grid");
  static_assert(offsetof(FrameUbo, cascade_params) == offsetof(FrameUbo, cluster_grid) + 16, "std140: cascade_params");
  static_assert(sizeof(FrameUbo) == 128 + 64 * kMaxCascades + 96, "std140: Frame blogu boyutu (6 x vec4 kuyruk)");
  struct GpuPointLight { // GLSL PointLight { vec4 pos_radius; vec4 color_intensity; } = 32 bayt
    float pos_radius[4];
    float color_intensity[4];
  };
  bool make_buffer(VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags mem, VkBuffer *buf,
                   rhi::MemoryAlloc *out);
  bool upload(VkBuffer dst, const void *data, VkDeviceSize size, VkBufferUsageFlags usage_dst);
  // Vertex'leri STAGING BELLEGINE DOGRUDAN paketler: ara dizi ayrilmaz.
  bool upload_packed(VkBuffer dst, const Vertex *v, const SkinnedVertex *sv, uint32_t n);
  bool make_pipelines(VkRenderPass rp);
  // main_vs/shadow_vs: bu kumede kullanilacak vertex shader'lari (statik,
  // iskeletli ya da DOLAYLI yolun kendi shader'i).
  bool make_pipeline_set(VkRenderPass rp, bool skinned, VkShaderModule main_vs, VkShaderModule shadow_vs_mod,
                         VkPipeline *depth, VkPipeline *color, VkPipeline *shadow);
  bool make_shadow(); // render pass + goruntu + sampler + boru hatti
  static Mat4 cascade_matrix(Vec3 dir, Vec3 center, float radius, float depth, uint32_t tile);
  MaterialHandle create_material_impl(TextureHandle albedo, Vec3 color, const PbrParams &pbr, bool is_pbr,
                                     const PbrTextures &tex);
  void write_material_set(uint32_t id); // set 1'in 5 baglamasini yazar
  void write_material_ubo(uint32_t id);
  bool make_material_layout();
  bool make_ui(VkRenderPass rp, Arena &arena);
  enum class UiPipeKind : uint8_t { Blend = 0, Opaque = 1, Overdraw = 2, Sdf = 3 };
  VkPipeline make_ui_pipeline(VkRenderPass rp, UiPipeKind kind);
  bool ui_ensure_overdraw_pipeline();
  bool ui_ensure_sdf_pipeline();
  void ui_push_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t rgba,
                    bool opaque);
  uint32_t ui_build_order();                       // ui_order_'i doldurur, dortgen sayisi doner
  void ui_group_by_atlas(uint32_t from, uint32_t to); // GUVENLIYSE atlasa gore kararli sirala
  void ui_emit(VkCommandBuffer cb, bool overdraw);
  // --- Son islem (graph.hpp tablosundan) ----------------------------------
  bool make_post(VkRenderPass target_rp); // false: post kapanir, sebep PostInfo'da
  bool make_post_image(VkFormat fmt, VkImageUsageFlags usage, uint32_t w, uint32_t h, uint32_t mips, bool lazily,
                       VkImage *img, rhi::MemoryAlloc *mem);
  bool make_post_pipe(VkShaderModule fs, VkRenderPass rp, uint32_t subpass, VkPipeline *out);
  VkImageView graph_view(uint8_t res, uint8_t level) const;
  void graph_size(uint8_t res, uint8_t level, uint32_t *w, uint32_t *h) const;
  // --- Faz 5: hareket vektoru gecisi --------------------------------------
  bool make_motion();                        // false: MV kapanir, sebep TemporalInfo'da
  void record_motion_pass(VkCommandBuffer cb);
  void update_scaled_size();                 // render_scale -> scaled_w_/h_
  // --- Faz 9: GPU cull ------------------------------------------------------
  bool make_cull();                          // false: cull kapanir, sebep CullInfo'da
  uint32_t build_batches();                  // CPU: kume kosulari + SSBO yazimi
  void record_cull(VkCommandBuffer cb);      // compute gecisi + bariyer
  void record_scene_indirect(VkCommandBuffer cb);
  void record_shadow_pass(VkCommandBuffer cb); // bugunku golge gecisi govdesi
  void record_scene(VkCommandBuffer cb);     // bugunku govde (cagiranin ya da HDR gecisinde)
  void record_hdr_scene(VkCommandBuffer cb); // ic HDR gecisi: begin + record_scene + end
  void record_post_chain(VkCommandBuffer cb); // parlak + indirgeme + yukari ornekleme
  void record_compose(VkCommandBuffer cb);    // cagiranin gecisinde tam ekran birlestirme

  rhi::Device *dev_ = nullptr;
  RendererConfig cfg_{};
  Mesh *meshes_ = nullptr;
  uint32_t mesh_count_ = 0;
  Texture *textures_ = nullptr;
  uint32_t texture_count_ = 0;
  Material *materials_ = nullptr;
  uint32_t material_count_ = 0;
  VkSampler tex_sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout mat_layout_ = VK_NULL_HANDLE;
  // Butun malzemelerin UBO'su TEK tampon; her malzeme kendi ofsetine bakar
  // (stride cihazin minUniformBufferOffsetAlignment'ina yuvarlanir). Malzeme
  // basina ayri vkAllocateMemory YOK — tahsis sayaci kapisi bunu olcuyor.
  VkBuffer mat_ubo_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc mat_ubo_mem_{};
  uint32_t mat_ubo_stride_ = 0;
  VkDescriptorPool mat_pool_ = VK_NULL_HANDLE;
  TextureHandle default_texture_{};
  MaterialHandle default_material_{};
  Draw *draws_ = nullptr;
  uint32_t draw_count_ = 0;
  RendererStats stats_{};
  Mat4 view_{}, proj_{};
  Vec3 light_dir_{0.4f, 1.0f, 0.3f}, ambient_{0.12f, 0.13f, 0.16f};
  float diffuse_scale_ = 0.9f;
  Vec3 shadow_center_{0, 0, 0};
  float shadow_radius_ = 16.0f, shadow_depth_ = 60.0f;
  uint64_t vertex_bytes_ = 0;
  Mat4 light_vp_[kMaxCascades]{};
  float cascade_radius_[kMaxCascades]{};
  Vec3 shadow_focus_{0, 0, 0};
  bool has_focus_ = false;
  ShadowInfo shadow_info_{};
  VkShaderModule vs_ = VK_NULL_HANDLE, fs_ = VK_NULL_HANDLE, shadow_vs_ = VK_NULL_HANDLE;
  VkShaderModule skin_vs_ = VK_NULL_HANDLE, skin_shadow_vs_ = VK_NULL_HANDLE;
  VkPipeline pipe_skin_depth_ = VK_NULL_HANDLE, pipe_skin_color_ = VK_NULL_HANDLE, pipe_skin_shadow_ = VK_NULL_HANDLE;
  uint32_t skin_count_ = 0;
  VkRenderPass shadow_rp_ = VK_NULL_HANDLE;
  VkImage shadow_img_ = VK_NULL_HANDLE;
  VkImageView shadow_view_ = VK_NULL_HANDLE;
  VkFramebuffer shadow_fb_ = VK_NULL_HANDLE;
  VkSampler shadow_sampler_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc shadow_mem_{};
  VkPipeline pipe_shadow_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipe_depth_ = VK_NULL_HANDLE, pipe_color_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  static constexpr uint32_t kMaxFrames = 3;
  VkBuffer ubo_[kMaxFrames] = {};
  rhi::MemoryAlloc ubo_mem_[kMaxFrames] = {};
  VkBuffer lights_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc lights_mem_[kMaxFrames] = {};
  VkBuffer cluster_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc cluster_mem_[kMaxFrames] = {};
  VkDescriptorSet sets_[kMaxFrames] = {};
  VkBuffer skin_buf_[kMaxFrames] = {};        // eklem matrisleri (kare basina, host-visible)
  rhi::MemoryAlloc skin_mem_[kMaxFrames] = {};
  // UI
  VkShaderModule ui_vs_ = VK_NULL_HANDLE, ui_fs_ = VK_NULL_HANDLE, ui_over_fs_ = VK_NULL_HANDLE,
                 ui_sdf_fs_ = VK_NULL_HANDLE;
  VkPipeline pipe_ui_ = VK_NULL_HANDLE;        // harmanli (SRC_ALPHA / 1-SRC_ALPHA)
  VkPipeline pipe_ui_opaque_ = VK_NULL_HANDLE; // harmanlamasiz (opak dortgen)
  VkPipeline pipe_ui_over_ = VK_NULL_HANDLE;   // overdraw sayimi (TEMBEL yaratilir)
  VkPipeline pipe_ui_sdf_ = VK_NULL_HANDLE;    // SDF metin ornekleme (TEMBEL yaratilir)
  VkRenderPass ui_rp_ = VK_NULL_HANDLE;        // tembel boru hatti icin saklanir
  VkBuffer ui_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc ui_mem_[kMaxFrames] = {};
  uint32_t ui_count_ = 0;
  float ui_w_ = 1, ui_h_ = 1, ui_rot_ = 0;
  MaterialHandle ui_atlas_{};
  UiStats ui_stats_{};
  // --- FAZ 4: CPU dortgen kuyrugu (siralama/batch/retained bunu ister) ----
  // Eskiden ui_quad dogrudan mapped vertex tamponuna yaziyordu; SIRA ancak
  // butun kuyruk bilindikten sonra kurulabilecegi icin dortgenler once bu
  // diziye girer, vertex'ler ui_record'da SIRALI yazilir. Diziler KURULUMDA
  // arenadan alinir — kare icinde ayirma yok (A2).
  struct UiQuad {
    float x, y, w, h;
    float u0, v0, u1, v1;
    uint32_t rgba;
    uint32_t atlas;  // materials_ indeksi
    uint8_t opaque;  // 1: harmanlamasiz cizilebilir
    uint8_t hoist;   // 1: opak gruba tasindi (ui_build_order isaretler)
    uint8_t sdf;     // 1: SDF ornekleme boru hatti
    uint8_t pad;
  };
  struct UiBlock {
    uint32_t id = 0xFFFFFFFFu;
    uint64_t hash = 0;
    uint32_t count = 0;
    bool valid = false, overflow = false;
  };
  static constexpr uint32_t kUiMaxAtlasGroups = 8;
  UiQuad *ui_quads_ = nullptr;
  uint32_t *ui_order_ = nullptr, *ui_scratch_ = nullptr;
  uint32_t ui_quad_cap_ = 0, ui_quad_n_ = 0;
  UiBlock *ui_blocks_ = nullptr;
  UiQuad *ui_block_quads_ = nullptr;
  uint32_t ui_block_cap_ = 0, ui_block_quad_cap_ = 0;
  int32_t ui_block_active_ = -1;
  UiSortMode ui_sort_ = UiSortMode::OpaqueFirst;
  bool ui_sdf_ = false;       // gecerli durum: sonraki dortgenler SDF mi
  bool ui_sdf_failed_ = false; // boru hatti kurulamadi (bir kez denenir)
  const char *ui_warning_ = "";
  uint64_t ui_gen_t0_ = 0;
  VkQueryPool ui_query_ = VK_NULL_HANDLE;
  bool ui_query_reset_[kMaxFrames] = {}, ui_query_written_[kMaxFrames] = {};
  uint32_t ui_query_frame_ = 0;
  ClusterGrid grid_{};
  uint32_t *cluster_masks_ = nullptr; // Arena, grid_.count()
  uint32_t *cluster_stoch_ = nullptr;  // Arena, 2 * grid_.count(): (kuyruk maskesi, kuyruk toplami)
  VkBuffer stoch_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc stoch_mem_[kMaxFrames] = {};
  bool stoch_enabled_ = false;
  uint32_t stoch_frame_ = 0; // MONOTON kare sayaci (begin_frame'de artar): desen bununla doner
  PointLight point_lights_[kMaxPointLights];
  uint32_t point_light_count_ = 0;
  uint32_t render_w_ = 1, render_h_ = 1;
  uint32_t frame_ = 0;
  uint32_t sparse_mesh_count_ = 0;
  // --- Son islem: hedefler, gecisler, tablo -------------------------------
  // Push sabiti (butun son islem gecisleri paylasir, yalniz FRAGMENT):
  // texel.xy = 1/kaynak0 olcusu, texel.zw = 1/kaynak1 olcusu.
  struct PostPush {
    float texel[4];
    float p[4];
    // q.x: dinamik cozunurluk uv carpani, q.y: keskinlik, q.z: UpscalerKind,
    // q.w: ayrilmis. Zincirin tamami AYNI blogu iter (tek duzen, tek havuz);
    // kullanmayan shader'lar blogun yalniz onunu bildirir.
    float q[4];
  };
  static_assert(sizeof(PostPush) == 48, "son islem push sabiti 48 bayt");
  PostInfo post_{};
  GraphPass graph_[kMaxGraphPasses]{};
  uint32_t graph_n_ = 0;
  VkFormat hdr_fmt_ = VK_FORMAT_UNDEFINED;
  uint32_t post_w_ = 0, post_h_ = 0;
  VkImage hdr_img_ = VK_NULL_HANDLE, hdr_depth_img_ = VK_NULL_HANDLE;
  VkImageView hdr_view_ = VK_NULL_HANDLE, hdr_depth_view_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc hdr_mem_{}, hdr_depth_mem_{};
  VkRenderPass hdr_rp_ = VK_NULL_HANDLE, bloom_rp_ = VK_NULL_HANDLE;
  VkFramebuffer hdr_fb_ = VK_NULL_HANDLE;
  // 0 = indirgeme zinciri, 1 = yukari zincir. Zincir basina TEK goruntu (mip'li):
  // mip basina ayri goruntu olsaydi her biri ayri vkAllocateMemory olurdu.
  VkImage bloom_img_[2] = {};
  rhi::MemoryAlloc bloom_mem_[2] = {};
  VkImageView bloom_view_[2][kMaxBloomMips] = {};
  VkFramebuffer bloom_fb_[2][kMaxBloomMips] = {};
  uint32_t bloom_w_[kMaxBloomMips] = {}, bloom_h_[kMaxBloomMips] = {};
  uint32_t bloom_mips_ = 0;
  VkSampler post_sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout post_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout post_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool post_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet post_sets_[kMaxGraphPasses] = {};
  VkShaderModule post_vs_ = VK_NULL_HANDLE, bright_fs_ = VK_NULL_HANDLE, down_fs_ = VK_NULL_HANDLE,
                 up_fs_ = VK_NULL_HANDLE, compose_fs_ = VK_NULL_HANDLE;
  VkPipeline pipe_bright_ = VK_NULL_HANDLE, pipe_down_ = VK_NULL_HANDLE, pipe_up_ = VK_NULL_HANDLE,
             pipe_compose_ = VK_NULL_HANDLE;

  // --- Faz 5: zamansal ------------------------------------------------------
  // Hareket gecisinin kare bloku (std140): gl_Position JITTER'LI matristen
  // (renk pikselleriyle ayni yere otursun), hareket vektoru JITTER'SIZ iki
  // matristen (kaydirma farki MV'ye SIZMASIN — upscaler jitter'i kendi bilir).
  struct MotionUbo {
    Mat4 viewproj_jit;
    Mat4 viewproj;
    Mat4 prev_viewproj;
    float params[4]; // xy: hedefin piksel olcusu, zw: ayrilmis
  };
  static_assert(sizeof(MotionUbo) == 208, "std140: hareket kare blogu");
  // Cizim basina ornek: push sabiti 128 baytlik garantiye sigmadigi icin
  // (model + onceki model = 128, reactive'e yer kalmiyor) SSBO'ya tasindi;
  // push yalniz indeksi tasir.
  struct MotionInst {
    Mat4 model;
    Mat4 prev_model;
    float misc[4]; // x: reactive, yzw: ayrilmis
  };
  static_assert(sizeof(MotionInst) == 144, "std430: hareket ornegi");
  TemporalInfo temporal_{};
  float scale_ = 1.0f;
  uint32_t scaled_w_ = 1, scaled_h_ = 1;
  uint32_t temporal_frame_ = 0; // jitter fazi (begin_frame basina artar)
  Mat4 jittered_proj_{};
  Mat4 prev_viewproj_{};
  bool has_prev_vp_ = false;
  bool motion_recorded_ = false; // MV hedefi en az bir kez yazildi (okuma icin)
  VkFormat motion_fmt_ = VK_FORMAT_UNDEFINED;
  uint32_t motion_w_ = 0, motion_h_ = 0;
  VkImage motion_img_ = VK_NULL_HANDLE, motion_depth_img_ = VK_NULL_HANDLE;
  VkImageView motion_view_ = VK_NULL_HANDLE, motion_depth_view_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc motion_mem_alloc_{}, motion_depth_mem_{};
  VkRenderPass motion_rp_ = VK_NULL_HANDLE;
  VkFramebuffer motion_fb_ = VK_NULL_HANDLE;
  VkShaderModule motion_vs_ = VK_NULL_HANDLE, motion_fs_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout motion_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout motion_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool motion_pool_ = VK_NULL_HANDLE;
  VkPipeline pipe_motion_ = VK_NULL_HANDLE;
  VkDescriptorSet motion_sets_[kMaxFrames] = {};
  VkBuffer motion_ubo_[kMaxFrames] = {}, motion_inst_[kMaxFrames] = {};
  rhi::MemoryAlloc motion_ubo_mem_[kMaxFrames] = {}, motion_inst_mem_[kMaxFrames] = {};
  VkBuffer motion_read_ = VK_NULL_HANDLE; // olcum yolunun geri okuma tamponu
  rhi::MemoryAlloc motion_read_mem_{};

  // --- Faz 9: GPU cull + dolayli cizim --------------------------------------
  // VkApi (rhi/vk_api.hpp) bu uc giris noktasini listelemiyor; RHI'nin Vulkan
  // yuzeyini genisletmek yerine cizici KENDI isaretcilerini vkGetDeviceProcAddr
  // ile alir (yukleyici zaten dlopen'li). Biri bile gelmezse cull kapanir ve
  // sebebi CullInfo'ya yazilir.
  using PfnCreateComputePipelines = VkResult(VKAPI_PTR *)(VkDevice, VkPipelineCache, uint32_t,
                                                          const VkComputePipelineCreateInfo *,
                                                          const VkAllocationCallbacks *, VkPipeline *);
  using PfnCmdDispatch = void(VKAPI_PTR *)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
  using PfnCmdDrawIndexedIndirect = void(VKAPI_PTR *)(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
  PfnCreateComputePipelines pfn_create_compute_ = nullptr;
  PfnCmdDispatch pfn_dispatch_ = nullptr;
  PfnCmdDrawIndexedIndirect pfn_draw_indirect_ = nullptr;
  CullInfo cull_{};
  bool cull_ready_ = false;   // bu karede cull gecisi KAYDEDILDI (yoksa CPU yolu)
  bool cull_recorded_ = false; // en az bir kare kaydedildi (sayaclar anlamli)
  uint32_t cull_batches_n_ = 0;
  uint32_t cull_frusta_n_ = 0;
  uint32_t max_batches_ = 0;
  CullBatchCpu *batches_ = nullptr; // Arena, max_batches_
  VkShaderModule cull_cs_ = VK_NULL_HANDLE, cull_vs_ = VK_NULL_HANDLE, cull_shadow_vs_ = VK_NULL_HANDLE;
  VkPipeline pipe_cull_depth_ = VK_NULL_HANDLE, pipe_cull_color_ = VK_NULL_HANDLE, pipe_cull_shadow_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout cull_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout cull_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool cull_pool_ = VK_NULL_HANDLE;
  VkPipeline pipe_cull_ = VK_NULL_HANDLE;
  VkDescriptorSet cull_sets_[kMaxFrames] = {};
  VkBuffer cull_draw_buf_[kMaxFrames] = {};   // GpuDrawItem * max_draws (host)
  VkBuffer cull_batch_buf_[kMaxFrames] = {};  // GpuBatch * max_batches_ (host)
  VkBuffer cull_frustum_buf_[kMaxFrames] = {};// vec4 * 6 * kMaxCullFrusta (host)
  VkBuffer cull_cmd_buf_[kMaxFrames] = {};    // GpuIndirectCmd * frusta * max_batches_ (host: geri okunur)
  VkBuffer cull_vis_buf_[kMaxFrames] = {};    // uint * frusta * max_draws (device-local)
  rhi::MemoryAlloc cull_draw_mem_[kMaxFrames] = {}, cull_batch_mem_[kMaxFrames] = {},
                   cull_frustum_mem_[kMaxFrames] = {}, cull_cmd_mem_[kMaxFrames] = {}, cull_vis_mem_[kMaxFrames] = {};
  VkQueryPool cull_query_ = VK_NULL_HANDLE; // 2 damga * kMaxFrames
  bool cull_query_written_[kMaxFrames] = {};
  uint32_t cull_read_frame_ = 0; // sayaclari okunacak kare yuvasi
  // Push sabiti (compute): x kume sayisi, y frustum sayisi, z kume kapasitesi,
  // w cizim kapasitesi.
  struct CullPush {
    uint32_t counts[4];
  };
};

} // namespace tulpar::engine::renderer
