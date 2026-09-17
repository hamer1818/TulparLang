// L6 CONTENT — GI SONDA BAKE'i (PLAN Faz 6: "scene compiler: resident set,
// peak memory, PSO uretimi, GI probe / navmesh bake"). Oyun tanimi (CIHAZ-
// MATRISI §1) "PBR yok, basit BRDF + bake GI" diyor: statik isik DERLEME
// aninda CPU'da isin izlemeyle cozulur, runtime yalniz OKUR.
//
// GOSTERIM: 6 yonlu AMBIENT CUBE (+X,-X,+Y,-Y,+Z,-Z), sonda basina 6 x RGB.
// SH-L1 (4 katsayi x RGB = 48 bayt) yerine bu (72 bayt) secildi, NEDEN:
//   1. Halka (ringing) yok. Guclu tek yonlu bir gunes SH-L1'e sigmaz; lob ters
//      tarafta NEGATIF olur, kirpinca enerji kaybolur ve "gunese donuk yuz vs
//      ters yuz" olcumu gostergenin kendi hatasini olcmeye baslar. Ambient
//      cube'da her yuz BAGIMSIZ, negatif olamayan bir integraldir: olculen sayi
//      dogrudan o yondeki isimadir.
//   2. Cozum 3 carpma-topla (n^2 agirlikli, birim bolunmesi): Mali/Adreno'da
//      SH-L1'in 4 nokta carpimindan ucuz, ve kirpma/koruma kodu gerekmez.
//   3. Interpolasyon yuz yuz dogrusal kalir — SH'de oldugu gibi lob sekli
//      bozulmaz, gecerli/gecersiz sonda agirliklandirmasi guvenlidir.
// Bedeli: sonda basina 24 bayt daha ve yumusak alanlarda hafif kare etkisi
// (n^2 tabani L2'ye gore genis). Sahne cesitliligi degil, mobil oyun sahnesi
// hedeflendigi icin kabul edildi.
//
// BIRIM: yuz degeri E(n)/pi — yani "albedo 1 olan Lambert yuzeyin o yonde
// verecegi renk". mesh.frag'in isik terimiyle AYNI sozlesme:
//     renk = albedo * (ambient + nl*golge*sun_diffuse + nokta_isiklar)
// Bake bu parantezin TAMAMINI sonda alanina koyar (gi_flags bit0/bit1 hangi
// terimlerin iceride oldugunu soyler) — cift sayim yapmamak icin runtime
// bayraklara bakmali.
//
// Uygulama content/scene_blob.cpp icinde (scene_compile ile ayni TU; motorun
// CMake kaynak listesi elle tutuluyor, yeni .cpp eklenmiyor).
#pragma once
#include <cmath>
#include <cstdint>

#include "content/model.hpp"
#include "content/scene.hpp"
#include "content/scene_blob.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kGiFaces = 6;
constexpr uint32_t kGiMaxProbes = kGiBlobMaxProbes; // blob/bake ust siniri
constexpr uint32_t kGiMaxOccluders = kSceneMaxEntities;
constexpr uint32_t kGiMaxLights = 64;

// gi_flags bitleri (SceneBlobHeader::gi_flags).
enum SceneGiFlagBits : uint32_t {
  kGiDirectSun = 1u << 0,   // dogrudan gunes sonda alanina DAHIL (runtime ayrica eklemesin)
  kGiPointLights = 1u << 1, // nokta isiklarin dogrudan terimi dahil
  kGiModelTris = 1u << 2,   // model ucgenleri tikayici olarak kullanildi
  kGiBounce = 1u << 3,      // en az bir sicrama (indirect) hesaplandi
};

