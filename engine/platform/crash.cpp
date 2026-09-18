#include "platform/crash.hpp"

// PLATFORM NOTU: mingw-w64 `unistd.h`, `fcntl.h`, `dirent.h` ve GCC de
// `unwind.h` tasidigi icin dosya yazma ve YIGIN COZME kodu Windows'ta AYNEN
// kullaniliyor (_Unwind_Backtrace GCC'nin kendi cozucusudur). Platforma ozgu
// kalan yalnizca iki sey:
//   * yakalama mekanizmasi: POSIX sigaction+sigaltstack / Windows
//     SetUnhandledExceptionFilter
//   * adres -> modul cozumu: dladdr / GetModuleHandleEx + GetModuleFileName
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <unwind.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>   // getpid
#else
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#endif

#include <cstdint>
#include <cstdlib>

namespace tulpar::engine::platform {

namespace {
CrashConfig g_cfg;
bool g_installed = false;
void *g_alt_stack = nullptr;
volatile int g_in_handler = 0;
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

// Rapor bicimi PLATFORMDAN BAGIMSIZ: Windows'ta da POSIX sinyal adlari
// yaziliyor (erisim ihlali -> SIGSEGV). Boylece tek bir kapi/ayristirici iki
// platformu da okuyabiliyor; Windows'a ozgu kod ayrica `exception:` satirinda.
const char *sig_name(int sig) {
  switch (sig) {
  case 11: return "SIGSEGV";
  case 7:  return "SIGBUS";
  case 8:  return "SIGFPE";
  case 4:  return "SIGILL";
  case 6:  return "SIGABRT";
  default: return "SIG?";
  }
}

void write_frames(int fd, void **pcs, int n) {
  for (int i = 0; i < n; i++) {
    w_str(fd, "  #");
    w_dec(fd, (uint64_t)i);
    w_str(fd, " ");
    w_hex(fd, (uint64_t)(uintptr_t)pcs[i]);
#if defined(_WIN32)
    // Windows'ta dladdr yok: adresi ICEREN modulu bul, adini ve modul basina
    // gore OFSETINI yaz. Sembol adi icin DbgHelp (SymFromAddr) gerekir; onu
    // BILINCLI olarak cagirmiyoruz — cokme isleyicisinde kilit alan, bellek
    // ayiran ve tek thread'e serilesen bir kutuphanedir. Ofset + modul,
    // `addr2line`/`nm` ile cozulmeye yeter.
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)pcs[i], &mod) &&
        mod) {
      char modpath[MAX_PATH];
      DWORD len = GetModuleFileNameA(mod, modpath, sizeof modpath);
      if (len > 0 && len < sizeof modpath) {
        w_str(fd, " ");
        w_str(fd, modpath);
        w_str(fd, "+");
        w_hex(fd, (uint64_t)((uintptr_t)pcs[i] - (uintptr_t)mod));
      }
    }
#else
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
#endif
    w_str(fd, "\n");
  }
}

// RAPORU YAZAN ORTAK GOVDE. Yakalama mekanizmasi platforma gore degisiyor
// (POSIX sinyal / Windows unhandled-exception filtresi) ama RAPOR BICIMI ve
// yazma yolu tek yerde: iki platformun ciktisi ayrisamaz.
void write_report(int sig, const void *fault_addr) {

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
    w_hex(f, (uint64_t)(uintptr_t)fault_addr);
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
}

#if defined(_WIN32)
// Windows: yakalama SetUnhandledExceptionFilter ile. Exception kodunu POSIX
// sinyal numarasina esliyoruz ki rapor bicimi iki platformda AYNI kalsin.
int sig_from_exception(DWORD code) {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION:
  case EXCEPTION_IN_PAGE_ERROR:        return 11;  // SIGSEGV
  case EXCEPTION_DATATYPE_MISALIGNMENT: return 7;  // SIGBUS
  case EXCEPTION_ILLEGAL_INSTRUCTION:
  case EXCEPTION_PRIV_INSTRUCTION:     return 4;   // SIGILL
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
  case EXCEPTION_FLT_INVALID_OPERATION:
  case EXCEPTION_FLT_OVERFLOW:
  case EXCEPTION_INT_OVERFLOW:         return 8;   // SIGFPE
  default:                             return 6;   // SIGABRT
  }
}

