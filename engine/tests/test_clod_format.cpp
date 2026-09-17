// content/clod_format.hpp: .clod yukleyicisi.
//
// Asil sinanan sey DOGRULAMA. Bu dosya diskten gelir; bozuk ya da kotu
// niyetli bir .clod, kontrol edilmezse CIZIM ANINDA sinir disi okumaya yol
// acar. Her kontrol icin ayri bir negatif test var.
#include "content/clod_format.hpp"

#include <cfloat>
#include <cstring>

#include "core/memory/arena.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

namespace {

constexpr uint32_t kN = 3;  // kume
constexpr uint32_t kI = 9;  // indeks (3 kume x 1 ucgen)
constexpr uint32_t kV = 6;  // vertex
constexpr uint32_t kStride = sizeof(renderer::Vertex) / sizeof(float); // 8

// alignas: bayt dizisini ClodHeader olarak okuyacagiz. Hizalama
// belirtilmezse dizinin hizalamasi 1 olur ve donusum bazi ARM
// yapilandirmalarinda yanlis veri ya da sinyal uretir.
struct Blob {
  alignas(8) uint8_t bytes[sizeof(ClodHeader) + kN * sizeof(ClodClusterRaw) +
                           kI * sizeof(uint32_t) + kV * kStride * sizeof(float)];
  uint32_t size = 0;

  ClodHeader *header() { return reinterpret_cast<ClodHeader *>(bytes); }
  ClodClusterRaw *cluster(uint32_t i) {
    return reinterpret_cast<ClodClusterRaw *>(bytes + header()->clusters_offset) + i;
  }
  uint32_t *index(uint32_t i) {
    return reinterpret_cast<uint32_t *>(bytes + header()->indices_offset) + i;
  }
};

Blob make_valid() {
  Blob b{};
  ClodHeader h{};
  h.magic = kClodMagic;
  h.version = kClodVersion;
  h.cluster_count = kN;
  h.index_count = kI;
  h.vertex_count = kV;
  h.vertex_stride_floats = kStride;
  h.clusters_offset = sizeof(ClodHeader);
  h.indices_offset = h.clusters_offset + kN * (uint32_t)sizeof(ClodClusterRaw);
  h.vertices_offset = h.indices_offset + kI * (uint32_t)sizeof(uint32_t);
  h.total_bytes = h.vertices_offset + kV * kStride * (uint32_t)sizeof(float);
  std::memcpy(b.bytes, &h, sizeof(h));
  b.size = h.total_bytes;

  ClodClusterRaw c[kN] = {};
  for (uint32_t i = 0; i < kN; i++) {
    c[i].center[0] = (float)i;
    c[i].radius = 1.0f;
    c[i].current.center[1] = 2.0f;
    c[i].current.radius = 3.0f;
    c[i].current.error = 0.0625f;
    c[i].coarser.error = 0.125f;
    c[i].index_offset = i * 3;
    c[i].index_count = 3;
    c[i].geometry = i;
    c[i].level = i;
  }
  c[kN - 1].coarser.error = FLT_MAX; // SON seviye
  std::memcpy(b.bytes + h.clusters_offset, c, sizeof(c));

  for (uint32_t i = 0; i < kI; i++) {
    const uint32_t v = i % kV; // her indeks vertex tamponunun ICINDE
    std::memcpy(b.bytes + h.indices_offset + i * sizeof(uint32_t), &v, sizeof(v));
  }
  for (uint32_t i = 0; i < kV * kStride; i++) {
    const float f = (float)i;
    std::memcpy(b.bytes + h.vertices_offset + i * sizeof(float), &f, sizeof(f));
  }
  return b;
}

// Her cagri TEMIZ bir arenayla calissin: testler birbirini etkilemesin.
bool loads(Blob &b) {
  static SystemArena sys;
  static bool ready = false;
  if (!ready) { ready = sys.reserve(1u << 20, "clod"); }
  sys.reset();
  ClodMesh m;
  return clod_load(sys, b.bytes, b.size, m);
}

} // namespace

ENGINE_TEST(clod_loads_valid_file_and_round_trips_values) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "clod"));
  Blob b = make_valid();

  ClodMesh m;
  CHECK(clod_load(sys, b.bytes, b.size, m));
  CHECK(m.cluster_count == kN);
  CHECK(m.index_count == kI);
  CHECK(m.vertex_count == kV);

  // Degerler BOZULMADAN gecmeli.
  CHECK(m.clusters[1].center.x == 1.0f);
  CHECK(m.clusters[1].radius == 1.0f);
  CHECK(m.clusters[1].current.center.y == 2.0f);
  CHECK(m.clusters[1].current.radius == 3.0f);
  CHECK(m.clusters[1].current.error == 0.0625f);
  CHECK(m.clusters[1].coarser.error == 0.125f);
  CHECK(m.clusters[kN - 1].coarser.error == FLT_MAX); // SON seviye korunmali
  CHECK(m.clusters[2].geometry == 2);
  CHECK(m.clusters[2].level == 2);
  CHECK(m.clusters[2].index_offset == 6);
  CHECK(m.clusters[2].index_count == 3);

  // Indeksler ve vertexler kopyalanmaz, dogrudan tampona bakar.
  CHECK(m.indices == reinterpret_cast<const uint32_t *>(b.bytes + b.header()->indices_offset));
  CHECK(m.vertices == reinterpret_cast<const float *>(b.bytes + b.header()->vertices_offset));
  CHECK(m.vertices[0] == 0.0f);

  const renderer::ClusterLodMesh v = clod_view(m);
  CHECK(v.cluster_count == kN);
  CHECK(v.clusters == m.clusters);
}

