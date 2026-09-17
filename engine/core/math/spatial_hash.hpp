// L1 CORE — Uzamsal hash izgarasi: "su noktanin R yaricapindaki her sey"
// sorgusunu HIZLI cevaplar.
//
// **core/math/bvh.hpp ile FARKI (ikisi de "uzamsal yapi" -- hangisi ne zaman):**
// BVH bir AGACTIR ve STATIK/yari-statik geometri icindir: insasi pahali
// (SAH ile siralama), sorgusu cok hizli, ray/isin sorgularinda ustundur.
// Bu izgara ise HER KARE YENIDEN INSA EDILMEK uzere tasarlandi: insasi
// dogrusal (O(n), sayma-siralamasi), YARICAP sorgularinda ustundur.
// Kural: sahne geometrisi -> BVH. Hareket eden binlerce ajan/parcacik ->
// bu izgara.
//
// **Neden bu dosya oncelikli:** su an motorda DORT ayri yer ayni soruyu
// soruyor ve hicbirinin cevabi yok --
//   1) sim/fluid_system.cpp (SPH) komsu aramayi O(n^2) kaba kuvvetle yapiyor
//   2) suru davranisi (boids) komsu listesi ister
//   3) tetikleyici hacimler "bu alandaki varliklar" ister
//   4) ekosistem/AI yakinlik sorgulari
// Tek bir ilkel yapi, dort ozelligin onkosulu.
//
// **Kaynak:** Teschner ve ark. 2003, "Optimized Spatial Hashing for Collision
// Detection of Deformable Objects" -- hash sabitleri (73856093, 19349663,
// 83492791) o makaleden; SPH/carpisma literaturunun standart referansi.
// Yerlestirme SAYMA-SIRALAMASI (counting sort) ile yapilir: hucre basina
// dinamik liste YOK, iki duz dizi var -- ayirma yok (A2), onbellek dostu.
//
// **Determinizm:** tum hash aritmetigi ISARETSIZ tamsayi (tanimli tasma),
// sorgu sonucu HER ZAMAN ayni sirada doner (hucre taramasi z->y->x, hucre
// icinde ekleme sirasi). Ayni girdi -> ayni cikti, her platformda.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine {

struct SpatialHash {
  float cell_size = 1.0f;
  // IKININ KUVVETI olmali (sorgu `& (bucket_count-1)` kullanir). build()
  // degilse false doner.
  uint32_t bucket_count = 0;
  uint32_t item_count = 0;

  // Cagiranin ayirdigi iki dizi (bu dosya AYIRMA YAPMAZ):
  uint32_t *cell_start = nullptr; // [bucket_count + 1] -- kova b: items[cell_start[b] .. cell_start[b+1])
  uint32_t *items = nullptr;      // [item_count] -- oge indeksleri, kovalara gore gruplanmis
};

// Bir dunya koordinatinin hangi hucreye dustugu. Negatifte de DOGRU calisir
// (kesme degil ASAGI yuvarlama -- aksi halde 0 civarinda iki koordinat ayni
// hucreye duserdi).
int32_t spatial_cell_coord(float v, float cell_size);

// positions[count]: KOPYALANMAZ; sorgu sirasinda da AYNI dizi verilmelidir.
// false: gecersiz parametre (dizi yok, bucket_count ikinin kuvveti degil,
// cell_size <= 0).
bool spatial_hash_build(SpatialHash &grid, const Vec3 *positions, uint32_t count);

// center'in radius yaricapindaki ogelerin indekslerini out_indices'e yazar.
// DONUS: GERCEK eslesme sayisi -- bu deger max_out'tan BUYUKSE tampon
// yetmemistir (ilk max_out tanesi yazildi, gerisi SESSIZCE KAYBOLMADI,
// cagiran farki gorur).
uint32_t spatial_hash_query(const SpatialHash &grid, const Vec3 *positions, Vec3 center, float radius,
                             uint32_t *out_indices, uint32_t max_out);

} // namespace tulpar::engine
