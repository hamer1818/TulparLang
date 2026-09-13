#include "tests/test.hpp"

#include <cstdint>
#include <cstring>

#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "platform/crash.hpp"

const char *g_engine_tests_exe = nullptr;

namespace {
// Cokme cocugu: rapor testinin kobayi. noinline: sembol cozumu onu bulmali.
// Adres derleyicinin GOREMEDIGI bir global'den okunur: sabit `nullptr`
// dereference'ini GCC -O2+ UB sayip yazmayi SILIYOR (olculdu 2026-09-14:
// `volatile int *p = nullptr; *p = 42;` cokmeden dondu, cikis kodu 4).
volatile uintptr_t g_crash_addr = 0;
__attribute__((noinline)) void crash_child_fn() {
  int *p = reinterpret_cast<int *>(g_crash_addr);
  *p = 42; // SIGSEGV @ 0x0
}
void crash_job(void *) { crash_child_fn(); }
} // namespace

namespace tulpar::engine::test {
Case Registry::cases[Registry::kMax];
int Registry::count = 0;
int Registry::failures = 0;
int Registry::failures_total = 0;
int Registry::skipped = 0;
} // namespace tulpar::engine::test

using namespace tulpar::engine::test;

int main(int argc, char **argv) {
  g_engine_tests_exe = argv[0];
  if (argc >= 3 && std::strcmp(argv[1], "--crash-child") == 0) {
    tulpar::engine::platform::CrashConfig cfg;
    cfg.report_dir = argv[2];
    cfg.build_id = "test-build";
    if (!tulpar::engine::platform::crash_reporter_install(cfg)) return 3;
    if (argc >= 4 && std::strcmp(argv[3], "fiber") == 0) {
      // Fiber ICINDEN cokme: rapor fiber yiginini cozmeli (plan risk kaydi).
      tulpar::engine::SystemArena sys;
      sys.reserve(8u << 20, "crash");
      tulpar::engine::JobSystem js;
      tulpar::engine::JobSystemConfig jc;
      jc.worker_threads = 1;
      js.init(sys, jc);
      tulpar::engine::Counter c;
      js.run(tulpar::engine::JobDecl{crash_job, nullptr, "crash_job"}, &c);
      js.wait(c);
      js.shutdown(); // cokme olmadiysa temiz cik; test yanlis sebeple dusmesin
      return 4;      // ulasilmamali
    }
    crash_child_fn();
    return 4;
  }
  const char *only = argc > 1 ? argv[1] : nullptr;
  // Satir tamponu: CI/dosyaya yonlendirmede asili kalan testin adi GORUNSUN.
  setvbuf(stdout, nullptr, _IOLBF, 0);
  int passed = 0, failed = 0, ran = 0;
  for (int i = 0; i < Registry::count; i++) {
    const Case &c = Registry::cases[i];
    if (only && std::strstr(c.name, only) == nullptr) continue;
    ran++;
    Registry::failures = 0;
    std::printf("  RUN  %s\n", c.name);
    c.fn();
    if (Registry::failures == 0) {
      std::printf("  PASS %s\n", c.name);
      passed++;
    } else {
      std::printf("  FAIL %s (%d kontrol)\n", c.name, Registry::failures);
      failed++;
      Registry::failures_total += Registry::failures;
    }
  }
  std::printf("engine tests: %d passed, %d failed, %d atlandi (%d/%d kosuldu)\n", passed, failed,
              Registry::skipped, ran, Registry::count);
  return failed == 0 && ran > 0 ? 0 : 1;
}
