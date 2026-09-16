// L1 CORE — Sabit noktali (fixed-point) deterministik aritmetik: Q16.16.
//
// **NEDEN VAR:** motorun bir numarali yapisal iddiasi "ayni girdi, HER
// platformda bit-es ayni sonuc" (VIZYON.md SS1). Bu iddia bugun kayan
// noktayla ve DISIPLINLE ayakta duruyor: strict FP, -ffp-contract=off,
// libm'den kacinma, altin ozet karsilastirmasi. Disiplin ise yariyor ama
// KIRILGAN -- bir derleyici surumu, bir -ffast-math, bir std::sin cagrisi
// iddiayi SESSIZCE bozar ve desenkronun sebebini bulmak gunler surer.
//
// Bu tip ayni iddiayi DISIPLINDEN YAPIYA tasir: `a*b` burada bir TAMSAYI
// carpimidir. x86'da, ARM'da, hangi derleyici/bayrak olursa olsun AYNI
// BITTIR. Determinizm artik "dikkat edilmesi gereken bir sey" degil, TIPIN
// KENDISIDIR.
//
// **Kaynaklar (varligi/lisansi dogrulandi):** yaklasim MikeLankamp/fpm
// (MIT, 840*) ve mas-bandwidth/fixed (MIT, Q48.16) ile ayni klasik Q-format
// aritmetigi. mas-bandwidth/fixed'in test felsefesi bu motorunkiyle BIREBIR
// ortusuyor: her sonucu hash'leyip kaynaga dondurmak, tek bit degisirse
// testin her platformda dusmesi -- bizim "altin ozet" disiplinimiz.
// Kod KOPYALANMADI; format ve algoritmalar (Q-carpim, bit-bit tamsayi
// karekoku) kamuya mal olmus standart tekniklerdir.
//
// **Format secimi -- Q16.16 (int32_t):**
//   aralik   : +-32768
//   hassasiyet: 1/65536 = ~1.5e-5  (yaklasik 15 mikrometre)
//   boyut    : 4 bayt -- float ile AYNI
//   carpim   : int64 ara deger yeter (32x32 -> en fazla 64 bit), TASMA YOK
//   bolme    : (raw << 16) en fazla 2^47, int64'e sigar, TASMA YOK
// Yani +-32 km'lik bir dunyada 15 mikrometre hassasiyet, float ile ayni
// bellekte. Daha genis dunya gerekirse Q32.32 (int64 + 128-bit ara deger)
// SONRAKI adim -- o zaman tasinabilirlik icin elle 64x64 carpim gerekir.
//
// **KULLANIM KURALI:** from_float()/to_float() YALNIZCA yukleme/yazarlik
// ve GORUNTULEME sinirinda cagrilir. Simulasyon dongusunun ICINDE float'a
// donmek, tipin varlik sebebini ortadan kaldirir.
#pragma once
#include <cstdint>

namespace tulpar::engine {

struct Fixed {
  static constexpr int32_t kFracBits = 16;
  static constexpr int32_t kOne = 1 << kFracBits; // 65536
  int32_t raw = 0;

  // Ham degerden DOGRUDAN kurulum (serilestirme/test icin). Cagiran ham
  // degerin Q16.16 oldugunu BILIR.
  static constexpr Fixed from_raw(int32_t r) { return Fixed{r}; }
  static constexpr Fixed from_int(int32_t v) { return Fixed{v * kOne}; }

  // SINIR fonksiyonlari -- simulasyon dongusunde KULLANILMAZ (bkz. baslik).
  // Yuvarlama: sifira dogru KESME (C++ tamsayi donusumunun kendi kurali),
  // boylece ayni float girdisi her platformda ayni ham degeri verir.
  static Fixed from_float(float v) { return Fixed{(int32_t)(v * (float)kOne)}; }
  float to_float() const { return (float)raw / (float)kOne; }

