#include "platform/thermal.hpp"

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace tulpar::engine::platform {

namespace {
#if defined(__ANDROID__)
// android/thermal.h'i KASITLI olarak dahil ETMIYORUZ: o basliktaki
// fonksiyon bildirimleri __INTRODUCED_IN(30) tasir, android-26 hedefiyle
// derlerken dogrudan cagrisal kullanim riskli olabilir. dlsym icin yalniz
// ABI'yi (isim + imza) bilmek yeterli -- rhi/vk_api.cpp'nin Vulkan
// fonksiyon isaretcilerini elle bildirmesiyle ayni desen.
struct AThermalManager; // opak tip: hicbir zaman elle cozulmez, sadece tasinir

using PFN_acquire = AThermalManager *(*)();
using PFN_status = int (*)(AThermalManager *);   // AThermalStatus, C ABI'de int
using PFN_headroom = float (*)(AThermalManager *, int);

struct Api {
  void *lib = nullptr;
  AThermalManager *mgr = nullptr;
  PFN_acquire acquire = nullptr;
  PFN_status status = nullptr;
  PFN_headroom headroom = nullptr;
  bool tried = false;
};

// C++11+ fonksiyon-yerel static: ilk erisim thread-safe (magic statics).
Api &api() {
  static Api a;
  if (a.tried) return a;
  a.tried = true;
  a.lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
  if (!a.lib) return a;
  a.acquire = (PFN_acquire)dlsym(a.lib, "AThermal_acquireManager");
  a.status = (PFN_status)dlsym(a.lib, "AThermal_getCurrentThermalStatus");
  a.headroom = (PFN_headroom)dlsym(a.lib, "AThermal_getThermalHeadroom"); // opsiyonel (API 30, status ile ayni surum ama ayri kontrol)
  if (!a.acquire || !a.status) { a.acquire = nullptr; return a; } // API 30 alti/bulunamadi: yok say
  a.mgr = a.acquire();
  return a;
}
#endif
} // namespace

bool thermal_available() {
#if defined(__ANDROID__)
  return api().mgr != nullptr;
#else
  return false;
#endif
}

ThermalStatus thermal_status() {
#if defined(__ANDROID__)
  Api &a = api();
  if (!a.mgr || !a.status) return ThermalStatus::kUnavailable;
  const int s = a.status(a.mgr);
  if (s < -1 || s > 6) return ThermalStatus::kUnavailable; // beklenmeyen deger: guvenli tarafta kal
  return static_cast<ThermalStatus>(s);
#else
  return ThermalStatus::kUnavailable;
#endif
}

float thermal_headroom(int forecast_seconds) {
#if defined(__ANDROID__)
  Api &a = api();
  if (!a.mgr || !a.headroom) return -1.0f;
  return a.headroom(a.mgr, forecast_seconds);
#else
  (void)forecast_seconds;
  return -1.0f;
#endif
}

} // namespace tulpar::engine::platform
