// CPU-GPU YERLESIM KAPISI (Faz 8.4) — shader bloklarinin std140/std430
// yerlesimi ile C++ struct'larinin bayt yerlesimi ayni mi?
//
// Bu sinif hata SESSIZ: ne derleyici, ne linker, ne Vulkan dogrulama katmani
// bir ofset kaymasini soyler. GPU baska bir ofsetten okur, goruntu "biraz
// yanlis" olur. Depodaki elle yazilmis `static_assert(sizeof(X) == N)`
// kaliplari bu hatanin yalniz BIR alt kumesini goruyor: ayni boyutta alan
// sirasi degismis bir struct o kapidan KACAR.
//
// Beklenen sayilar ELLE YAZILMIYOR: SPIR-V (`rhi/shaders/*_spv.h`, glslc'nin
// urettigi ve depoya giren baytlar) burada AYRISTIRILIR, `OpMemberDecorate
// Offset` okunur ve gercek C++ tipleriyle `offsetof` uzerinden karsilastirilir.
//
// Kardes arac: `engine/tools/layout_check.py` — o, ozel (private) struct'lar
// dahil BUTUN bloklari kapsar (uretilmis bir sonda TU'su ile) ve blok/uye
// adlarini GLSL kaynagindan alir. Bu test, engine_tests'in normal kosumunda
// da yakalansin diye erisilebilir tipleri baglar.
#include "tests/test.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "renderer/cull.hpp"
#include "renderer/renderer.hpp"

#include "rhi/shaders/bloom_bright_frag_spv.h"
#include "rhi/shaders/bloom_down_frag_spv.h"
#include "rhi/shaders/bloom_up_frag_spv.h"
#include "rhi/shaders/compose_frag_spv.h"
#include "rhi/shaders/cull_comp_spv.h"
#include "rhi/shaders/mesh_cull_vert_spv.h"
#include "rhi/shaders/mesh_frag_spv.h"
#include "rhi/shaders/mesh_skin_vert_spv.h"
#include "rhi/shaders/mesh_vert_spv.h"
#include "rhi/shaders/motion_frag_spv.h"
#include "rhi/shaders/motion_vert_spv.h"
#include "rhi/shaders/post_vert_spv.h"
#include "rhi/shaders/shadow_cull_vert_spv.h"
#include "rhi/shaders/shadow_skin_vert_spv.h"
#include "rhi/shaders/shadow_vert_spv.h"
#include "rhi/shaders/triangle_frag_spv.h"
#include "rhi/shaders/triangle_vert_spv.h"
#include "rhi/shaders/ui_frag_spv.h"
#include "rhi/shaders/ui_overdraw_frag_spv.h"
#include "rhi/shaders/ui_sdf_frag_spv.h"
#include "rhi/shaders/ui_vert_spv.h"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

namespace {

// --- SPIR-V cekirdegi: yalniz yerlesim icin gereken kismi ------------------
enum : uint16_t {
  kOpEntryPoint = 15, kOpTypeVoid = 19, kOpTypeBool = 20, kOpTypeInt = 21,
  kOpTypeFloat = 22, kOpTypeVector = 23, kOpTypeMatrix = 24, kOpTypeArray = 28,
  kOpTypeRuntimeArray = 29, kOpTypeStruct = 30, kOpTypePointer = 32,
  kOpConstant = 43, kOpVariable = 59, kOpDecorate = 71, kOpMemberDecorate = 72,
};
enum : uint32_t {
  kDecBlock = 2, kDecBufferBlock = 3, kDecArrayStride = 6, kDecMatrixStride = 7,
  kDecBuiltIn = 11, kDecLocation = 30, kDecBinding = 33, kDecDescriptorSet = 34, kDecOffset = 35,
};
enum : uint32_t { kScInput = 1, kScUniform = 2, kScPushConstant = 9, kScStorageBuffer = 12 };

constexpr uint32_t kMaxId = 4096;
constexpr uint32_t kMaxMemberDec = 4096;
constexpr uint32_t kMaxVar = 256;
constexpr uint32_t kMaxMembers = 32;

struct MemberDec {
  uint32_t sid;
  uint16_t idx;
  uint16_t dec;
  uint32_t val;
};

// Yerlesim bilgisi: bir blogun (ya da dizi ELEMANININ) ust duzey uyeleri.
struct Layout {
  bool found = false;
  uint32_t member_count = 0;
  uint32_t offset[kMaxMembers] = {};
  uint32_t size[kMaxMembers] = {};
  uint32_t extent = 0;        // son uyenin sonu
  uint32_t array_stride = 0;  // eleman karsilastirmasinda dizi adimi
};

// Tek shader'in tip/dekorasyon tablosu. Statik degil: yigit kullanmak yerine
// cagiran tarafta tek nesne tutulur (AllocGate: `new` YOK).
struct Reflect {
  uint16_t op[kMaxId] = {};
  uint32_t a[kMaxId] = {}, b[kMaxId] = {}, c[kMaxId] = {};
  uint32_t dec_set[kMaxId] = {}, dec_bind[kMaxId] = {}, dec_astride[kMaxId] = {};
  uint32_t dec_loc[kMaxId] = {};
  uint8_t has_loc[kMaxId] = {}, is_builtin[kMaxId] = {};
  uint8_t is_block[kMaxId] = {}, is_buffer_block[kMaxId] = {};
  uint32_t constant[kMaxId] = {};
  MemberDec md[kMaxMemberDec] = {};
  uint32_t md_n = 0;
  uint32_t var_id[kMaxVar] = {}, var_type[kMaxVar] = {}, var_sc[kMaxVar] = {};
  uint32_t var_n = 0;
  uint32_t bound = 0;
  bool ok = false;

