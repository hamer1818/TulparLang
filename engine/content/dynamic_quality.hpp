// L6 CONTENT — Kare-suresi guduml, histerezisli kalite olcekleyici (420
// madde listesi #180: "Dynamic Quality Scaling"). `device_tier.hpp`'nin
// DeviceTier/TierProfile'ini RUNTIME'da ayarlar: baslangic tier'i yukleme
// aninda device_tier_classify() ile secilir (VIZYON.md S2.5 "cihaz-sinifi
// bake" bahsi), bu siniftaki QualityScaler ise CALISMA ZAMANINDA olculen
// gercek kare suresine gore ayni tier'i asagi/yukari kaydirir.
//
// Histerezis KASITLI asimetrik: kotu kareye HIZLI tepki (akiciligi geri
// kazanmak onemli), iyi kareye YAVAS tepki (art arda uzun sure rahatlik
// gormeden yukseltmek "flapping"e -- surekli inip cikan kaliteye -- yol
// acar). Ikisi arasindaki "notr bolge" HER iki sayaci da sifirlar: gurultulu
// (bir iyi bir kotu) kare dizisi HICBIR ZAMAN tier degistirmemeli.
#pragma once
#include <cstdint>

#include "content/device_tier.hpp"

namespace tulpar::engine::content {

class QualityScaler {
public:
  explicit QualityScaler(DeviceTier start = DeviceTier::Mid) : tier_(start) {}

  // Her render karesinde BIR KEZ cagirilir. frame_ms: bu karenin olculen
  // suresi; target_ms: hedef kare suresi (ornek: 60 FPS icin 16.667f).
  // Donus: bu cagridan SONRAKI (degismis olabilecek) tier.
  DeviceTier update(float frame_ms, float target_ms);
  DeviceTier current() const { return tier_; }

private:
  static constexpr int kBadThreshold = 10;    // art arda kotu kare (dusurmeden once)
  static constexpr int kGoodThreshold = 30;   // art arda iyi kare (yukseltmeden once) -- kasitli kBadThreshold'dan buyuk
  static constexpr float kBadFactor = 1.15f;  // hedefin >%15 ustu = kotu kare
  static constexpr float kGoodFactor = 0.85f; // hedefin <%15 alti = iyi kare

  DeviceTier tier_;
  int bad_streak_ = 0;
  int good_streak_ = 0;
};

} // namespace tulpar::engine::content