// Yuz sirasi: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
inline Vec3 gi_face_dir(uint32_t f) {
  switch (f) {
  case 0: return {1, 0, 0};
  case 1: return {-1, 0, 0};
  case 2: return {0, 1, 0};
  case 3: return {0, -1, 0};
  case 4: return {0, 0, 1};
  default: return {0, 0, -1};
  }
}
// Ambient cube cozumu: n^2 agirlikli birim bolunmesi (sum n^2 = 1), yani
// n bir eksene tam esitse o yuzun degeri BIT BIT geri gelir.
inline Vec3 gi_eval_cube(const float face[kGiFaces][3], Vec3 n) {
  const float ax = n.x * n.x, ay = n.y * n.y, az = n.z * n.z;
  const float *fx = face[n.x >= 0 ? 0 : 1];
  const float *fy = face[n.y >= 0 ? 2 : 3];
  const float *fz = face[n.z >= 0 ? 4 : 5];
  return {ax * fx[0] + ay * fy[0] + az * fz[0], ax * fx[1] + ay * fy[1] + az * fz[1], ax * fx[2] + ay * fy[2] + az * fz[2]};
}
// Yazar rengi (sRGB) -> dogrusal albedo. renderer::Renderer::srgb_to_linear ile
// AYNI egri; burada kopya, cunku bake L3'e baglanmadan da calisabilmeli.
inline float gi_srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline Vec3 gi_srgb_to_linear(Vec3 c) { return {gi_srgb_to_linear(c.x), gi_srgb_to_linear(c.y), gi_srgb_to_linear(c.z)}; }

// --- Bake girdisi: sahne geometrisi -----------------------------------------
// Tikayici: SABIT (dynamic=false) govdeler. Kutu OBB (yazar donusu + olcek
// uygulanmis), kure analitik. Dinamik govdeler bake'e GIRMEZ: yerleri oyun
// icinde degisir, bake edilmis golgeleri yalan olur.
struct GiOccluder {
  Vec3 center{0, 0, 0};
  float radius = 0; // kure
  Vec3 half{0, 0, 0};
  uint32_t kind = 0; // 0 kutu (OBB), 1 kure
  Quat rot{0, 0, 0, 1};
  Vec3 albedo{0.5f, 0.5f, 0.5f}; // DOGRUSAL
  float reserved = 0;
};
// Model ucgeni, DUNYA uzayinda (varlik matrisi uygulanmis).
struct GiTri {
  Vec3 a{0, 0, 0}, b{0, 0, 0}, c{0, 0, 0};
  Vec3 albedo{1, 1, 1};
};
// Nokta isik (mesh.frag'daki pencereli ters-kare sonumle ayni parametreler).
struct GiLight {
  Vec3 pos{0, 0, 0};
  float radius = 1;
  Vec3 color{1, 1, 1};
  float intensity = 1;
};
struct GiScene {
  const GiOccluder *occluders = nullptr;
  uint32_t occluder_count = 0;
  const GiTri *tris = nullptr;
  uint32_t tri_count = 0;
  const GiLight *lights = nullptr;
  uint32_t light_count = 0;
  Vec3 sun_dir{0, 1, 0}; // NORMALIZE (isiga dogru)
  Vec3 sun_color{1, 1, 1};
  float sun_diffuse = 0;
  Vec3 ambient{0, 0, 0};                        // dogrusal, her yonde esit gokyuzu isimasi
  Vec3 bounds_lo{0, 0, 0}, bounds_hi{0, 0, 0}; // izgara hacmi
};

struct SceneGiOptions {
  float spacing = 2.0f;        // izgara adimi (dunya birimi)
  float margin = 1.0f;         // sahne sinirinin disina tasma
  uint32_t max_probes = 4096;  // asilirsa adim buyutulur (belirlenimli)
  uint32_t rays = 128;         // sonda basina KURE ornegi (Fibonacci spirali)
  uint32_t bounces = 1;        // 0 = yalniz gokyuzu gorunurlugu + dogrudan (KAPI KONTROLU)
  bool direct_sun = true;      // dogrudan gunes sonda alanina girsin mi
  bool point_lights = true;    // nokta isiklarin dogrudan terimi girsin mi
  bool shadow_point_lights = true; // nokta isik icin gorunurluk isini
  float surface_albedo = 0.5f; // rengi olmayan govdenin dogrusal albedosu
  float ray_bias = 1e-3f;      // kendi kendini vurma payi
  float max_distance = 1.0e4f;
};

