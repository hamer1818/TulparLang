#include "core/math/spatial_hash.hpp"

#include <cmath>

namespace tulpar::engine {

namespace {
// Teschner ve ark. 2003'teki asal sabitler. Carpimlar ISARETSIZ: isaretli
// tamsayi tasmasi C++'ta TANIMSIZ DAVRANIS, isaretsiz tasma ise TANIMLI
// (mod 2^32) -- yani negatif hucre koordinatlarinda bile sonuc her
// platformda AYNI.
constexpr uint32_t kPx = 73856093u;
constexpr uint32_t kPy = 19349663u;
constexpr uint32_t kPz = 83492791u;

uint32_t hash_cell(int32_t x, int32_t y, int32_t z, uint32_t bucket_count) {
  const uint32_t h = ((uint32_t)x * kPx) ^ ((uint32_t)y * kPy) ^ ((uint32_t)z * kPz);
  return h & (bucket_count - 1); // bucket_count IKININ KUVVETI (build dogrular)
}

bool is_power_of_two(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }
} // namespace

int32_t spatial_cell_coord(float v, float cell_size) {
  // std::floor: IEEE-754 TAM olarak tanimlidir (yuvarlama belirsizligi yok),
  // bu yuzden determinizm icin guvenli. Kesme (truncation) KULLANILAMAZ:
  // -0.5 ve +0.5 ayni hucreye (0) duserdi.
  return (int32_t)std::floor(v / cell_size);
}

bool spatial_hash_build(SpatialHash &grid, const Vec3 *positions, uint32_t count) {
  if (!grid.cell_start || grid.cell_size <= 0.0f) return false;
  if (!is_power_of_two(grid.bucket_count)) return false;
  if (count > 0 && (!positions || !grid.items)) return false;

  grid.item_count = count;
  const uint32_t nb = grid.bucket_count;

  // (1) Kova sayaclarini sifirla.
  for (uint32_t b = 0; b <= nb; b++) grid.cell_start[b] = 0;
  if (count == 0) return true;

  // (2) Her kovaya kac oge dustugunu say.
  for (uint32_t i = 0; i < count; i++) {
    const int32_t cx = spatial_cell_coord(positions[i].x, grid.cell_size);
    const int32_t cy = spatial_cell_coord(positions[i].y, grid.cell_size);
    const int32_t cz = spatial_cell_coord(positions[i].z, grid.cell_size);
    grid.cell_start[hash_cell(cx, cy, cz, nb)]++;
  }

  // (3) YERINDE dislayici (exclusive) onek toplami: cell_start[b] artik
  // kovanin BASLANGIC indeksi.
  uint32_t running = 0;
  for (uint32_t b = 0; b < nb; b++) {
    const uint32_t c = grid.cell_start[b];
    grid.cell_start[b] = running;
    running += c;
  }
  grid.cell_start[nb] = running; // == count

  // (4) Dagit. cell_start[b]'yi YAZMA IMLECI olarak kullaniyoruz; bu yuzden
  // dongu bitince cell_start[b], kovanin BITIS indeksine (yani b+1'in
  // baslangicina) esitlenmis olur -- adim (5) bunu geri kaydirir.
  // Ekstra imlec dizisine GEREK YOK (A2: ayirma yok).
  for (uint32_t i = 0; i < count; i++) {
    const int32_t cx = spatial_cell_coord(positions[i].x, grid.cell_size);
    const int32_t cy = spatial_cell_coord(positions[i].y, grid.cell_size);
    const int32_t cz = spatial_cell_coord(positions[i].z, grid.cell_size);
    grid.items[grid.cell_start[hash_cell(cx, cy, cz, nb)]++] = i;
  }

  // (5) Bir saga kaydir: cell_start[b] yeniden BASLANGIC indeksi olur.
  for (uint32_t b = nb; b > 0; b--) grid.cell_start[b] = grid.cell_start[b - 1];
  grid.cell_start[0] = 0;
  return true;
}

uint32_t spatial_hash_query(const SpatialHash &grid, const Vec3 *positions, Vec3 center, float radius,
                             uint32_t *out_indices, uint32_t max_out) {
  if (!grid.cell_start || grid.item_count == 0 || radius < 0.0f) return 0;
  if (!positions) return 0;

  const float cs = grid.cell_size;
  const float r2 = radius * radius;
  const int32_t x0 = spatial_cell_coord(center.x - radius, cs);
  const int32_t x1 = spatial_cell_coord(center.x + radius, cs);
  const int32_t y0 = spatial_cell_coord(center.y - radius, cs);
  const int32_t y1 = spatial_cell_coord(center.y + radius, cs);
  const int32_t z0 = spatial_cell_coord(center.z - radius, cs);
  const int32_t z1 = spatial_cell_coord(center.z + radius, cs);

  uint32_t found = 0;
  // Tarama sirasi z -> y -> x, hucre icinde ekleme sirasi: SONUC SIRASI
  // her zaman AYNI (determinizm).
  for (int32_t cz = z0; cz <= z1; cz++) {
    for (int32_t cy = y0; cy <= y1; cy++) {
      for (int32_t cx = x0; cx <= x1; cx++) {
        const uint32_t b = hash_cell(cx, cy, cz, grid.bucket_count);
        const uint32_t begin = grid.cell_start[b], end = grid.cell_start[b + 1];
        for (uint32_t k = begin; k < end; k++) {
          const uint32_t item = grid.items[k];
          const Vec3 p = positions[item];

          // HASH CAKISMASI KONTROLU: farkli hucreler ayni kovaya duşebilir.
          // Ogenin GERCEKTEN (cx,cy,cz) hucresinde olup olmadigini dogrula.
          // Bu olmadan (a) yanlis adaylar mesafe testine girer (zararsiz,
          // yalniz yavas) ve (b) sorgu araligindaki IKI farkli hucre ayni
          // kovaya duserse AYNI OGE IKI KEZ donerdi -- asil hata budur.
          if (spatial_cell_coord(p.x, cs) != cx) continue;
          if (spatial_cell_coord(p.y, cs) != cy) continue;
          if (spatial_cell_coord(p.z, cs) != cz) continue;

          const Vec3 d = p - center;
          if (dot(d, d) > r2) continue;

          if (found < max_out && out_indices) out_indices[found] = item;
          found++; // tampon dolsa da SAYMAYA devam -> cagiran tasmayi gorur
        }
      }
    }
  }
  return found;
}

} // namespace tulpar::engine
