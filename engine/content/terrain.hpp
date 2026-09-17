// L6 CONTENT — Prosedurel terrain: yukseklik haritasi uretimi + orneklemesi
// (500 madde listesi #107 "Heightmap Terrain") ve yukseklik-bantli doku
// karistirma agirliklari (#108 "Splatting"). core/math/noise.hpp'nin
// (fbm_2d) DOGRUDAN tuketicisi -- PCG zincirinin (#438) UCUNCU tasi.
//
// content/ katmani renderer/'a BAGIMLI DEGIL (PLAN.md katman kurali): cikti
// duz float dizisi, cagiran mesh'e (heightfield ucgenlemesi) veya doku
// verisine kendi cevirir.
#pragma once
#include <cstdint>

namespace tulpar::engine::content {

struct HeightmapConfig {
  uint32_t width = 64, height = 64; // grid tarafi (dunya boyutu = (N-1)*cell_size)
  float cell_size = 1.0f;
  uint32_t seed = 0;
  int octaves = 5;
  float lacunarity = 2.0f;
  float gain = 0.5f;
  float frequency = 0.02f; // gurultu ornekleme frekansi (dunya birimi basina)
  float amplitude = 20.0f; // metre cinsinden MAKSIMUM yukseklik (cikti HER ZAMAN [0,amplitude) araliginda)
};

// out_heights: cagiranin ONCEDEN ayirdigi width*height boyutunda dizi (satir-major, z*width+x).
void generate_heightmap(const HeightmapConfig &cfg, float *out_heights);

// Dunya (world_x,world_z) konumunda BILINEAR enterpole yukseklik (deger-
// gurultusunun smoothstep'i DEGIL, DUZ dogrusal -- heightfield MESH'inin
// ucgenleri arasinda GERCEKTE nasil enterpole edildigiyle EŞLEŞIR). Sinir
// disi konumlar en yakin kenara KILITLENIR (ekstrapolasyon yok).
float sample_height(const float *heights, uint32_t width, uint32_t height, float cell_size, float world_x,
                     float world_z);

// Yukseklik-bantli doku katmani: [height_min,height_max] araliginda TAM
// agirlik (1), disina dogru `blend` genisliginde YUMUSAK gecis (0'a iner).
struct SplatLayer {
  float height_min, height_max;
  float blend = 1.0f;
};

// out_weights: layer_count boyutunda cikti, TOPLAMI 1'e normalize edilir
// (en az bir katman etkiliyse). HICBIR katman etkin degilse (sum=0) TUMU 0
// doner -- cagiran bir "varsayilan/yedek katman" tutmali (ör. son katmani
// her zaman biraz etkin birakmak).
void compute_splat_weights(float height, const SplatLayer *layers, uint32_t layer_count, float *out_weights);

} // namespace tulpar::engine::content