  uint32_t mdec(uint32_t sid, uint32_t idx, uint16_t dec, uint32_t def) const {
    for (uint32_t i = 0; i < md_n; i++)
      if (md[i].sid == sid && md[i].idx == idx && md[i].dec == dec) return md[i].val;
    return def;
  }

  void parse(const uint32_t *w, uint32_t n) {
    ok = false;
    if (n < 5 || w[0] != 0x07230203u) return;
    bound = w[3];
    if (bound >= kMaxId) return;
    uint32_t i = 5;
    while (i < n) {
      const uint32_t wc = w[i] >> 16;
      const uint16_t o = (uint16_t)(w[i] & 0xFFFFu);
      if (wc == 0 || i + wc > n) return;
      const uint32_t *p = w + i + 1;
      const uint32_t na = wc - 1;
      switch (o) {
        case kOpDecorate:
          if (na >= 2 && p[0] < kMaxId) {
            if (p[1] == kDecBlock) is_block[p[0]] = 1;
            else if (p[1] == kDecBufferBlock) is_buffer_block[p[0]] = 1;
            else if (p[1] == kDecBuiltIn) is_builtin[p[0]] = 1;
            else if (na >= 3 && p[1] == kDecDescriptorSet) dec_set[p[0]] = p[2] + 1;
            else if (na >= 3 && p[1] == kDecBinding) dec_bind[p[0]] = p[2] + 1;
            else if (na >= 3 && p[1] == kDecArrayStride) dec_astride[p[0]] = p[2];
            else if (na >= 3 && p[1] == kDecLocation) { dec_loc[p[0]] = p[2]; has_loc[p[0]] = 1; }
          }
          break;
        case kOpMemberDecorate:
          if (na >= 4 && md_n < kMaxMemberDec && (p[2] == kDecOffset || p[2] == kDecMatrixStride)) {
            md[md_n].sid = p[0];
            md[md_n].idx = (uint16_t)p[1];
            md[md_n].dec = (uint16_t)p[2];
            md[md_n].val = p[3];
            md_n++;
          }
          break;
        case kOpConstant:
          if (na >= 3 && p[1] < kMaxId) constant[p[1]] = p[2];
          break;
        case kOpVariable:
          if (na >= 3 && var_n < kMaxVar) {
            var_id[var_n] = p[1];
            var_type[var_n] = p[0];
            var_sc[var_n] = p[2];
            var_n++;
          }
          break;
        default:
          if (na >= 1 && (o == kOpTypeVoid || o == kOpTypeBool || o == kOpTypeInt ||
                          o == kOpTypeFloat || o == kOpTypeVector || o == kOpTypeMatrix ||
                          o == kOpTypeArray || o == kOpTypeRuntimeArray || o == kOpTypeStruct ||
                          o == kOpTypePointer)) {
            const uint32_t id = p[0];
            if (id < kMaxId) {
              op[id] = o;
              a[id] = na >= 2 ? p[1] : 0;
              b[id] = na >= 3 ? p[2] : 0;
              c[id] = na >= 4 ? p[3] : 0;
              if (o == kOpTypeStruct) a[id] = na - 1; // uye sayisi
            }
          }
          break;
      }
      i += wc;
    }
    ok = true;
  }

