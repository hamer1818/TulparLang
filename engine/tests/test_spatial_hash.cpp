// core/math/spatial_hash.hpp: yaricap sorgusunun DOGRU kumeyi, DOGRU
// SIRADA ve TEKRARSIZ dondurdugunu kanitlar. Ozellikle iki tuzak test edilir:
//   (a) negatif koordinatta hucre hesabi (kesme degil ASAGI yuvarlama),
//   (b) hash cakismasi -> ayni ogenin IKI KEZ donmemesi.
#include "core/math/spatial_hash.hpp"

#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(spatial_cell_coord_rounds_down_not_toward_zero) {
  // TUZAK: kesme kullanilsaydi -0.5 ve +0.5 AYNI hucreye (0) duserdi ve
  // orijinin iki yanindaki nesneler yanlis komsu sayilirdi.
  CHECK(spatial_cell_coord(0.5f, 1.0f) == 0);
  CHECK(spatial_cell_coord(-0.5f, 1.0f) == -1);
  CHECK(spatial_cell_coord(-1.5f, 1.0f) == -2);
  CHECK(spatial_cell_coord(1.0f, 1.0f) == 1);   // sinir: yukari hucreye ait
  CHECK(spatial_cell_coord(-1.0f, 1.0f) == -1); // sinir: kendi hucresi
  // Farkli hucre boyutu
  CHECK(spatial_cell_coord(5.0f, 2.0f) == 2);
  CHECK(spatial_cell_coord(-5.0f, 2.0f) == -3);
}

ENGINE_TEST(spatial_hash_build_produces_valid_bucket_offsets) {
  constexpr uint32_t kBuckets = 16;
  const Vec3 pos[3] = {{0.5f, 0.5f, 0.5f}, {1.5f, 0.5f, 0.5f}, {10.5f, 0.5f, 0.5f}};
  uint32_t cell_start[kBuckets + 1];
  uint32_t items[3];

  SpatialHash g;
  g.cell_size = 1.0f;
  g.bucket_count = kBuckets;
  g.cell_start = cell_start;
  g.items = items;
  CHECK(spatial_hash_build(g, pos, 3));

  // DEGISMEZLER: ofsetler azalmayan olmali ve sonuncusu oge sayisina esit.
  CHECK(cell_start[0] == 0);
  CHECK(cell_start[kBuckets] == 3);
  for (uint32_t b = 0; b < kBuckets; b++) CHECK(cell_start[b] <= cell_start[b + 1]);

  // Her oge TAM BIR KEZ yerlestirilmis olmali.
  bool seen[3] = {false, false, false};
  for (uint32_t k = 0; k < 3; k++) {
    CHECK(items[k] < 3);
    CHECK(!seen[items[k]]); // IKI KEZ yerlestirme YOK
    seen[items[k]] = true;
  }
  CHECK(seen[0] && seen[1] && seen[2]);
}

ENGINE_TEST(spatial_hash_query_returns_exactly_the_items_in_radius) {
  constexpr uint32_t kBuckets = 16;
  // Hucre boyutu 1. Ogeler: hucre (0,0,0), (1,0,0) ve cok uzakta (10,0,0).
  const Vec3 pos[3] = {{0.5f, 0.5f, 0.5f}, {1.5f, 0.5f, 0.5f}, {10.5f, 0.5f, 0.5f}};
  uint32_t cell_start[kBuckets + 1];
  uint32_t items[3];
  SpatialHash g;
  g.cell_size = 1.0f;
  g.bucket_count = kBuckets;
  g.cell_start = cell_start;
  g.items = items;
  CHECK(spatial_hash_build(g, pos, 3));

  uint32_t out[8];

  // r=0.9: yalniz oge 0 (oge 1 TAM 1.0 uzakta -> DISARIDA).
  uint32_t n = spatial_hash_query(g, pos, Vec3{0.5f, 0.5f, 0.5f}, 0.9f, out, 8);
  CHECK(n == 1);
  CHECK(out[0] == 0);

  // r=1.5: oge 0 ve 1. SIRA da belirleyici (x dongusu artan) -> 0, sonra 1.
  n = spatial_hash_query(g, pos, Vec3{0.5f, 0.5f, 0.5f}, 1.5f, out, 8);
  CHECK(n == 2);
  CHECK(out[0] == 0);
  CHECK(out[1] == 1);

  // Uzaktaki oge hicbir zaman girmemeli (hucresi arama araliginda degil).
  n = spatial_hash_query(g, pos, Vec3{0.5f, 0.5f, 0.5f}, 3.0f, out, 8);
  CHECK(n == 2);

  // Merkezi uzaktaki ogeye tasi -> yalniz o gelsin.
  n = spatial_hash_query(g, pos, Vec3{10.5f, 0.5f, 0.5f}, 0.5f, out, 8);
  CHECK(n == 1);
  CHECK(out[0] == 2);

  // Hicbir sey yoksa 0.
  n = spatial_hash_query(g, pos, Vec3{100.0f, 100.0f, 100.0f}, 1.0f, out, 8);
  CHECK(n == 0);
}