  // Tam sayi kismi: ASAGI yuvarlar (negatifte de -- aritmetik kaydirma,
  // C++'ta isaretli saga kaydirma uygulama tanimliydi ama C++20'den beri
  // aritmetiktir ve tum hedef derleyicilerimizde zaten oyleydi).
  int32_t floor_int() const { return raw >> kFracBits; }
};

constexpr Fixed operator+(Fixed a, Fixed b) { return Fixed{a.raw + b.raw}; }
constexpr Fixed operator-(Fixed a, Fixed b) { return Fixed{a.raw - b.raw}; }
constexpr Fixed operator-(Fixed a) { return Fixed{-a.raw}; }

// Q16.16 * Q16.16 = Q32.32 -> 16 bit saga kaydirarak Q16.16'ya don.
// int64 ara deger: 32x32 carpim en fazla 64 bit, TASMA IMKANSIZ.
inline Fixed operator*(Fixed a, Fixed b) {
  return Fixed{(int32_t)(((int64_t)a.raw * (int64_t)b.raw) >> Fixed::kFracBits)};
}

// Bolme: bolunen ONCE 16 bit sola kaydirilir (aksi halde kesir kaybolur).
// (2^31 << 16) = 2^47 -- int64'e rahat sigar. b.raw == 0 ise 0 doner
// (sessiz NaN/tanimsiz davranis YOK; cagiran sifir bolen vermemelidir).
inline Fixed operator/(Fixed a, Fixed b) {
  if (b.raw == 0) return Fixed{0};
  return Fixed{(int32_t)((((int64_t)a.raw) << Fixed::kFracBits) / (int64_t)b.raw)};
}

inline Fixed &operator+=(Fixed &a, Fixed b) { a = a + b; return a; }
inline Fixed &operator-=(Fixed &a, Fixed b) { a = a - b; return a; }
inline Fixed &operator*=(Fixed &a, Fixed b) { a = a * b; return a; }

constexpr bool operator==(Fixed a, Fixed b) { return a.raw == b.raw; }
constexpr bool operator!=(Fixed a, Fixed b) { return a.raw != b.raw; }
constexpr bool operator<(Fixed a, Fixed b) { return a.raw < b.raw; }
constexpr bool operator<=(Fixed a, Fixed b) { return a.raw <= b.raw; }
constexpr bool operator>(Fixed a, Fixed b) { return a.raw > b.raw; }
constexpr bool operator>=(Fixed a, Fixed b) { return a.raw >= b.raw; }

constexpr Fixed fixed_abs(Fixed a) { return a.raw < 0 ? Fixed{-a.raw} : a; }
constexpr Fixed fixed_min(Fixed a, Fixed b) { return a.raw < b.raw ? a : b; }
constexpr Fixed fixed_max(Fixed a, Fixed b) { return a.raw > b.raw ? a : b; }

// Bit-bit tamsayi karekoku (klasik, std::sqrt'siz -> libm YOK -> platformlar
// arasi AYNI). Kayan nokta HIC kullanilmaz, yalniz toplama/cikarma/kaydirma.
inline uint64_t isqrt64(uint64_t n) {
  uint64_t res = 0;
  uint64_t bit = 1ull << 62; // 2'nin EN BUYUK CIFT kuvveti
  while (bit > n) bit >>= 2;
  while (bit != 0) {
    if (n >= res + bit) {
      n -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return res;
}

// sqrt(v) icin: sonuc.raw = isqrt(v.raw << 16).
// Turetim: v = raw/2^16, sqrt(v) = sqrt(raw)/2^8; bunu Q16.16'ya cevirmek
// icin 2^16 ile carpariz -> sqrt(raw)*2^8 = sqrt(raw * 2^16).
// Negatif giris: 0 doner (tanimsiz davranis YOK).
inline Fixed fixed_sqrt(Fixed v) {
  if (v.raw <= 0) return Fixed{0};
  return Fixed{(int32_t)isqrt64(((uint64_t)(uint32_t)v.raw) << Fixed::kFracBits)};
}

// --- 3B vektor (ayni determinizm garantisiyle) --------------------------
struct FixedVec3 {
  Fixed x, y, z;
};

inline FixedVec3 operator+(FixedVec3 a, FixedVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline FixedVec3 operator-(FixedVec3 a, FixedVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline FixedVec3 operator-(FixedVec3 a) { return {-a.x, -a.y, -a.z}; }
inline FixedVec3 operator*(FixedVec3 a, Fixed s) { return {a.x * s, a.y * s, a.z * s}; }
inline FixedVec3 &operator+=(FixedVec3 &a, FixedVec3 b) { a = a + b; return a; }

inline Fixed dot(FixedVec3 a, FixedVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// UZUNLUK KARESI: karekok YOK -> hicbir hassasiyet kaybi yok. Karsilastirma
// yapan her yerde (menzil testi, en yakini bulma) BUNU tercih edin.
inline Fixed length_sq(FixedVec3 a) { return dot(a, a); }
inline Fixed length(FixedVec3 a) { return fixed_sqrt(length_sq(a)); }

constexpr bool operator==(FixedVec3 a, FixedVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

} // namespace tulpar::engine