  // struct uye tip kimligi: OpTypeStruct'in operandlari ardisik duruyor, ama
  // yalniz ilk ucunu sakladik. Uye tiplerini yeniden taramak yerine ikinci bir
  // gecis yapiyoruz (test kodunda basitlik > hiz).
  uint32_t struct_member_type(const uint32_t *w, uint32_t n, uint32_t sid, uint32_t idx) const {
    uint32_t i = 5;
    while (i < n) {
      const uint32_t wc = w[i] >> 16;
      const uint16_t o = (uint16_t)(w[i] & 0xFFFFu);
      if (wc == 0 || i + wc > n) return 0;
      if (o == kOpTypeStruct && w[i + 1] == sid && idx + 2 < wc) return w[i + 2 + idx];
      i += wc;
    }
    return 0;
  }

  uint32_t scalar_bytes(uint32_t t) const {
    if (t >= kMaxId) return 0;
    if (op[t] == kOpTypeFloat || op[t] == kOpTypeInt) return a[t] / 8;
    if (op[t] == kOpTypeBool) return 4;
    return 0;
  }

  // Bir tipin bayt uzantisi. mstride: uye dekorasyonundan gelen matris adimi.
  uint32_t extent_of(const uint32_t *w, uint32_t n, uint32_t t, uint32_t mstride) const {
    if (t >= kMaxId) return 0;
    switch (op[t]) {
      case kOpTypeFloat: case kOpTypeInt: case kOpTypeBool: return scalar_bytes(t);
      case kOpTypeVector: return scalar_bytes(a[t]) * b[t];
      case kOpTypeMatrix: {
        const uint32_t cols = b[t];
        const uint32_t st = mstride ? mstride : extent_of(w, n, a[t], 0);
        return st * cols;
      }
      case kOpTypeArray: {
        const uint32_t len = (b[t] < kMaxId) ? constant[b[t]] : 0;
        return dec_astride[t] * len;
      }
      case kOpTypeRuntimeArray: return dec_astride[t];
      case kOpTypeStruct: {
        uint32_t e = 0;
        for (uint32_t m = 0; m < a[t]; m++) {
          const uint32_t mt = struct_member_type(w, n, t, m);
          const uint32_t off = mdec(t, m, kDecOffset, 0);
          const uint32_t ms = mdec(t, m, kDecMatrixStride, 0);
          const uint32_t end = off + extent_of(w, n, mt, ms);
          if (end > e) e = end;
        }
        return e;
      }
      default: return 0;
    }
  }

  void fill(const uint32_t *w, uint32_t n, uint32_t sid, Layout *out) const {
    out->found = true;
    out->member_count = a[sid] > kMaxMembers ? kMaxMembers : a[sid];
    for (uint32_t m = 0; m < out->member_count; m++) {
      const uint32_t mt = struct_member_type(w, n, sid, m);
      out->offset[m] = mdec(sid, m, kDecOffset, 0);
      out->size[m] = extent_of(w, n, mt, mdec(sid, m, kDecMatrixStride, 0));
      const uint32_t e = out->offset[m] + out->size[m];
      if (e > out->extent) out->extent = e;
    }
  }

  // set/binding'deki blogu bul. `element` true ise blok `{ T x[]; }` kalibinda
  // kabul edilir ve ELEMAN struct'inin yerlesimi doner (dizi adimiyla).
  Layout block(const uint32_t *w, uint32_t n, int set, int binding, bool element) const {
    Layout L;
    for (uint32_t v = 0; v < var_n; v++) {
      const uint32_t sc = var_sc[v];
      if (sc != kScUniform && sc != kScStorageBuffer && sc != kScPushConstant) continue;
      const uint32_t pt = var_type[v];
      if (pt >= kMaxId || op[pt] != kOpTypePointer) continue;
      const uint32_t sid = b[pt];
      if (sid >= kMaxId || op[sid] != kOpTypeStruct) continue;
      if (!is_block[sid] && !is_buffer_block[sid]) continue;
      if (sc != kScPushConstant) {
        if ((int)dec_set[var_id[v]] - 1 != set || (int)dec_bind[var_id[v]] - 1 != binding) continue;
      } else if (set >= 0) {
        continue;
      }
      if (!element) {
        fill(w, n, sid, &L);
        return L;
      }
      if (a[sid] != 1) continue;
      const uint32_t at = struct_member_type(w, n, sid, 0);
      if (at >= kMaxId) continue;
      if (op[at] != kOpTypeArray && op[at] != kOpTypeRuntimeArray) continue;
      const uint32_t et = a[at];
      if (et >= kMaxId || op[et] != kOpTypeStruct) continue;
      fill(w, n, et, &L);
      L.array_stride = dec_astride[at];
      return L;
    }
    return L;
  }