ENGINE_TEST(spatial_hash_no_duplicates_across_many_cells) {
  // ASIL TUZAK: sorgu araligi COK sayida hucre kapsiyor ve kova sayisi
  // KUCUK -> hash cakismasi KESIN. Cakisma dogrulamasi olmasaydi ayni oge
  // birden fazla kez donerdi.
  constexpr uint32_t kBuckets = 4; // bilerek COK kucuk -> cakisma garantili
  constexpr uint32_t kN = 27;
  Vec3 pos[kN];
  uint32_t idx = 0;
  for (int z = 0; z < 3; z++)
    for (int y = 0; y < 3; y++)
      for (int x = 0; x < 3; x++) pos[idx++] = Vec3{(float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f};

  uint32_t cell_start[kBuckets + 1];
  uint32_t items[kN];
  SpatialHash g;
  g.cell_size = 1.0f;
  g.bucket_count = kBuckets;
  g.cell_start = cell_start;
  g.items = items;
  CHECK(spatial_hash_build(g, pos, kN));

  // Tum kupu kapsayan sorgu: 27 ogenin TAMAMI, HER BIRI BIR KEZ.
  uint32_t out[64];
  const uint32_t n = spatial_hash_query(g, pos, Vec3{1.5f, 1.5f, 1.5f}, 10.0f, out, 64);
  CHECK(n == kN);

  bool seen[kN] = {};
  for (uint32_t i = 0; i < n; i++) {
    CHECK(out[i] < kN);
    CHECK(!seen[out[i]]); // TEKRAR YOK -- cakisma dogrulamasinin kaniti
    seen[out[i]] = true;
  }
}

ENGINE_TEST(spatial_hash_reports_overflow_instead_of_silently_truncating) {
  constexpr uint32_t kBuckets = 8;
  const Vec3 pos[4] = {{0.1f, 0, 0}, {0.2f, 0, 0}, {0.3f, 0, 0}, {0.4f, 0, 0}};
  uint32_t cell_start[kBuckets + 1];
  uint32_t items[4];
  SpatialHash g;
  g.cell_size = 1.0f;
  g.bucket_count = kBuckets;
  g.cell_start = cell_start;
  g.items = items;
  CHECK(spatial_hash_build(g, pos, 4));

  uint32_t out[2];
  // Dordu de yaricapta ama tampon 2 kisilik: donus GERCEK sayiyi (4)
  // vermeli ki cagiran tasmayi FARK ETSIN.
  const uint32_t n = spatial_hash_query(g, pos, Vec3{0.25f, 0, 0}, 1.0f, out, 2);
  CHECK(n == 4);
  CHECK(n > 2); // cagiranin tasmayi tespit etme yolu
}

ENGINE_TEST(spatial_hash_rejects_invalid_configuration) {
  uint32_t cell_start[9];
  uint32_t items[4];
  const Vec3 pos[1] = {{0, 0, 0}};

  SpatialHash g;
  g.cell_size = 1.0f;
  g.cell_start = cell_start;
  g.items = items;

  g.bucket_count = 7; // IKININ KUVVETI DEGIL
  CHECK(!spatial_hash_build(g, pos, 1));

  g.bucket_count = 8;
  g.cell_size = 0.0f; // gecersiz hucre boyutu
  CHECK(!spatial_hash_build(g, pos, 1));

  g.cell_size = 1.0f;
  g.cell_start = nullptr;
  CHECK(!spatial_hash_build(g, pos, 1));

  // Bos izgara: gecerli, sorgu 0 doner.
  g.cell_start = cell_start;
  CHECK(spatial_hash_build(g, nullptr, 0));
  CHECK(cell_start[8] == 0);
  uint32_t out[4];
  CHECK(spatial_hash_query(g, pos, Vec3{0, 0, 0}, 1.0f, out, 4) == 0);
}
