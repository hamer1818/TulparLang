#include "platform/crash.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <unwind.h>

#include <cstdint>
#include <cstdlib>

namespace tulpar::engine::platform {

namespace {
CrashConfig g_cfg;
bool g_installed = false;
void *g_alt_stack = nullptr;
volatile sig_atomic_t g_in_handler = 0;
char g_dir[512];
char g_build[64];

// --- async-signal-safe yazma yardimcilari ------------------------------------
void w_str(int fd, const char *s) {
  size_t n = strlen(s);
  while (n) {
    ssize_t k = write(fd, s, n);
    if (k <= 0) return;
    s += k;
    n -= (size_t)k;
  }
}
void w_hex(int fd, uint64_t v) {
  char buf[19];
  buf[0] = '0';
  buf[1] = 'x';
  int i = 18;
  buf[i] = 0;
  if (v == 0) buf[--i] = '0';
  while (v) {
    buf[--i] = "0123456789abcdef"[v & 15];
    v >>= 4;
  }
  buf[--i] = 'x';
  buf[--i] = '0';
  w_str(fd, buf + i);
}
void w_dec(int fd, uint64_t v) {
  char buf[24];
  int i = 23;
  buf[i] = 0;
  if (v == 0) buf[--i] = '0';
  while (v) {
    buf[--i] = (char)('0' + v % 10);
    v /= 10;
  }
  w_str(fd, buf + i);
}

struct UnwindState {
  void **pcs;
  int max;
  int n;
  int skip;
};
_Unwind_Reason_Code unwind_cb(struct _Unwind_Context *ctx, void *arg) {
  UnwindState *st = static_cast<UnwindState *>(arg);
  uintptr_t pc = _Unwind_GetIP(ctx);
  if (st->skip > 0) {
    st->skip--;
    return _URC_NO_REASON;
  }
  if (st->n >= st->max) return _URC_END_OF_STACK;
  st->pcs[st->n++] = (void *)pc;
  return _URC_NO_REASON;
}

const char *sig_name(int sig) {
  switch (sig) {
  case SIGSEGV: return "SIGSEGV";
  case SIGBUS: return "SIGBUS";
  case SIGFPE: return "SIGFPE";
  case SIGILL: return "SIGILL";
  case SIGABRT: return "SIGABRT";
  default: return "SIG?";
  }
}

void write_frames(int fd, void **pcs, int n) {
  for (int i = 0; i < n; i++) {
    w_str(fd, "  #");
    w_dec(fd, (uint64_t)i);
    w_str(fd, " ");
    w_hex(fd, (uint64_t)(uintptr_t)pcs[i]);
    Dl_info info;
    // dladdr strict anlamda async-signal-safe degil; cokme isleyicilerinde
    // yaygin pratik (Crashpad/Breakpad de benzer sinirlarla yasar).
    if (dladdr(pcs[i], &info) && info.dli_fname) {
      w_str(fd, " ");
      w_str(fd, info.dli_fname);
      w_str(fd, "+");
      w_hex(fd, (uint64_t)((uintptr_t)pcs[i] - (uintptr_t)info.dli_fbase));
      if (info.dli_sname) {
        w_str(fd, " (");
        w_str(fd, info.dli_sname);
        w_str(fd, ")");
      }
    }
    w_str(fd, "\n");
  }
}

void handler(int sig, siginfo_t *si, void *) {
  if (g_in_handler) _exit(128 + sig); // isleyici icinde cokme: dongu yok
  g_in_handler = 1;

  char path[640];
  // <dir>/crash_<pid>_<sig>.txt
  size_t dl = strlen(g_dir);
  memcpy(path, g_dir, dl);
  const char *mid = "/crash_";
  memcpy(path + dl, mid, 7);
  size_t p = dl + 7;
  {
    char tmp[24];
    int i = 23;
    tmp[i] = 0;
    uint64_t v = (uint64_t)getpid();
    if (v == 0) tmp[--i] = '0';
    while (v) {
      tmp[--i] = (char)('0' + v % 10);
      v /= 10;
    }
    size_t tl = strlen(tmp + i);
    memcpy(path + p, tmp + i, tl);
    p += tl;
  }
  path[p++] = '_';
  path[p++] = (char)('0' + (sig / 10) % 10);
  path[p++] = (char)('0' + sig % 10);
  memcpy(path + p, ".txt", 5);

  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  int fds[2] = {fd, 2};
  for (int k = 0; k < 2; k++) {
    int f = fds[k];
    if (f < 0) continue;
    w_str(f, "=== tulpar engine crash ===\nbuild: ");
    w_str(f, g_build);
    w_str(f, "\nsignal: ");
    w_str(f, sig_name(sig));
    w_str(f, " (");
    w_dec(f, (uint64_t)sig);
    w_str(f, ")\nfault_addr: ");
    w_hex(f, (uint64_t)(uintptr_t)(si ? si->si_addr : nullptr));
    w_str(f, "\nthread: ");
    char tname[32] = "?";
#if defined(__linux__) || defined(__APPLE__)
    pthread_getname_np(pthread_self(), tname, sizeof tname);
#endif
    w_str(f, tname);
    w_str(f, "\nframes:\n");
    void *pcs[64];
    UnwindState st{pcs, 64, 0, 0};
    _Unwind_Backtrace(unwind_cb, &st);
    write_frames(f, pcs, st.n);
    w_str(f, "=== end ===\n");
  }
  if (fd >= 0) close(fd);

  // Varsayilan isleyiciye birak ve yeniden yukselt (core dump / debuggerd).
  signal(sig, SIG_DFL);
  raise(sig);
}
} // namespace

bool crash_reporter_install(const CrashConfig &cfg) {
  if (g_installed) return true;
  g_cfg = cfg;
  strncpy(g_dir, cfg.report_dir ? cfg.report_dir : ".", sizeof g_dir - 1);
  g_dir[sizeof g_dir - 1] = 0;
  strncpy(g_build, cfg.build_id ? cfg.build_id : "dev", sizeof g_build - 1);
  g_build[sizeof g_build - 1] = 0;

  g_alt_stack = std::malloc(cfg.alt_stack_bytes); // acilista, kare icinde degil
  if (!g_alt_stack) return false;
  stack_t ss;
  ss.ss_sp = g_alt_stack;
  ss.ss_size = cfg.alt_stack_bytes;
  ss.ss_flags = 0;
  if (sigaltstack(&ss, nullptr) != 0) return false;

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  const int sigs[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
  for (int s : sigs)
    if (sigaction(s, &sa, nullptr) != 0) return false;
  g_installed = true;
  return true;
}

bool crash_reporter_installed() { return g_installed; }

int crash_capture_frames(void **pcs, int max) {
  UnwindState st{pcs, max, 0, 1};
  _Unwind_Backtrace(unwind_cb, &st);
  return st.n;
}

} // namespace tulpar::engine::platform