  uint32_t input_count() const {
    uint32_t k = 0;
    for (uint32_t v = 0; v < var_n; v++)
      if (var_sc[v] == kScInput && has_loc[var_id[v]] && !is_builtin[var_id[v]]) k++;
    return k;
  }
};

struct ShaderBlob {
  const char *name;
  const uint32_t *words;
  uint32_t bytes;
};

const ShaderBlob k_shaders[] = {
    {"bloom_bright.frag", bloom_bright_frag_spv, bloom_bright_frag_spv_size},
    {"bloom_down.frag", bloom_down_frag_spv, bloom_down_frag_spv_size},
    {"bloom_up.frag", bloom_up_frag_spv, bloom_up_frag_spv_size},
    {"compose.frag", compose_frag_spv, compose_frag_spv_size},
    {"cull.comp", cull_comp_spv, cull_comp_spv_size},
    {"mesh_cull.vert", mesh_cull_vert_spv, mesh_cull_vert_spv_size},
    {"mesh.frag", mesh_frag_spv, mesh_frag_spv_size},
    {"mesh_skin.vert", mesh_skin_vert_spv, mesh_skin_vert_spv_size},
    {"mesh.vert", mesh_vert_spv, mesh_vert_spv_size},
    {"motion.frag", motion_frag_spv, motion_frag_spv_size},
    {"motion.vert", motion_vert_spv, motion_vert_spv_size},
    {"post.vert", post_vert_spv, post_vert_spv_size},
    {"shadow_cull.vert", shadow_cull_vert_spv, shadow_cull_vert_spv_size},
    {"shadow_skin.vert", shadow_skin_vert_spv, shadow_skin_vert_spv_size},
    {"shadow.vert", shadow_vert_spv, shadow_vert_spv_size},
    {"triangle.frag", triangle_frag_spv, triangle_frag_spv_size},
    {"triangle.vert", triangle_vert_spv, triangle_vert_spv_size},
    {"ui.frag", ui_frag_spv, ui_frag_spv_size},
    {"ui_overdraw.frag", ui_overdraw_frag_spv, ui_overdraw_frag_spv_size},
    {"ui_sdf.frag", ui_sdf_frag_spv, ui_sdf_frag_spv_size},
    {"ui.vert", ui_vert_spv, ui_vert_spv_size},
};
constexpr uint32_t k_shader_n = (uint32_t)(sizeof(k_shaders) / sizeof(k_shaders[0]));

// Tek karsilastirici: hem gercek kapida hem POZITIF KONTROLDE kullanilir.
// Donus: uyusmayan alan sayisi. `say` true ise farklari basar.
uint32_t compare(const char *what, const Layout &gpu, const uint32_t *cpu_off,
                 const uint32_t *cpu_size, uint32_t n, uint32_t cpu_sizeof, bool say) {
  uint32_t bad = 0;
  if (!gpu.found) {
    if (say) std::printf("    FAIL %s: SPIR-V blogu BULUNAMADI (denetim olcmuyor)\n", what);
    return 1;
  }
  if (gpu.member_count != n) {
    if (say)
      std::printf("    FAIL %s: GPU %u uye, C++ %u uye\n", what, gpu.member_count, n);
    return 1;
  }
  for (uint32_t i = 0; i < n; i++) {
    if (gpu.offset[i] != cpu_off[i] || gpu.size[i] != cpu_size[i]) {
      bad++;
      if (say)
        std::printf("    FAIL %s uye %u: GPU ofs=%u boy=%u | C++ ofs=%u boy=%u (ofset farki %+d)\n",
                    what, i, gpu.offset[i], gpu.size[i], cpu_off[i], cpu_size[i],
                    (int)cpu_off[i] - (int)gpu.offset[i]);
    }
  }
  if (gpu.array_stride && gpu.array_stride != cpu_sizeof) {
    bad++;
    if (say)
      std::printf("    FAIL %s: dizi adimi GPU=%u | sizeof=%u\n", what, gpu.array_stride, cpu_sizeof);
  }
  if (gpu.extent > cpu_sizeof) {
    bad++;
    if (say)
      std::printf("    FAIL %s: GPU %u bayt okuyor, sizeof=%u (GPU STRUCT'I TASAR)\n", what,
                  gpu.extent, cpu_sizeof);
  }
  return bad;
}

// GpuDrawItem <-> DrawItem (cull.comp / mesh_cull.vert / shadow_cull.vert)
const uint32_t k_draw_off[4] = {
    (uint32_t)offsetof(GpuDrawItem, model), (uint32_t)offsetof(GpuDrawItem, color),
    (uint32_t)offsetof(GpuDrawItem, sphere), (uint32_t)offsetof(GpuDrawItem, misc)};
const uint32_t k_draw_size[4] = {
    (uint32_t)sizeof(GpuDrawItem::model), (uint32_t)sizeof(GpuDrawItem::color),
    (uint32_t)sizeof(GpuDrawItem::sphere), (uint32_t)sizeof(GpuDrawItem::misc)};

// GpuBatch <-> Batch: GLSL'de `pad0/pad1/pad2`, C++'ta `pad[3]` — ayni baytlar.
const uint32_t k_batch_off[8] = {
    (uint32_t)offsetof(GpuBatch, first_draw),  (uint32_t)offsetof(GpuBatch, draw_count),
    (uint32_t)offsetof(GpuBatch, index_count), (uint32_t)offsetof(GpuBatch, first_index),
    (uint32_t)offsetof(GpuBatch, vertex_offset), (uint32_t)offsetof(GpuBatch, pad),
    (uint32_t)offsetof(GpuBatch, pad) + 4, (uint32_t)offsetof(GpuBatch, pad) + 8};
const uint32_t k_batch_size[8] = {4, 4, 4, 4, 4, 4, 4, 4};

} // namespace