ENGINE_TEST(clod_rejects_bad_magic_and_version) {
  Blob b = make_valid();
  b.header()->magic = 0xDEADBEEFu;
  CHECK(!loads(b));

  b = make_valid();
  b.header()->version = kClodVersion + 1; // ileri surumu YANLIS okumaktansa reddet
  CHECK(!loads(b));
}

ENGINE_TEST(clod_rejects_wrong_vertex_stride) {
  // Vertex duzeni renderer::Vertex ile ayni olmak ZORUNDA: dosya dogrudan
  // vertex buffer olarak yuklenecek. Yapi degisirse eski dosyalar SESSIZCE
  // yanlis okunmamali.
  Blob b = make_valid();
  b.header()->vertex_stride_floats = kStride + 1;
  CHECK(!loads(b));
  b = make_valid();
  b.header()->vertex_stride_floats = 0;
  CHECK(!loads(b));
}

ENGINE_TEST(clod_rejects_truncated_file) {
  Blob b = make_valid();
  b.size = b.header()->total_bytes - 4; // son float eksik
  CHECK(!loads(b));

  b = make_valid();
  b.size = 8; // baslik bile tamamlanmamis
  CHECK(!loads(b));
}

ENGINE_TEST(clod_rejects_cluster_index_range_outside_buffer) {
  // TEHLIKE: kume, indeks tamponunun DISINI gosterirse cizim aninda sinir
  // disi okuma olur.
  Blob b = make_valid();
  b.cluster(1)->index_count = kI; // offset 3 + 9 > 9
  CHECK(!loads(b));

  b = make_valid();
  b.cluster(0)->index_offset = kI; // tam sinirda baslayip 3 okumak
  CHECK(!loads(b));

  // Tam sinirda BITEN aralik GECERLI (kapali-acik aralik).
  b = make_valid();
  b.cluster(0)->index_offset = kI - 3;
  b.cluster(0)->index_count = 3;
  CHECK(loads(b));
}

ENGINE_TEST(clod_rejects_index_pointing_past_vertex_buffer) {
  // ASIL TEHLIKE: indeks araligi dogru olsa bile indeksin KENDISI vertex
  // tamponunun disini gosterebilir.
  Blob b = make_valid();
  *b.index(4) = kV; // tam sinirin disi
  CHECK(!loads(b));

  b = make_valid();
  *b.index(0) = 0xFFFFFFFFu;
  CHECK(!loads(b));

  // Son gecerli vertex KABUL edilmeli.
  b = make_valid();
  *b.index(0) = kV - 1;
  CHECK(loads(b));
}

ENGINE_TEST(clod_rejects_unaligned_offsets) {
  // uint32/float dizisine hizasiz isaretci ile bakmak bazi ARM
  // yapilandirmalarinda yanlis veri ya da sinyal uretir.
  Blob b = make_valid();
  b.header()->indices_offset += 1;
  CHECK(!loads(b));

  b = make_valid();
  b.header()->vertices_offset += 2;
  CHECK(!loads(b));
}

ENGINE_TEST(clod_rejects_empty_counts) {
  // Bos veri "yuklendi" sayilmamali: cagiran bos ekrana bakip nedenini
  // arardi. Basarisizlik GORUNUR olsun.
  Blob b = make_valid();
  b.header()->cluster_count = 0;
  CHECK(!loads(b));

  b = make_valid();
  b.header()->index_count = 0;
  CHECK(!loads(b));

  b = make_valid();
  b.header()->vertex_count = 0;
  CHECK(!loads(b));
}

ENGINE_TEST(clod_rejects_counts_that_would_overflow_32bit_math) {
  // EN SINSI DURUM: cluster_count * sizeof(ClodClusterRaw) 32 bitte SARAR.
  // 72 bayt/kume oldugu icin 0x0E38E38E * 72 ~ 2^32. Kontrol 64 bit
  // yapilmazsa dosya "sigiyor" sanilip KABUL EDILIR ve gigabaytlarca
  // sinir disi okuma denenir.
  Blob b = make_valid();
  b.header()->cluster_count = 0x0E38E38Eu;
  CHECK(!loads(b));

  b = make_valid();
  b.header()->index_count = 0x40000000u; // * 4 = 2^32
  CHECK(!loads(b));

  b = make_valid();
  b.header()->vertex_count = 0x20000000u; // * 8 float * 4 bayt = 2^34
  CHECK(!loads(b));
}

ENGINE_TEST(clod_rejects_null_and_zero_size) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "clod"));
  ClodMesh m;
  CHECK(!clod_load(sys, nullptr, 1024, m));
  Blob b = make_valid();
  CHECK(!clod_load(sys, b.bytes, 0, m));
  // Basarisizlikta cikti SIFIRLANMIS olmali: cagiran yari dolu bir yapiyla
  // devam etmesin.
  CHECK(m.clusters == nullptr);
  CHECK(m.cluster_count == 0);
  CHECK(m.vertices == nullptr);
}
