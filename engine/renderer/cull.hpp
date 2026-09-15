// L3 RENDERER — GPU GORUNURLUK KUMELEME (cull) + DOLAYLI (indirect) CIZIM.
// PLAN.md Faz 9 ilk adim. Cluster DAG (icerik tarafi, content/) BURADA DEGIL:
// bu dosya yalniz cizici tarafini — "hangi cizim hangi frustum'da gorunur" —
// ve bunun GPU'ya tasinan veri duzenini tanimlar.
//
// Vulkan tipi ICERMEZ (graph.hpp ile ayni ilke: saf veri): boylece frustum
// cikarimi ve kure testi GPU OLMADAN, tek basina test edilebilir — GPU kapisi
// duserse "sayi mi yanlis, boru hatti mi" sorusu ayrilabilir olur.
//
// --- Neden kume (batch)? ---------------------------------------------------
// Cihaz ozelligi multiDrawIndirect KAPALI (rhi/device.cpp bu ozelligi
// acmiyor): tek bir vkCmdDrawIndexedIndirect cagrisi EN COK 1 komut okur.
// drawIndirectFirstInstance de kapali, yani komuttaki firstInstance 0 OLMAK
// ZORUNDA ve gl_InstanceIndex 0'dan baslar; gl_DrawID (shaderDrawParameters)
// da yok. Bu ucu birlikte tek yolu birakir: ayni (mesh, malzeme) ciftinden
// olusan ardisik cizimler bir KUME'dir, kume basina TEK dolayli komut verilir,
// hayatta kalanlar cull gecisinde kume icinde SIKISTIRILIR ve vertex shader
// cizim kaydini `gorunurluk_listesi[taban + gl_InstanceIndex]` ile bulur.
// Sonuc: CPU kare basina cizim sayisi kadar degil KUME sayisi kadar komut
// verir; gorunurluk kararinin tamami GPU'da.
//
// --- Bilinen sinirlar (uydurma yok, sirada duruyor) ------------------------
// 1. LOD SECIMI cull gecisinde YAPILAMIYOR (PLAN Faz 9 "LOD cut secimi cull
//    pass'inde"): ornek basina farkli LOD, ornek basina AYRI dolayli komut
//    ister; multiDrawIndirect kapaliyken bir kume tek komuttur ve kumedeki
//    butun ornekler ayni indeks araligini kullanir. Yol acik: ozellik
//    acildiginda (rhi/device.cpp) kume basina komut yerine ORNEK basina komut
//    + vkCmdDrawIndexedIndirectCount'a gecilir; GpuBatch'teki mesh araligi
//    (index_count / first_index / vertex_offset) o gun LOD dugumunden gelir.
// 2. Ayni sebeple ISKELETLI (skinned) cizimler cull DISINDA: vertex duzeni ve
//    eklem SSBO'su farkli, kendi shader'ini ister. CPU yolunda cizilir ve
//    CullInfo::cpu_draws'da SAYILIR (sessizce kaybolmaz).
// 3. Cizim SIRASI: kume icinde sira korunur (cull sikistirmasi belirlenimli),
//    ama kume DISINDA kalan cizimler (iskeletli / kapasite) kumelerden SONRA
//    kaydedilir. Opak geometri + derinlik on gecisi ile piksel sonucu
//    degismez; siraya duyarli bir yol (saydam) eklenirse burasi gozden
//    gecirilmeli.
// 4. Dogrulama katmani cull.comp icin bilgi amacli
//    "BestPractices-SpirvDeprecated_WorkgroupSize" uyarisi verir (Arm DEGIL):
//    compile_shaders.py butun shader'lari --target-env=vulkan1.1 ile
//    derliyor; LocalSizeId Vulkan 1.3 ister. Cihaz alt siniri (Mali-G72,
//    Vulkan 1.1) yukselene kadar bu uyari kabul edilmistir.
#pragma once
#include <cmath>
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

// Bir karede cull edilen frustum sayisi: kamera + golge kademeleri.
// (Renderer::kMaxCascades = 3 ile tutarli; burada bagimsiz sabit tutuluyor ki
// bu baslik renderer.hpp'yi icermek zorunda kalmasin.)
constexpr uint32_t kMaxCullFrusta = 4;

// --- Frustum: 6 duzlem, ic taraf POZITIF, normaller BIRIM ------------------
// Birim normal sart: kure testi `dot(n, c) + d < -r` metrik uzaklik ister;
// normalize edilmemis duzlemde r'nin birimi tutmaz ve buyuk nesneler yanlis
// elenir (sessiz, ekranda kaybolan nesne olarak gorunur).
struct Frustum {
  float p[6][4]; // sol, sag, alt, ust, yakin, uzak
};

// Gribb-Hartmann: clip = vp * v oldugundan duzlemler vp'nin SATIRLARININ
// toplam/farklaridir. Vulkan gelenegi (z in [0,1]) icin yakin duzlemi z >= 0,
// uzak duzlemi z <= w. Mat4 SUTUN dizilidir (m[sutun][satir]).
inline Frustum frustum_from_viewproj(const Mat4 &vp) {
  float row[4][4];
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) row[r][c] = vp.m[c][r];
  Frustum f{};
  for (int i = 0; i < 4; i++) {
    f.p[0][i] = row[3][i] + row[0][i]; // sol
    f.p[1][i] = row[3][i] - row[0][i]; // sag
    f.p[2][i] = row[3][i] + row[1][i]; // alt
    f.p[3][i] = row[3][i] - row[1][i]; // ust
    f.p[4][i] = row[2][i];             // yakin
    f.p[5][i] = row[3][i] - row[2][i]; // uzak
  }
  for (int k = 0; k < 6; k++) {
    const float n = std::sqrt(f.p[k][0] * f.p[k][0] + f.p[k][1] * f.p[k][1] + f.p[k][2] * f.p[k][2]);
    if (n > 1e-20f) {
      const float inv = 1.0f / n;
      for (int i = 0; i < 4; i++) f.p[k][i] *= inv;
    }
  }
  return f;
}

