// platform::perf_hint_*: platform/thermal.hpp ile ayni gerekce/desen --
// bu test masaustunde/CI'da HER ZAMAN "kullanilamiyor" yolunu (Android disi
// #else dali) sinar. Gercek APerformanceHint cagrisi (dlopen+dlsym) yalniz
// cihazda dogrulanabilir.
#include "platform/perf_hint.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(perf_hint_reports_unavailable_off_android) {
#if !defined(__ANDROID__)
  CHECK(!platform::perf_hint_available());
  platform::PerfHintSession s = platform::perf_hint_create_session(nullptr, 0, 16666666);
  CHECK(!s.valid());
#else
  skip("bu test yalniz Android-disi derlemede anlamli (cihazda gercek ADPF yolu ayri dogrulanir)");
#endif
}

ENGINE_TEST(perf_hint_invalid_session_calls_are_safe_noops) {
  // Negatif kontrol: gecersiz (impl==nullptr) oturumla update/report/close
  // cagirmak COKMEMELI -- API yokken bile guvenli olmali.
  platform::PerfHintSession invalid;
  CHECK(!invalid.valid());
  platform::perf_hint_update_target(invalid, 16666666);
  platform::perf_hint_report_actual(invalid, 15000000);
  platform::perf_hint_close_session(invalid);
  CHECK(true); // buraya kadar geldiyse cokmedi
}