// SPIR-V ayristirici gercekten 21 shader'i cozuyor mu? Cozemezse asagidaki
// kapilar "blok bulunamadi" ile KIRMIZI olur; burasi nedenini gosterir.
ENGINE_TEST(layout_spirv_shaderlari_cozuluyor) {
  static Reflect r;
  uint32_t parsed = 0, with_block = 0;
  for (uint32_t i = 0; i < k_shader_n; i++) {
    r = Reflect{};
    r.parse(k_shaders[i].words, k_shaders[i].bytes / 4);
    if (!r.ok) {
      std::printf("    FAIL %s: SPIR-V ayristirilamadi (bound=%u)\n", k_shaders[i].name, r.bound);
      continue;
    }
    parsed++;
    for (uint32_t v = 0; v < r.var_n; v++) {
      const uint32_t pt = r.var_type[v];
      if (pt < kMaxId && r.op[pt] == kOpTypePointer && r.b[pt] < kMaxId &&
          (r.is_block[r.b[pt]] || r.is_buffer_block[r.b[pt]])) {
        with_block++;
        break;
      }
    }
  }
  std::printf("    [bilgi] %u/%u shader cozuldu, %u tanesinde arayuz blogu var\n", parsed,
              k_shader_n, with_block);
  CHECK(parsed == k_shader_n);
  CHECK(with_block >= 10);
}

