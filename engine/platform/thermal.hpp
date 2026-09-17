// L0 PLATFORM — ADPF Termal API (420 madde listesi #13/#174-176/#351-353:
// "Termal/Guc Yonetimi", "ADPF", "Thermal API"). Gercek API
// (AThermal_acquireManager/getCurrentThermalStatus/getThermalHeadroom,
// <android/thermal.h>) API 30+ gerektirir, ama bu projenin build hedefi
// android-26 (tools/android_run.sh: ANDROID_PLATFORM=android-26) -- DOGRUDAN
// linklemek eski cihazlarda (DURUM.md'de olculen Huawei P20 Pro dahil)
// dinamik baglayici hatasiyla COKER. Bu yuzden rhi/vk_api.cpp'nin dlopen+
// dlsym deseniyle AYNI sekilde: fonksiyon isaretcileri CALISMA ZAMANINDA
// aranir, bulunamazsa (eski Android/masaustu/test) sessizce "kullanilamiyor"
// doner -- hicbir zaman coker, cagiran her zaman guvenli bir sentinel gorur.
#pragma once

namespace tulpar::engine::platform {

// ATHERMAL_STATUS_* (android/thermal.h) ile BIREBIR ayni tam sayi degerleri
// tasir (dlsym'lenen fonksiyonun donusu dogrudan bu enum'a static_cast edilir).
enum class ThermalStatus {
  kUnavailable = -2, // bu API/surum yok (masaustu, API<30, ya da dlopen basarisiz)
  kError = -1,
  kNone = 0,
  kLight = 1,
  kModerate = 2,
  kSevere = 3,
  kCritical = 4,
  kEmergency = 5,
  kShutdown = 6,
};

// API gercekten kullanilabiliyor mu (Android + API 30+ + dlopen basarili).
// Ilk cagrida dlopen/dlsym dener (lazy, tek seferlik, surec omru boyunca).
bool thermal_available();

ThermalStatus thermal_status();

// forecast_seconds sonrasi icin tahmini termal pay: 0 = throttling yok,
// >=1.0 => agir/tam throttling (ust sinir YOK, gercek API boyle tanimlar).
// Kullanilamiyorsa -1.0f doner (cagiran negatifi "bilinmiyor" okumali).
float thermal_headroom(int forecast_seconds);

} // namespace tulpar::engine::platform
