// Crash reporter: cocuk surec kendini cokertir, ebeveyn raporu okur.
// Iki kip: duz fonksiyon ve fiber icinden (plan risk kaydi: fiber yiginlari
// unwinding'i keser mi?). Sembol cozumu: tools/symbolize.py + stripsiz ikili.
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>

#include "platform/crash.hpp"
#include "tests/test.hpp"

extern const char *g_engine_tests_exe;

namespace {
// Rapor dosyasini bul, icerigi buf'a oku; dosya yolunu path'e yaz.
bool read_report(const char *dir, char *path, size_t path_n, char *buf, size_t buf_n) {
  DIR *d = opendir(dir);
  if (!d) return false;
  bool found = false;
  while (struct dirent *e = readdir(d)) {
    if (strncmp(e->d_name, "crash_", 6) == 0) {
      snprintf(path, path_n, "%s/%s", dir, e->d_name);
      found = true;
      break;
    }
  }
  closedir(d);
  if (!found) return false;
  FILE *f = fopen(path, "r");
  if (!f) return false;
  size_t n = fread(buf, 1, buf_n - 1, f);
  buf[n] = 0;
  fclose(f);
  return true;
}

int count_frames(const char *buf) {
  int n = 0;
  for (const char *p = buf; (p = strstr(p, "\n  #")) != nullptr; p++) n++;
  return n;
}

void run_case(bool fiber) {
  if (!g_engine_tests_exe) {
    // APK icinde: cocuk surec exec edilemez (ikili yok, app_process). Gorunur atla.
    ::tulpar::engine::test::skip("alt surec yok (APK icinde kosuyor) — cokme raporu testi masaustunde");
    return;
  }
  char dir[512];
  ::tulpar::engine::test::tmp_template(dir, sizeof dir, "engine_crash");
  CHECK(mkdtemp(dir) != nullptr);
  pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    if (fiber) execl(g_engine_tests_exe, g_engine_tests_exe, "--crash-child", dir, "fiber", (char *)nullptr);
    else execl(g_engine_tests_exe, g_engine_tests_exe, "--crash-child", dir, (char *)nullptr);
    _exit(127);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  CHECK(WIFSIGNALED(status));
  CHECK(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
  static char buf[16384];
  char path[512];
  bool ok = read_report(dir, path, sizeof path, buf, sizeof buf);
  CHECK(ok);
  if (ok) {
    CHECK(strstr(buf, "build: test-build") != nullptr);
    CHECK(strstr(buf, "signal: SIGSEGV") != nullptr || strstr(buf, "signal: SIGBUS") != nullptr);
    CHECK(strstr(buf, "fault_addr: 0x0") != nullptr);
    int frames = count_frames(buf);
    CHECK(frames >= 3);
    CHECK(strstr(buf, "=== end ===") != nullptr);
    // Sembol cozumu: arac varsa crash_child_fn adini bulmali (Linux'ta
    // ELF ofsetleri addr2line/llvm-symbolizer ile dogrudan cozulur).
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "python3 " ENGINE_SOURCE_DIR "/tools/symbolize.py '%s' --binary '%s' 2>/dev/null",
             path, g_engine_tests_exe);
    FILE *pp = popen(cmd, "r");
    static char out[32768];
    size_t n = pp ? fread(out, 1, sizeof out - 1, pp) : 0;
    out[n] = 0;
    int rc = pp ? pclose(pp) : -1;
    if (rc != 0 && WEXITSTATUS(rc) == 2) {
      std::printf("    ATLANDI: sembol cozucu (llvm-symbolizer/addr2line) yok, rapor ham kaldi\n");
    } else {
#if defined(__linux__)
      CHECK(strstr(out, "crash_child_fn") != nullptr);
#else
      std::printf("    [bilgi] sembol cozumu bu platformda iddia edilmiyor: %s\n",
                  strstr(out, "crash_child_fn") ? "cozuldu" : "cozulmedi");
#endif
    }
    unlink(path);
  }
  rmdir(dir);
}
} // namespace

ENGINE_TEST(crash_reporter_writes_report_from_plain_crash) { run_case(false); }
ENGINE_TEST(crash_reporter_writes_report_from_fiber_crash) { run_case(true); }

ENGINE_TEST(crash_capture_frames_walks_current_stack) {
  void *pcs[32];
  int n = tulpar::engine::platform::crash_capture_frames(pcs, 32);
  CHECK(n >= 2);
}