// GERCEK KAPI: cull boru hattinin SSBO elemanlari ile C++ struct'lari.
ENGINE_TEST(layout_cull_ssbo_cpp_structlariyla_ayni) {
  static Reflect r;
  uint32_t bad = 0;

  r = Reflect{};
  r.parse(cull_comp_spv, cull_comp_spv_size / 4);
  Layout draws = r.block(cull_comp_spv, cull_comp_spv_size / 4, 0, 0, true);
  bad += compare("cull.comp Draws.DrawItem", draws, k_draw_off, k_draw_size, 4, sizeof(GpuDrawItem), true);
  Layout batches = r.block(cull_comp_spv, cull_comp_spv_size / 4, 0, 1, true);
  bad += compare("cull.comp Batches.Batch", batches, k_batch_off, k_batch_size, 8, sizeof(GpuBatch), true);

  // AYNI struct, BASKA shader: cizim kaydi uc yerde bildiriliyor. Ucunun de
  // ayni C++ struct'ina uymasi, shader'lar arasi kaymayi da kapatir.
  r = Reflect{};
  r.parse(mesh_cull_vert_spv, mesh_cull_vert_spv_size / 4);
  Layout mv = r.block(mesh_cull_vert_spv, mesh_cull_vert_spv_size / 4, 0, 5, true);
  bad += compare("mesh_cull.vert Draws.DrawItem", mv, k_draw_off, k_draw_size, 4, sizeof(GpuDrawItem), true);

  r = Reflect{};
  r.parse(shadow_cull_vert_spv, shadow_cull_vert_spv_size / 4);
  Layout sv = r.block(shadow_cull_vert_spv, shadow_cull_vert_spv_size / 4, 0, 5, true);
  bad += compare("shadow_cull.vert Draws.DrawItem", sv, k_draw_off, k_draw_size, 4, sizeof(GpuDrawItem), true);

  std::printf("    [bilgi] DrawItem 3 shader'da, Batch 1 shader'da; %u uyusmazlik\n", bad);
  CHECK(bad == 0);
  // Dolayli komut: GLSL tarafi duz `uint c[]` (adim 4), C++ tarafi 5 x 4 bayt.
  CHECK(sizeof(GpuIndirectCmd) % 4 == 0);
}

// POZITIF KONTROL: karsilastirici gercekten olcuyor mu? Ayni kapi, BOZULMUS
// beklenti ile cagrilir; KIRMIZI donmezse kapi bos bir yesil uretiyor demektir.
ENGINE_TEST(layout_kapisi_bozuk_yerlesimi_yakaliyor) {
  static Reflect r;
  r = Reflect{};
  r.parse(cull_comp_spv, cull_comp_spv_size / 4);
  Layout draws = r.block(cull_comp_spv, cull_comp_spv_size / 4, 0, 0, true);
  CHECK(draws.found);

  // (i) bir alan 4 bayt kaymis
  uint32_t off_shift[4] = {k_draw_off[0], k_draw_off[1], k_draw_off[2], k_draw_off[3]};
  off_shift[2] += 4;
  const uint32_t n1 = compare("kontrol-kaydirma", draws, off_shift, k_draw_size, 4, sizeof(GpuDrawItem), false);

  // (ii) AYNI BOYUT, alan sirasi degismis (sizeof ayni kalir: 112).
  // `static_assert(sizeof(GpuDrawItem) == 112)` bu hatayi GORMEZ.
  uint32_t off_swap[4] = {k_draw_off[0], k_draw_off[2], k_draw_off[1], k_draw_off[3]};
  uint32_t size_swap[4] = {k_draw_size[0], k_draw_size[2], k_draw_size[1], k_draw_size[3]};
  const uint32_t n2 = compare("kontrol-sira", draws, off_swap, size_swap, 4, sizeof(GpuDrawItem), false);

  // (iii) blok yok: sessiz gecis olmamali
  Layout yok;
  const uint32_t n3 = compare("kontrol-yok", yok, k_draw_off, k_draw_size, 4, sizeof(GpuDrawItem), false);

  std::printf("    [bilgi] pozitif kontrol: kaydirma=%u, sira=%u, blok-yok=%u uyusmazlik\n", n1, n2, n3);
  CHECK(n1 > 0);
  CHECK(n2 > 0);
  CHECK(n3 > 0);
}

// Vertex girdi yerlesimi: shader'in bildirdigi konum sayisi ile CPU'nun
// paketledigi alan sayisi ayni mi? (Bicim donusumu var, alan SAYISI degil.)
ENGINE_TEST(layout_vertex_girdi_sayisi_cpu_ile_ayni) {
  static Reflect r;
  r = Reflect{};
  r.parse(mesh_vert_spv, mesh_vert_spv_size / 4);
  const uint32_t statik = r.input_count();
  r = Reflect{};
  r.parse(mesh_skin_vert_spv, mesh_skin_vert_spv_size / 4);
  const uint32_t iskeletli = r.input_count();
  std::printf("    [bilgi] mesh.vert %u girdi (GpuVertex 3 alan), mesh_skin.vert %u girdi "
              "(GpuSkinnedVertex 5 alan)\n", statik, iskeletli);
  CHECK(statik == 3);
  CHECK(iskeletli == 5);
  CHECK(sizeof(GpuVertex) == 20);
  CHECK(sizeof(GpuSkinnedVertex) == 32);
}