struct SceneGiReport {
  uint32_t probes = 0, probes_inside = 0; // toplam; kati icinde (gecersiz)
  uint32_t dim[3] = {0, 0, 0};
  float spacing = 0;
  float origin[3] = {0, 0, 0};
  uint32_t occluders = 0, triangles = 0, lights = 0;
  uint32_t bvh_nodes = 0;
  uint64_t rays = 0; // atilan TOPLAM isin (birincil + golge)
  double seconds = 0;
  uint64_t hash = 0;  // sonda tablosunun FNV-1a'si (belirlenimlilik)
  float min_luma = 0, max_luma = 0; // bilgi: en karanlik / en parlak yuz
};

// --- Sahneden girdi toplama --------------------------------------------------
// SABIT kutu/kure govdeler -> tikayici. Donus: yazilan tikayici sayisi.
uint32_t scene_gi_occluders(const SceneDesc &d, const SceneGiOptions &opt, GiOccluder *out, uint32_t max);
// Model ucgenleri (dunya uzayinda). models[asset] null ise o kaynak atlanir;
// ISKELETLI mesh'ler atlanir (poz basina degisir, bake edilemez).
uint32_t scene_gi_model_tris(const SceneDesc &d, const SceneGiOptions &opt, const Model *const *models, uint32_t asset_count, GiTri *out,
                             uint32_t max);
// Nokta isik bilesenli varliklar -> GiLight (dunya konumu).
uint32_t scene_gi_lights(const SceneDesc &d, GiLight *out, uint32_t max);
// Gunes/ortam/sinir alanlarini SceneDesc'ten doldurur (diziler cagiranin).
void scene_gi_setup(const SceneDesc &d, GiScene *g);

// --- Bake --------------------------------------------------------------------
// Sonda tablosunu `arena`dan ayirir ve x->gi_* alanlarini doldurur.
// false: arena dolu ya da izgara kurulamadi (sahne bos).
bool scene_gi_bake(Arena &arena, const GiScene &g, const SceneGiOptions &opt, SceneBlobExtras *x, SceneGiReport *rep);

