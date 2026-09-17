// L4 SIMULATION — Dalga Fonksiyonu Cokusu (Wave Function Collapse, WFC)
// ile prosedurel icerik uretimi.
//
// **Isim hakkinda durustce:** WFC'nin "kuantum" cagrisimi bir METAFORDUR --
// Maxim Gumin algoritmayi, her hucrenin basta TUM olasiliklarin "ust
// ustelenmesi" (superposition) olmasi ve gozlem/kisitlarla TEK bir degere
// "cokmesi" benzetmesinden dolayi boyle adlandirdi. Icinde kuantum fizigi
// YOKTUR; klasik bir KISIT YAYILIMI (constraint propagation) algoritmasidir.
//
// **Kaldirilan bagimliliklar ve NEDENI:**
//   - qpp (Quantum++): onceki kod bir Pauli-X kapisi uygulayip SONUCU
//     ATIYORDU -- uretilen icerige hicbir etkisi yoktu. Eigen tabanli agir
//     bir sablon kutuphanesini derleme suresine/ikili boyuta eklemenin
//     karsiligi SIFIRDI.
//   - FastNoiseSIMD: calisma zamaninda CPU'ya gore SIMD yolu secer
//     (AVX2/SSE/NEON); ayni tohum farkli cihazda FARKLI gurultu verebilir.
//     Motorun "3 platform bit-es" iddiasi (VIZYON.md SS1) prosedurel
//     uretimi de kapsar -- ayni tohum HER cihazda AYNI dunyayi vermeli.
//     Yerine core/math/noise.hpp (tam sayi hash tabanli, bit-es) kullanilir.
//
// Kapsam: "basit doseli model" (simple tiled model) -- komsuluk kurallari
// dogrudan verilir (ornek bitmapten kural CIKARIMI daha buyuk bir is,
// sonraki adim). En fazla 32 dose (kMaxTiles) -- olasilik kumesi tek bir
// uint32 bit maskesinde tutulur, bu da yayilimi cok hizli yapar.
#pragma once
#include <cstdint>

#include "core/math/random.hpp"

namespace tulpar::engine::sim {

constexpr uint32_t kWfcMaxTiles = 32;

// Yonler: komsuluk kurallari YON BASINA verilir (bir dose saginda X'e izin
// verirken ustunde vermeyebilir).
enum class WfcDir : uint8_t { kRight = 0, kLeft = 1, kDown = 2, kUp = 3 };

struct WfcRules {
  uint32_t tile_count = 0;
  // allowed[dir][tile]: `tile`in `dir` yonundeki komsusu olabilecek
  // doselerin BIT MASKESI. Simetri CAGIRANIN sorumlulugunda (A'nin saginda
  // B olabiliyorsa, B'nin solunda A olabilmeli) -- build_flow_field gibi
  // burada da kural dogrulamasi yapilir: check_rules_symmetric().
  uint32_t allowed[4][kWfcMaxTiles] = {};
};

// Kurallarin simetrik olup olmadigini dogrular (yanlis kural seti
// cozulemez izgaralara yol acar; hatayi URETIM sirasinda degil BURADA yakala).
bool wfc_rules_symmetric(const WfcRules &rules);

struct WfcGrid {
  uint32_t width = 0, height = 0;
  // cells[w*h]: her hucrenin OLASILIK MASKESI. Cozum sonrasi her hucrede
  // TEK bit kalir. Cagiran ayirir.
  uint32_t *cells = nullptr;
  // Yayilim calisma yigini (cagiran ayirir, width*height eleman).
  uint32_t *stack = nullptr;

  uint32_t index(uint32_t x, uint32_t y) const { return y * width + x; }
};

struct WfcResult {
  bool solved = false;        // tum hucreler TEK doseye cokti
  bool contradiction = false; // bir hucrenin TUM olasiliklari tukendi
  uint32_t collapsed = 0;     // kac hucre cokturuldu
};

// Izgarayi cozer. rng: deterministik secim kaynagi (ayni tohum -> AYNI
// dunya, HER platformda). Cozum BASARISIZSA (celiski) cells[] kismen
// cokmus halde birakilir -- cagiran isterse farkli tohumla yeniden dener
// (standart WFC pratigi).
WfcResult wfc_solve(WfcGrid &grid, const WfcRules &rules, Rng &rng);

// Tek bir hucrenin cokmus dose indeksini verir (cokmemisse UINT32_MAX).
uint32_t wfc_tile_at(const WfcGrid &grid, uint32_t x, uint32_t y);

} // namespace tulpar::engine::sim
