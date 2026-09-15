// L1 CORE — Deterministik PRNG (500 madde listesi #438 "PCG (Prosedurel
// Icerik)"nin temel tasi). xorshift32: yalniz tam sayi bit operasyonlari
// (kayma+xor) -- kayan nokta/libm YOK, bu yuzden motorun "3 platform bit-es"
// determinizm garantisiyle (VIZYON.md SS1) AYNI sinifta: ayni tohum, HER
// platformda (x86_64/ARM/vb.) TAM AYNI bit dizisini uretir. Prosedurel
// icerik (terrain/dagitim/varyasyon) bu garantiyi KAYBETMEDEN uretilebilir.
//
// Bilincli olarak dar kapsam: TEMEL uretec + birkac dogrudan yardimci
// (float/araluk/karistir). Deger/Perlin gurultusu gibi daha ileri PCG
// araclari SONRAKI adim -- bu, onlarin da uzerine kurulacagi temel tastir.
//
// xorshift'in BILINEN tek zayifligi: tohum tam SIFIR ise sonsuza kadar sifir
// uretir -- constructor bunu SABIT bir sifir-olmayan degere kaydirarak onler.
#pragma once
#include <cstdint>

namespace tulpar::engine {

class Rng {
 public:
  explicit Rng(uint32_t seed) : state_(seed != 0 ? seed : 0x9E3779B9u) {}

  uint32_t next_u32() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
  }

  // [0,1) araliginda kayan nokta. Ust 24 bit kullanilir (float mantissa
  // hassasiyetiyle eslesir), 1/2^24 ile olceklenir (tam ikiye-bolen, hassasiyet
  // kaybi yok).
  float next_float() { return (float)(next_u32() >> 8) * (1.0f / 16777216.0f); }

  // [lo, hi) tam sayi araligi (hi>lo varsayilir, cagiran kontrol eder).
  // NOT: modulo yanliligi var (hi-lo, 2^32'yi tam bolmuyorsa kucuk bir egilim
  // olusur) -- ilk dilim icin kabul edilebilir, Lemire'in yansiz yontemi
  // SONRAKI optimizasyon (BOSLUK-TARAMASI.md tarzi bilincli erteleme).
  int32_t next_range(int32_t lo, int32_t hi) {
    const uint32_t span = (uint32_t)(hi - lo);
    return lo + (int32_t)(next_u32() % span);
  }

  uint32_t state() const { return state_; } // testler/hata ayiklama icin

 private:
  uint32_t state_;
};

// Fisher-Yates: n elemanli diziyi YERINDE, yansiz (rng yansizsa) karistirir.
// i=n'den 2'ye iner, her adimda arr[i-1]'i arr[0..i-1] arasindan rastgele
// secilen biriyle takas eder -- standart, kanitlanmis algoritma.
template <class T>
void shuffle(T *arr, uint32_t n, Rng &rng) {
  for (uint32_t i = n; i > 1; i--) {
    const uint32_t j = (uint32_t)rng.next_range(0, (int32_t)i);
    T tmp = arr[i - 1];
    arr[i - 1] = arr[j];
    arr[j] = tmp;
  }
}

} // namespace tulpar::engine
