#include "tests/test.hpp"

#include <csignal>
#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <unistd.h>

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

// Kosan testin adi: cokme olursa sinyal isleyicisi bunu basar. Cokme,
// ozet satirini engelliyor ve harness FAIL diyor ama NEREDE oldugunu
// soylemiyordu (macOS CI, 2026-09-14: ozet yok, son [bilgi] fizikti).
volatile const char *g_running_test = "?";
void sig_write(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  ssize_t ignored = write(1, s, n);
  (void)ignored;
}
extern "C" void on_fatal_signal(int sig) {
  sig_write("\n  COKME testi: ");
  sig_write(const_cast<const char *>(g_running_test));
  sig_write(sig == SIGSEGV   ? " (SIGSEGV)\n"
            : sig == SIGBUS  ? " (SIGBUS)\n"
            : sig == SIGILL  ? " (SIGILL)\n"
            : sig == SIGFPE  ? " (SIGFPE)\n"
            : sig == SIGABRT ? " (SIGABRT)\n"
                             : " (sinyal)\n");
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
} // namespace

namespace tulpar::engine::test {
Case Registry::cases[Registry::kMax];
int Registry::count = 0;
int Registry::failures = 0;
int Registry::failures_total = 0;
int Registry::skipped = 0;
} // namespace tulpar::engine::test

using namespace tulpar::engine::test;

// APK icinde host cagirir (main yok: ENGINE_TESTS_NO_MAIN). argv[0] yoksa
// cocuk surec isteyen testler gorunur atlanir.
int engine_tests_main(int argc, char **argv) {
  g_engine_tests_exe = (argc > 0 && argv && argv[0] && argv[0][0]) ? argv[0] : nullptr;
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
  for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) std::signal(sig, on_fatal_signal);
  // Pozitif kontrol: isleyici gercekten test adini basiyor mu (Tuzaklar 1m —
  // gormedigin teshise guvenme). `engine_tests --cokme-kontrol` cokmeli.
  if (argc > 1 && argv[1] && std::strcmp(argv[1], "--cokme-kontrol") == 0) {
    g_running_test = "cokme_pozitif_kontrol";
    crash_child_fn();
    return 4;
  }
  const char *only = (argc > 1 && argv[1] && argv[1][0]) ? argv[1] : nullptr;
  // Satir tamponu: CI/dosyaya yonlendirmede asili kalan testin adi GORUNSUN.
  setvbuf(stdout, nullptr, _IOLBF, 0);
  int passed = 0, failed = 0, ran = 0;
  for (int i = 0; i < Registry::count; i++) {
    const Case &c = Registry::cases[i];
    if (only && std::strstr(c.name, only) == nullptr) continue;
    ran++;
    Registry::failures = 0;
    g_running_test = c.name;
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

#if !defined(ENGINE_TESTS_NO_MAIN)
int main(int argc, char **argv) { return engine_tests_main(argc, argv); }
#endif

// --- Arm (Mali) BestPractices sondasi -------------------------------------
// Bildirimi tests/test.hpp'de; govdesi burada cunku Vulkan basliklarini
// test.hpp'ye sokmak istemiyoruz (o baslik her testte var).
#include "rhi/device.hpp"
#include "rhi/vk_api.hpp"

namespace tulpar::engine::test {
bool arm_rules_missing(rhi::Device &dev) {
  VkSamplerCreateInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  si.magFilter = si.minFilter = VK_FILTER_NEAREST;
  si.maxLod = 0.0f; // Arm kurali: LOD kirpmak mip zincirini bosa cikarir
  VkSampler smp = VK_NULL_HANDLE;
  const uint32_t before = dev.best_practice_arm_warnings();
  if (dev.api().vkCreateSampler(dev.handle(), &si, nullptr, &smp) == VK_SUCCESS)
    dev.api().vkDestroySampler(dev.handle(), smp, nullptr);
  const uint32_t after = dev.best_practice_arm_warnings();
  std::printf("    [bilgi] pozitif kontrol (LOD kirpan sampler): Arm uyarisi %u -> %u\n", before, after);
  return after == before;
}
} // namespace tulpar::engine::test