// --- Runtime sorgusu ----------------------------------------------------------
// Blob'daki sonda izgarasinin okuma yuzu: BAKE YOK, ayirma yok, sadece arama.
// Gecersiz (kati icinde) sondalarin agirligi 0 — trilineer karisimda yalniz
// gecerli komsular sayilir; hicbiri gecerli degilse sahne ortami dondurulur.
class SceneGi {
public:
  bool init(const SceneBlobView &v) {
    *this = SceneGi{};
    if (!v.h) return false;
    ambient_ = {v.h->ambient[0], v.h->ambient[1], v.h->ambient[2]};
    if (v.h->gi_probe_count == 0 || !v.gi_probes) return false;
    if (v.h->gi_spacing <= 0) return false;
    probes_ = v.gi_probes;
    count_ = v.h->gi_probe_count;
    dim_[0] = v.h->gi_dim[0]; dim_[1] = v.h->gi_dim[1]; dim_[2] = v.h->gi_dim[2];
    if ((uint64_t)dim_[0] * dim_[1] * dim_[2] != count_) { probes_ = nullptr; return false; }
    origin_ = {v.h->gi_origin[0], v.h->gi_origin[1], v.h->gi_origin[2]};
    spacing_ = v.h->gi_spacing;
    flags_ = v.h->gi_flags;
    return true;
  }
  bool ok() const { return probes_ != nullptr; }
  uint32_t count() const { return count_; }
  uint32_t flags() const { return flags_; }
  const uint32_t *dim() const { return dim_; }
  float spacing() const { return spacing_; }
  Vec3 ambient() const { return ambient_; }
  const SceneBlobGiProbe &probe(uint32_t i) const { return probes_[i]; }
  uint32_t index(uint32_t x, uint32_t y, uint32_t z) const { return (z * dim_[1] + y) * dim_[0] + x; }
  Vec3 probe_pos(uint32_t i) const {
    const uint32_t x = i % dim_[0], y = (i / dim_[0]) % dim_[1], z = i / (dim_[0] * dim_[1]);
    return {origin_.x + (float)x * spacing_, origin_.y + (float)y * spacing_, origin_.z + (float)z * spacing_};
  }
  // En yakin sonda (interpolasyonsuz). -1: izgara yok.
  int32_t nearest_index(Vec3 p) const {
    if (!probes_) return -1;
    return (int32_t)index(cell(p.x - origin_.x, dim_[0]), cell(p.y - origin_.y, dim_[1]), cell(p.z - origin_.z, dim_[2]));
  }
  Vec3 sample_nearest(Vec3 p, Vec3 n) const {
    const int32_t i = nearest_index(p);
    if (i < 0 || (probes_[i].flags & 1u)) return ambient_;
    return gi_eval_cube(probes_[i].face, n);
  }
  // Trilineer + gecerlilik agirlikli okuma. n normalize olmali.
  Vec3 sample(Vec3 p, Vec3 n) const {
    Corner c[8];
    const uint32_t k = corners(p, c);
    Vec3 acc{0, 0, 0};
    float wsum = 0;
    for (uint32_t i = 0; i < k; i++) {
      acc += gi_eval_cube(probes_[c[i].index].face, n) * c[i].w;
      wsum += c[i].w;
    }
    return wsum > 0 ? acc * (1.0f / wsum) : ambient_;
  }
  // Dogrudan gunes gorunurlugu (0..1). Gecerli komsu yoksa 1 (aydinlik varsay:
  // bake edilmemis bolgede sahneyi karartmak golgeyi uydurmak olur).
  float sun_visibility(Vec3 p) const {
    Corner c[8];
    const uint32_t k = corners(p, c);
    float acc = 0, wsum = 0;
    for (uint32_t i = 0; i < k; i++) {
      acc += probes_[c[i].index].sun_vis * c[i].w;
      wsum += c[i].w;
    }
    return wsum > 0 ? acc / wsum : 1.0f;
  }

private:
  struct Corner {
    uint32_t index;
    float w;
  };
  uint32_t cell(float d, uint32_t n) const {
    const float g = d / spacing_;
    const int32_t i = (int32_t)(g + 0.5f);
    if (i < 0) return 0;
    return (uint32_t)i >= n ? n - 1 : (uint32_t)i;
  }
  // Gecerli (kati disi) 8 komsu + trilineer agirliklar.
  uint32_t corners(Vec3 p, Corner *out) const {
    if (!probes_) return 0;
    uint32_t i0[3], i1[3];
    float f[3];
    const float d[3] = {p.x - origin_.x, p.y - origin_.y, p.z - origin_.z};
    for (uint32_t a = 0; a < 3; a++) {
      float g = d[a] / spacing_;
      if (g < 0) g = 0;
      const float top = (float)(dim_[a] - 1);
      if (g > top) g = top;
      const float fl = std::floor(g);
      i0[a] = (uint32_t)fl;
      if (i0[a] >= dim_[a]) i0[a] = dim_[a] - 1;
      i1[a] = i0[a] + 1 < dim_[a] ? i0[a] + 1 : i0[a];
      f[a] = g - fl;
    }
    uint32_t k = 0;
    for (uint32_t b = 0; b < 8; b++) {
      const uint32_t x = (b & 1) ? i1[0] : i0[0], y = (b & 2) ? i1[1] : i0[1], z = (b & 4) ? i1[2] : i0[2];
      const float w = ((b & 1) ? f[0] : 1.0f - f[0]) * ((b & 2) ? f[1] : 1.0f - f[1]) * ((b & 4) ? f[2] : 1.0f - f[2]);
      if (w <= 0) continue;
      const uint32_t idx = index(x, y, z);
      if (probes_[idx].flags & 1u) continue; // kati icinde: agirlik 0
      out[k].index = idx;
      out[k].w = w;
      k++;
    }
    return k;
  }
  const SceneBlobGiProbe *probes_ = nullptr;
  uint32_t count_ = 0, flags_ = 0;
  uint32_t dim_[3] = {0, 0, 0};
  Vec3 origin_{0, 0, 0}, ambient_{0, 0, 0};
  float spacing_ = 0;
};

} // namespace tulpar::engine::content