// YAKALAMA NOKTASI: VECTORED handler (SetUnhandledExceptionFilter DEGIL).
// Sebep olculdu (2026-09-18, mingw): MinGW CRT'si SEH istisnalarini C
// sinyallerine CEVIRIR (`_gnu_exception_handler` -> SIGSEGV) ve testin kendi
// `std::signal(SIGSEGV, ...)` isleyicisi calisip sureci bitirdigi icin
// "unhandled" filtresine SIRA GELMIYOR — rapor hic yazilmiyordu. Vectored
// isleyici SEH zincirinin EN BASINDA kosar, yani CRT cevirisinden once.
//
// Her istisna icin cagrildigi icin filtre SIKI: yalniz onulmez donanim
// hatalarinda rapor yazilir. C++ istisnasi (0xE06D7363), kesme noktasi ve
// benzeri normal olaylar dokunulmadan gecer.
LONG WINAPI win_vectored(EXCEPTION_POINTERS *ep) {
  DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION:
  case EXCEPTION_IN_PAGE_ERROR:
  case EXCEPTION_DATATYPE_MISALIGNMENT:
  case EXCEPTION_ILLEGAL_INSTRUCTION:
  case EXCEPTION_PRIV_INSTRUCTION:
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
  case EXCEPTION_STACK_OVERFLOW:
    break;
  default:
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (g_in_handler) return EXCEPTION_CONTINUE_SEARCH; // isleyici icinde cokme
  g_in_handler = 1;
  const void *fault = nullptr;
  // Erisim ihlalinde ADRES ExceptionInformation[1]'dedir ([0] = okuma/yazma).
  if (ep && ep->ExceptionRecord &&
      (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
      ep->ExceptionRecord->NumberParameters >= 2) {
    fault = (const void *)ep->ExceptionRecord->ExceptionInformation[1];
  } else if (ep && ep->ExceptionRecord) {
    fault = ep->ExceptionRecord->ExceptionAddress;
  }
  write_report(sig_from_exception(code), fault);
  // RAPOR YAZILDI, SIMDI OL. POSIX dali sinyali varsayilana birakip yeniden
  // yukseltiyor (core dump + kesin olum). Windows'ta "aramaya devam et"
  // demek, isletim sisteminin hata iletisim kutusuna / WER'e / winedbg'ye
  // dusmek demek: olculdu (2026-09-18, Wine) — cokme testinin cocuk sureci
  // orada ASILI KALDI ve paket 10 dakika sonra zaman asimina ugradi.
  // TerminateProcess ile istisna koduyla oluyoruz: cikis kodu POSIX'teki
  // "sinyalle oldu" bilgisinin karsiligi (0xC0000005 vb.) ve hicbir UI yok.
  // Hata ayiklayici VARSA dokunmuyoruz — gelistirici kirilma noktasini
  // gormeli.
  if (!IsDebuggerPresent()) TerminateProcess(GetCurrentProcess(), code);
  return EXCEPTION_CONTINUE_SEARCH;
}
#else
void handler(int sig, siginfo_t *si, void *) {
  if (g_in_handler) _exit(128 + sig); // isleyici icinde cokme: dongu yok
  g_in_handler = 1;
  write_report(sig, si ? si->si_addr : nullptr);
  // Varsayilan isleyiciye birak ve yeniden yukselt (core dump / debuggerd).
  signal(sig, SIG_DFL);
  raise(sig);
}
#endif
} // namespace

bool crash_reporter_install(const CrashConfig &cfg) {
  if (g_installed) return true;
  g_cfg = cfg;
  strncpy(g_dir, cfg.report_dir ? cfg.report_dir : ".", sizeof g_dir - 1);
  g_dir[sizeof g_dir - 1] = 0;
  strncpy(g_build, cfg.build_id ? cfg.build_id : "dev", sizeof g_build - 1);
  g_build[sizeof g_build - 1] = 0;

#if defined(_WIN32)
  // Windows'ta ayri sinyal yigini (sigaltstack) YOK: cekirdek zaten guard
  // sayfasini asan cokmeler icin ayri bir yol tutar ve filtre normal yiginda
  // kosar. Yani alt_stack_bytes bu platformda kullanilmiyor.
  (void)g_alt_stack;
  // Isletim sisteminin "program cokti" kutusu KAPATILIR: rapor bizde,
  // kullaniciyi/CI'i bekletecek bir diyalog istemiyoruz.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  // 1 = zincirin BASINA ekle (CRT'nin cevirisinden once kossun).
  AddVectoredExceptionHandler(1, win_vectored);
  g_installed = true;
  return true;
#else
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
#endif
}

bool crash_reporter_installed() { return g_installed; }

int crash_capture_frames(void **pcs, int max) {
  UnwindState st{pcs, max, 0, 1};
  _Unwind_Backtrace(unwind_cb, &st);
  return st.n;
}

} // namespace tulpar::engine::platform
