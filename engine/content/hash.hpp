// L6 CONTENT — Icerik boru hattinin tek ozet fonksiyonu: FNV-1a 64.
// Blob (.sahneb), pack (.tpak) ve ice aktarma onbellegi ayni ozeti kullanir:
// belirlenimli (ayni bayt -> ayni sayi, her platformda), tablosuz, akisli
// (parca parca beslenebilir). Kriptografik DEGIL — amaci bozulma yakalamak
// ve girdi kimligi uretmek, imza degil.
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::content {

constexpr uint64_t kFnvSeed = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;

inline uint64_t content_fnv1a(const void *data, size_t n, uint64_t h = kFnvSeed) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= kFnvPrime; }
  return h;
}
// NUL sonlu metin (sonlandirici ozete girmez).
inline uint64_t content_fnv1a_str(const char *s, uint64_t h = kFnvSeed) {
  for (; s && *s; s++) { h ^= (uint8_t)*s; h *= kFnvPrime; }
  return h;
}

} // namespace tulpar::engine::content