// CPU referansi: GPU shader'i ile AYNI testi yapar. Kapi GPU'nun verdigi
// sayiyi buna karsi olcer — ikisi ayrilirsa hangisinin yanlis oldugu bellidir.
inline bool sphere_in_frustum(const Frustum &f, Vec3 c, float r) {
  for (int k = 0; k < 6; k++)
    if (f.p[k][0] * c.x + f.p[k][1] * c.y + f.p[k][2] * c.z + f.p[k][3] < -r) return false;
  return true;
}

// Model matrisi altinda YEREL kurenin dunya karsiligi (shader ile ayni tarif:
// merkez donusur, yaricap en buyuk eksen olcegiyle carpilir — kesme (shear)
// varsa muhafazakar ust sinir, yani asla ERKEN elemez).
inline float world_sphere_radius(const Mat4 &model, float local_radius) {
  const float sx = std::sqrt(model.m[0][0] * model.m[0][0] + model.m[0][1] * model.m[0][1] + model.m[0][2] * model.m[0][2]);
  const float sy = std::sqrt(model.m[1][0] * model.m[1][0] + model.m[1][1] * model.m[1][1] + model.m[1][2] * model.m[1][2]);
  const float sz = std::sqrt(model.m[2][0] * model.m[2][0] + model.m[2][1] * model.m[2][1] + model.m[2][2] * model.m[2][2]);
  const float s = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
  return local_radius * s;
}
inline Vec3 world_sphere_center(const Mat4 &model, Vec3 local_center) {
  const Vec4 w = model * Vec4{local_center.x, local_center.y, local_center.z, 1.0f};
  return Vec3{w.x, w.y, w.z};
}

// --- GPU veri duzenleri (std430; GLSL karsiliklari cull.comp / mesh_cull.vert) ---
// Cizim basina kayit: model matrisi + renk + YEREL sinir kuresi + malzeme.
// "Renderer bugun her cizimi CPU'dan veriyor" maddesinin karsiligi: bu kayit
// GPU'ya gider, CPU yalniz kume basina komut verir.
struct GpuDrawItem {
  float model[16];    // 0
  float color[4];     // 64  rgb + ayrilmis
  float sphere[4];    // 80  yerel merkez.xyz + yerel yaricap
  uint32_t misc[4];   // 96  x: skin ofseti, y: malzeme, z: bayrak, w: ayrilmis
};
static_assert(sizeof(GpuDrawItem) == 112, "std430 cizim kaydi 112 bayt");

// Kume: ardisik ayni (mesh, malzeme) cizimleri + MESH ARALIGI. Aralik bugun
// mesh basina ayri tampon oldugu icin (indexCount, 0, 0); tek buyuk vertex/
// indeks arenasina gecildiginde first_index/vertex_offset burada dolar ve
// dolayli komut degismeden calisir.
struct GpuBatch {
  uint32_t first_draw;
  uint32_t draw_count;
  uint32_t index_count;
  uint32_t first_index;
  int32_t vertex_offset;
  uint32_t pad[3];
};
static_assert(sizeof(GpuBatch) == 32, "std430 kume kaydi 32 bayt");

// VkDrawIndexedIndirectCommand ile BAYT BAYT ayni (renderer.cpp bunu
// static_assert ile dogrular): indexCount, instanceCount, firstIndex,
// vertexOffset, firstInstance.
struct GpuIndirectCmd {
  uint32_t index_count;
  uint32_t instance_count;
  uint32_t first_index;
  int32_t vertex_offset;
  uint32_t first_instance;
};
static_assert(sizeof(GpuIndirectCmd) == 20, "dolayli komut 20 bayt");

// CPU tarafi kume kaydi (GPU'ya gitmez: baglama bilgisi).
struct CullBatchCpu {
  uint32_t mesh = 0;
  uint32_t material = 0;
  uint32_t first_draw = 0;
  uint32_t draw_count = 0;
};

// --- Disari verilen olcum --------------------------------------------------
// Sessiz kapanma YOK: gpu_cull istendi ama kurulamadiysa disabled_reason dolar
// ve CPU yolu kosar; kapi bunu GORUNUR atlama olarak basar.
struct CullInfo {
  bool enabled = false;
  const char *disabled_reason = "";
  uint32_t candidates = 0;   // cull'a giren (kumelenmis) cizim
  uint32_t batches = 0;      // dolayli komut sayisi = CPU'nun verdigi cizim
  uint32_t survived = 0;     // kamera frustum'unda hayatta kalan (GPU sayaci)
  uint32_t culled = 0;       // candidates - survived
  uint32_t cpu_draws = 0;    // cull DISINDA kalan (iskeletli / kume kapasitesi dolu)
  uint32_t frusta = 0;       // bu karede test edilen frustum sayisi
  bool shadow = false;       // golge kademeleri de dolayli cizildi mi
  const char *shadow_reason = "";
  uint32_t shadow_candidates = 0; // kademe sayisi * aday
  uint32_t shadow_survived = 0;   // butun kademelerin toplami
  bool counts_valid = false; // sayilar GPU tamponundan GERCEKTEN okundu
  bool timing = false;
  const char *timing_reason = "";
  float compute_ms = 0; // cull compute gecisi (zaman damgasi)
  uint64_t gpu_bytes = 0; // cull'un actigi GPU tamponlarinin toplami
};

} // namespace tulpar::engine::renderer
