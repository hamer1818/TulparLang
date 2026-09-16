// platform::thermal_*: bu test masaustunde/CI'da calisir, yani HER ZAMAN
// "kullanilamiyor" yolunu (Android disi #else dali) sinar -- gercek ADPF
// cagrisi (dlopen+dlsym, __ANDROID__ dali) yalniz cihazda dogrulanabilir
// (DEVAM_PLANI.md'de not edildigi gibi, bu oturumda derleyici yok). Burada
// asil onemli kontrol: API yokken bile GUVENLI sentinel donmesi, cokme yok.
#include "platform/thermal.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(thermal_reports_unavailable_off_android) {
#if !defined(__ANDROID__)
  CHECK(!platform::thermal_available());
  CHECK(platform::thermal_status() == platform::ThermalStatus::kUnavailable);
  CHECK(platform::thermal_headroom(10) == -1.0f);
#else
  skip("bu test yalniz Android-disi derlemede anlamli (cihazda gercek ADPF yolu calisir, ayri dogrulama gerekir)");
#endif
}

ENGINE_TEST(thermal_repeated_calls_are_stable) {
  // Negatif/kararlilik kontrolu: ikinci cagri ilkiyle CELISMEMELI (lazy init
  // sonrasi tutarli sonuc -- coklu cagrida coken/degisen bir sey olmamali).
  const bool a1 = platform::thermal_available();
  const bool a2 = platform::thermal_available();
  CHECK(a1 == a2);
  CHECK(platform::thermal_status() == platform::thermal_status());
}
