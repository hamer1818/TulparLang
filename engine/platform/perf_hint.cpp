#include "platform/perf_hint.hpp"
#include <cstddef> // size_t — libc++ (macOS) gecisli getirmiyor

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace tulpar::engine::platform {

namespace {
#if defined(__ANDROID__)
// android/performance_hint.h'i KASITLI dahil ETMIYORUZ (platform/thermal.cpp
// ile ayni gerekce: fonksiyonlar __INTRODUCED_IN(33) tasir, android-26
// hedefiyle dogrudan cagrisal kullanim riskli). Yalniz ABI (isim+imza)
// dlsym icin yeterli.
struct APerformanceHintManager; // opak
struct APerformanceHintSession; // opak

using PFN_get_manager = APerformanceHintManager *(*)();
using PFN_create_session = APerformanceHintSession *(*)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
using PFN_update_target = int (*)(APerformanceHintSession *, int64_t);
using PFN_report_actual = int (*)(APerformanceHintSession *, int64_t);
using PFN_close_session = void (*)(APerformanceHintSession *);

struct Api {
  void *lib = nullptr;
  APerformanceHintManager *mgr = nullptr;
  PFN_get_manager get_manager = nullptr;
  PFN_create_session create_session = nullptr;
  PFN_update_target update_target = nullptr;
  PFN_report_actual report_actual = nullptr;
  PFN_close_session close_session = nullptr;
  bool tried = false;
};

Api &api() {
  static Api a;
  if (a.tried) return a;
  a.tried = true;
  a.lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
  if (!a.lib) return a;
  a.get_manager = (PFN_get_manager)dlsym(a.lib, "APerformanceHint_getManager");
  a.create_session = (PFN_create_session)dlsym(a.lib, "APerformanceHint_createSession");
  a.update_target = (PFN_update_target)dlsym(a.lib, "APerformanceHint_updateTargetWorkDuration");
  a.report_actual = (PFN_report_actual)dlsym(a.lib, "APerformanceHint_reportActualWorkDuration");
  a.close_session = (PFN_close_session)dlsym(a.lib, "APerformanceHint_closeSession");
  if (!a.get_manager || !a.create_session || !a.update_target || !a.report_actual || !a.close_session) {
    a.get_manager = nullptr; // API 33 alti/bulunamadi: yok say
    return a;
  }
  a.mgr = a.get_manager();
  return a;
}
#endif
} // namespace

bool perf_hint_available() {
#if defined(__ANDROID__)
  return api().mgr != nullptr;
#else
  return false;
#endif
}

PerfHintSession perf_hint_create_session(const int32_t *thread_ids, uint64_t count, int64_t initial_target_ns) {
  PerfHintSession s;
#if defined(__ANDROID__)
  Api &a = api();
  if (!a.mgr) return s;
  s.impl = a.create_session(a.mgr, thread_ids, (size_t)count, initial_target_ns);
#else
  (void)thread_ids;
  (void)count;
  (void)initial_target_ns;
#endif
  return s;
}

void perf_hint_update_target(PerfHintSession session, int64_t target_ns) {
#if defined(__ANDROID__)
  if (!session.valid()) return;
  api().update_target((APerformanceHintSession *)session.impl, target_ns);
#else
  (void)session;
  (void)target_ns;
#endif
}

void perf_hint_report_actual(PerfHintSession session, int64_t actual_ns) {
#if defined(__ANDROID__)
  if (!session.valid()) return;
  api().report_actual((APerformanceHintSession *)session.impl, actual_ns);
#else
  (void)session;
  (void)actual_ns;
#endif
}

void perf_hint_close_session(PerfHintSession session) {
#if defined(__ANDROID__)
  if (!session.valid()) return;
  api().close_session((APerformanceHintSession *)session.impl);
#else
  (void)session;
#endif
}

} // namespace tulpar::engine::platform
