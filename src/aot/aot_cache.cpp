// DERLEME ONBELLEGI — icerik adresli anahtar. Model ve gerekce: aot_cache.hpp.
// Ortam degiskeni siniflari: aot_cache_env.inc (kapisi:
// tests/onbellek_anahtari_kapisi.py). Davranis kapisi: tests/onbellek.sh.
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  // GetFileInformationByHandleEx (FileBasicInfo)
#endif
#include "aot_cache.hpp"
#include "../common/import_resolve.hpp"
#include "aot_pipeline.hpp"
#include "../common/platform.h"
#include "../common/localization.hpp"
#include "../common/version.hpp"
#include "../embedded_libs.h"
#include "../ext/extensions.hpp"
#include "../pkg/sha256.hpp"

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#if PLATFORM_LINUX
#include <link.h>  // dl_iterate_phdr: yuklu libLLVM
#endif
#if PLATFORM_MACOS
#include <mach-o/dyld.h>
#endif
#endif

#if !PLATFORM_WINDOWS
extern "C" char **environ;  // Windows: <stdlib.h>'nin _environ'u (makro)
#endif

namespace fs = std::filesystem;

namespace tulpar {
namespace cache {
namespace {

// ---- Ortam degiskeni siniflari (aot_cache_env.inc) -------------------------
struct EnvRule {
  int kind;  // 0 tam ad, 1 onek, 2 sonek
  const char *pat;
};
const EnvRule kRuntimeRules[] = {
#define ONBELLEK_CALISMA_ZAMANI_ONEK(a, n) {1, a},
#define ONBELLEK_CALISMA_ZAMANI_SONEK(a, n) {2, a},
#define ONBELLEK_CALISMA_ZAMANI_AD(a, n) {0, a},
#include "aot_cache_env.inc"
};
const EnvRule kControlRules[] = {
#define ONBELLEK_DENETIM_ONEK(a, n) {1, a},
#define ONBELLEK_DENETIM_AD(a, n) {0, a},
#include "aot_cache_env.inc"
};
const char *const kObserveVars[] = {
#define ONBELLEK_GOZLEM_AD(a, n) a,
#include "aot_cache_env.inc"
};

bool rule_match(const EnvRule *r, size_t n, const std::string &name) {
  for (size_t i = 0; i < n; i++) {
    const size_t pl = strlen(r[i].pat);
    if (r[i].kind == 0 && name == r[i].pat) return true;
    if (r[i].kind == 1 && name.compare(0, pl, r[i].pat) == 0) return true;
    if (r[i].kind == 2 && name.size() >= pl &&
        name.compare(name.size() - pl, pl, r[i].pat) == 0)
      return true;
  }
  return false;
}

// Bagimli olmayan ama BAGLAYICININ okudugu ortam (clang/ld). Yol aramasi
// (PATH) anahtarda degil: onun yerine COZULEN surucunun kimligi var.
const char *const kToolchainEnv[] = {
    "LIBRARY_PATH", "LD_RUN_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX",
    "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "DEVELOPER_DIR", "CCC_OVERRIDE_OPTIONS",
};

#if PLATFORM_WINDOWS
std::string upper(std::string s) {
  for (char &c : s)
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  return s;
}
#endif

// ---- Kucuk yardimcilar ------------------------------------------------------
std::string fmt(const char *f, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

bool read_bytes(const std::string &path, std::string &out) {
  out.clear();
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[1 << 16];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return true;
}

bool write_bytes(const std::string &path, const std::string &data) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  return (fclose(f) == 0) && ok;
}

long pid_now() {
#if PLATFORM_WINDOWS
  return (long)_getpid();
#else
  return (long)getpid();
#endif
}

// Surec + an + sayac: ayni surecte, PID'i paylasan iki konteynerde (ayni kok
// dizini baglanmis) ve hizli ardisik cagrilarda da tekil.
std::string unique_suffix() {
  static unsigned counter = 0;
  const unsigned long long ns = (unsigned long long)std::chrono::duration_cast<
      std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
  return fmt("%ld-%llx-%u", pid_now(), ns, ++counter);
}

// Dosya KIMLIGI: boyut + mtime(ns) + ctime(ns) + inode + aygit; icerik
// okunmaz. ctime'i kullanici geri alamaz (touch -r yalniz mtime/atime'i
// degistirir; zamanlari AYARLAMAK da ctime'i "simdi" yapar), yani "icerigi
// degistirip mtime'i geri almak" da kimligi degistirir.
// Windows: st_ctime orada OLUSTURMA zamanidir ve NTFS "tunneling" ayni adla
// yeniden yaratilan dosyaya eskisini geri verir — ise yaramaz. Yerine NTFS'in
// ChangeTime'i (POSIX ctime karsiligi; SetFileTime onu da gunceller) ve dosya
// kimligi (FileIndex + birim seri no) kullaniliyor; yazma zamani 100 ns.
std::string file_identity(const std::string &path) {
#if PLATFORM_WINDOWS
  HANDLE h = CreateFileA(path.c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return "yok";
  BY_HANDLE_FILE_INFORMATION bi;
  FILE_BASIC_INFO basic;
  const bool ok1 = GetFileInformationByHandle(h, &bi) != 0;
  const bool ok2 = GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic) != 0;
  CloseHandle(h);
  if (!ok1) return "yok";
  if (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return "dizin";
  const unsigned long long size = ((unsigned long long)bi.nFileSizeHigh << 32) | bi.nFileSizeLow;
  const unsigned long long mt =
      ((unsigned long long)bi.ftLastWriteTime.dwHighDateTime << 32) | bi.ftLastWriteTime.dwLowDateTime;
  const unsigned long long idx = ((unsigned long long)bi.nFileIndexHigh << 32) | bi.nFileIndexLow;
  const unsigned long long chg = ok2 ? (unsigned long long)basic.ChangeTime.QuadPart : 0ull;
  return fmt("%llu %llu %llu %llu %lu", size, mt, chg, idx, (unsigned long)bi.dwVolumeSerialNumber);
#else
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return "yok";
  if (S_ISDIR(st.st_mode)) return "dizin";
#if PLATFORM_MACOS
  const long mns = st.st_mtimespec.tv_nsec, cns = st.st_ctimespec.tv_nsec;
#else
  const long mns = st.st_mtim.tv_nsec, cns = st.st_ctim.tv_nsec;
#endif
  return fmt("%lld %lld.%09ld %lld.%09ld %llu %llu", (long long)st.st_size,
             (long long)st.st_mtime, mns, (long long)st.st_ctime, cns,
             (unsigned long long)st.st_ino, (unsigned long long)st.st_dev);
#endif
}

bool is_regular_file(const std::string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

// Mutlak + normal yol. ONCE absolute: weakly_canonical goreli ve HENUZ
// OLMAYAN bir yolu goreli birakiyor ("irb" -> "irb"; dosya yazildiktan sonra
// "/dizin/irb") — ilk ve ikinci derlemenin anahtari farkli cikiyordu.
std::string canonical(const std::string &p) {
  std::error_code ec;
  fs::path a = fs::absolute(fs::path(p), ec);
  if (ec) return p;
  fs::path c = fs::weakly_canonical(a, ec);
  if (ec) c = a;
  return c.lexically_normal().string();
}

std::string cwd_abs() {
  std::error_code ec;
  fs::path c = fs::current_path(ec);
  return ec ? std::string() : c.string();
}

std::string self_exe() {
#if PLATFORM_WINDOWS
  char buf[4096];
  const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf);
  return (n > 0 && n < sizeof buf) ? std::string(buf, n) : std::string();
#elif PLATFORM_MACOS
  char buf[4096];
  uint32_t sz = sizeof buf;
  if (_NSGetExecutablePath(buf, &sz) != 0) return "";
  return canonical(buf);
#else
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = '\0';
  return buf;
#endif
}

#if PLATFORM_LINUX
int phdr_collect(struct dl_phdr_info *info, size_t, void *data) {
  auto *out = static_cast<std::vector<std::string> *>(data);
  if (info->dlpi_name && *info->dlpi_name) {
    const char *base = strrchr(info->dlpi_name, '/');
    base = base ? base + 1 : info->dlpi_name;
    if (strstr(base, "LLVM")) out->push_back(info->dlpi_name);
  }
  return 0;
}
#endif

// Kod uretimini yapan LLVM surucunun icinde degilse (dagitimlarin
// libLLVM.so'su) onun da kimligi: `pacman -Syu` LLVM'i guncellediginde tulpar
// ikilisi AYNI kalir ama kod uretimi degisir.
std::vector<std::string> loaded_llvm_libs() {
  std::vector<std::string> out;
#if PLATFORM_LINUX
  dl_iterate_phdr(phdr_collect, &out);
#elif PLATFORM_MACOS
  const uint32_t n = _dyld_image_count();
  for (uint32_t i = 0; i < n; i++) {
    const char *name = _dyld_get_image_name(i);
    if (!name) continue;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (strstr(base, "LLVM")) out.push_back(name);
  }
#endif
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string resolve_in_path(const std::string &prog) {
  if (prog.empty()) return "";
  if (prog.find('/') != std::string::npos || prog.find('\\') != std::string::npos)
    return is_regular_file(prog) ? prog : std::string();
  const char *path = getenv("PATH");
  if (!path) return "";
#if PLATFORM_WINDOWS
  const char sep = ';';
  const char *const exts[] = {"", ".exe"};
#else
  const char sep = ':';
  const char *const exts[] = {""};
#endif
  std::string all(path);
  size_t start = 0;
  while (start <= all.size()) {
    size_t end = all.find(sep, start);
    if (end == std::string::npos) end = all.size();
    std::string dir = all.substr(start, end - start);
    if (!dir.empty()) {
      for (const char *e : exts) {
        std::string cand = dir + "/" + prog + e;
        if (is_regular_file(cand)) return cand;
      }
    }
    start = end + 1;
  }
  return "";
}

// `-L"dizin" ` dizisinden dizinler.
std::vector<std::string> parse_search_dirs(const std::string &s) {
  std::vector<std::string> out;
  size_t p = 0;
  while ((p = s.find("-L\"", p)) != std::string::npos) {
    p += 3;
    const size_t q = s.find('"', p);
    if (q == std::string::npos) break;
    out.push_back(s.substr(p, q - p));
    p = q + 1;
  }
  return out;
}

// ---- Import taramasi ---------------------------------------------------------
// Kaynaktaki `import "<ad>"` adlari. Bilerek METIN taramasi: yorum ve bosluk
// atlanir; dizgi/yorum icindeki bir `import "x"` de sayilir (fazladan girdi =
// yalniz gereksiz iska). Eksik sayilan bir import ise BAYAT ikili uretmez:
// kodgen onu diskten okursa oz denetim (note_input) gorur ve sonuc onbellege
// yazilmaz; gomulu/eklenti modulune cozulurse icerigi zaten anahtarda (surucu /
// eklenti bolumu).
void scan_import_names(const std::string &src, std::vector<std::string> &out) {
  const size_t n = src.size();
  size_t pos = 0;
  while ((pos = src.find("import", pos)) != std::string::npos) {
    const size_t kw = pos;
    pos += 6;
    auto ident = [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_' || (unsigned char)c >= 0x80;
    };
    if (kw > 0 && ident(src[kw - 1])) continue;
    if (pos < n && ident(src[pos])) continue;
    size_t p = pos;
    for (;;) {  // bosluk + yorum atla
      while (p < n && (src[p] == ' ' || src[p] == '\t' || src[p] == '\n' || src[p] == '\r'))
        p++;
      if (p + 1 < n && src[p] == '/' && src[p + 1] == '/') {
        while (p < n && src[p] != '\n') p++;
        continue;
      }
      if (p + 1 < n && src[p] == '/' && src[p + 1] == '*') {
        const size_t e = src.find("*/", p + 2);
        p = (e == std::string::npos) ? n : e + 2;
        continue;
      }
      break;
    }
    if (p >= n || src[p] != '"') continue;
    std::string name;
    size_t q = p + 1;
    for (; q < n && src[q] != '"' && src[q] != '\n'; q++) {
      if (src[q] == '\\' && q + 1 < n) {
        // Lexer'in kacislari: \" \\ \n \t ... Ad icinde kacis beklenmez; olursa
        // en yakin yorum (oz denetim yanlisi yakalar).
        q++;
        const char c = src[q];
        name += (c == 'n') ? '\n' : (c == 't') ? '\t' : c;
        continue;
      }
      name += src[q];
    }
    if (q < n && src[q] == '"' && !name.empty()) out.push_back(name);
    pos = q;
  }
}

struct Resolved {
  int kind = 0;  // 0 yok, 1 gomulu, 2 eklenti, 3 disk
  std::string path, dir, src;
  // Ice aktaranin dizini kazandi ama calisma dizininde ayni adli baska dosya
  // da var: kodgen UYARI basiyor ve uyari isabette yeniden basiliyor —
  // golgelenenin VARLIGI anahtarda (silinince uyari bayat kalmasin).
  std::string golge;
};

// Cozum sirasi src/aot/llvm_backend.cpp import_load_module ile AYNI: gomulu
// stdlib -> yerel eklenti modulu -> DISK adaylari. Disk adaylari kodgenle
// AYNI fonksiyondan (src/common/import_resolve.hpp: once ice aktaranin
// dizini, sonra calisma dizini, sonra tulpar_modules/) — 2026-10-08'e kadar
// burada elle yazilmis bir kopyaydi; kural kodgende degisip burada
// unutulsaydi anahtar YANLIS dosyanin ozetini tasirdi (bayat ikili,
// Tuzaklar 7n). Ana dosya icin from_dir onun dizini (compute_key).
Resolved resolve_import(const std::string &name, const std::string &from_dir) {
  Resolved r;
  if (const char *emb = get_embedded_lib(name.c_str())) {
    r.kind = 1;
    r.src = emb;
    return r;
  }
  {
    std::string src, path, dir;
    if (tulpar::ext::read_module(name.c_str(), src, path, dir)) {
      r.kind = 2;
      r.src = std::move(src);
      r.path = std::move(path);
      r.dir = std::move(dir);
      return r;
    }
  }
  const tulpar::imports::Resolution d = tulpar::imports::resolve_disk(name, from_dir);
  if (!d.found) return r;
  FILE *f = fopen(d.path.c_str(), "rb");
  if (!f) return r;
  char buf[1 << 16];
  size_t k;
  while ((k = fread(buf, 1, sizeof buf, f)) > 0) r.src.append(buf, k);
  fclose(f);
  r.kind = 3;
  r.path = d.path;
  r.dir = d.dir;
  r.golge = d.shadowed;
  return r;
}

struct ScanState {
  std::string material;
  std::set<std::string> visited;
  std::set<std::string> inputs;
};

void scan_imports(const std::string &src, const std::string &from_dir, int depth,
                  ScanState &st) {
  if (depth > 32) return;  // kodgen 8'de keser; fazlasi oz denetime kalir
  std::vector<std::string> names;
  scan_import_names(src, names);
  for (const auto &name : names) {
    if (!st.visited.insert(from_dir + '\x1f' + name).second) continue;
    Resolved r = resolve_import(name, from_dir);
    st.material += "import " + name + " <" + from_dir + "> -> ";
    switch (r.kind) {
      case 0:
        st.material += "yok\n";
        break;
      case 1:
        st.material += "gomulu\n";
        scan_imports(r.src, "", depth + 1, st);
        break;
      default:
        st.material += (r.kind == 2 ? "eklenti " : "disk ") + r.path + " " +
                       tulpar::sha256_hex(r.src) +
                       (r.golge.empty() ? std::string() : " golge " + r.golge) + "\n";
        st.inputs.insert(canonical(r.path));
        scan_imports(r.src, r.dir, depth + 1, st);
        break;
    }
  }
}

// ---- Oz denetim kaydi --------------------------------------------------------
std::mutex g_rec_mu;
bool g_recording = false;
std::vector<std::string> g_recorded;

// ---- stderr yakalama durumu ---------------------------------------------------
// Sinyal isleyicisi yalniz async-signal-safe cagrilar yapar (dup2/lseek/read/
// write); durum duz degiskenlerde.
int g_cap_saved = -1;  // asil stderr'in kopyasi
int g_cap_fd = -1;     // yakalama dosyasi
bool g_cap_atexit = false;
typedef void (*SigFn)(int);
const int kCapSignals[] = {SIGSEGV, SIGILL, SIGFPE, SIGABRT
#ifdef SIGBUS
                           ,
                           SIGBUS
#endif
};
SigFn g_cap_prev[sizeof(kCapSignals) / sizeof(kCapSignals[0])];

#if PLATFORM_WINDOWS
#define CAP_DUP _dup
#define CAP_DUP2 _dup2
#define CAP_CLOSE _close
#define CAP_READ _read
#define CAP_WRITE _write
#define CAP_LSEEK _lseek
#else
#define CAP_DUP dup
#define CAP_DUP2 dup2
#define CAP_CLOSE close
#define CAP_READ read
#define CAP_WRITE write
#define CAP_LSEEK lseek
#endif

// Yakalananlari asil stderr'e aktar ve yonlendirmeyi geri al. Hem normal
// bitiste hem exit()/cokmede cagrilir.
void cap_forward_raw() {
  if (g_cap_saved < 0 || g_cap_fd < 0) return;
  CAP_DUP2(g_cap_saved, 2);
  CAP_LSEEK(g_cap_fd, 0, SEEK_SET);
  char buf[4096];
  for (;;) {
    const int n = (int)CAP_READ(g_cap_fd, buf, sizeof buf);
    if (n <= 0) break;
    int off = 0;
    while (off < n) {
      const int w = (int)CAP_WRITE(2, buf + off, (unsigned)(n - off));
      if (w <= 0) break;
      off += w;
    }
  }
  CAP_CLOSE(g_cap_fd);
  g_cap_fd = -1;
  CAP_CLOSE(g_cap_saved);
  g_cap_saved = -1;
}

void cap_on_exit() {
  fflush(stderr);
  cap_forward_raw();
}

void cap_on_signal(int sig) {
  cap_forward_raw();
  for (size_t i = 0; i < sizeof(kCapSignals) / sizeof(kCapSignals[0]); i++)
    if (kCapSignals[i] == sig) signal(sig, g_cap_prev[i] ? g_cap_prev[i] : SIG_DFL);
  raise(sig);
}

// ---- Kok dizin ve tavan ------------------------------------------------------
std::string run_dir() {
  const std::string r = root_dir();
  return r.empty() ? r : r + "/run";
}
std::string build_dir() {
  const std::string r = root_dir();
  return r.empty() ? r : r + "/build";
}
bool ensure_dir(const std::string &d) {
  if (d.empty()) return false;
  std::error_code ec;
  fs::create_directories(fs::path(d), ec);
  return fs::is_directory(fs::path(d), ec);
}

unsigned long long cap_bytes() {
  unsigned long long mb = 512;
  if (const char *e = getenv("TULPAR_CACHE_MAX_MB"); e && *e) {
    char *end = nullptr;
    const unsigned long long v = strtoull(e, &end, 10);
    if (end && *end == '\0' && v >= 1) mb = v;
  }
  return mb * 1024ull * 1024ull;
}

#if PLATFORM_WINDOWS
const char *kExeSuffix = ".exe";
#else
const char *kExeSuffix = "";
#endif

std::string entry_path(const std::string &hex) { return run_dir() + "/" + hex + kExeSuffix; }
std::string diag_path(const std::string &hex) { return run_dir() + "/" + hex + ".diag"; }

bool is_hex32(const std::string &s) {
  if (s.size() != 32) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

// LRU icin "son kullanim": mtime'i simdiye cek (atime cogu baglamada
// guvenilmez: noatime/relatime).
void touch_now(const std::string &p) {
#if PLATFORM_WINDOWS
  HANDLE h = CreateFileA(p.c_str(), FILE_WRITE_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  SetFileTime(h, nullptr, nullptr, &ft);
  CloseHandle(h);
#else
  utimensat(AT_FDCWD, p.c_str(), nullptr, 0);
#endif
}

// Yerine KOYARAK tasi (yalniz tani/kayit dosyalari icin; ikililer icin degil).
bool replace_file(const std::string &from, const std::string &to) {
#if PLATFORM_WINDOWS
  return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
  return rename(from.c_str(), to.c_str()) == 0;
#endif
}

// Kayit dosyasi: atomik yaz (gecici + yeniden adlandir).
bool write_atomic(const std::string &path, const std::string &data) {
  const std::string tmp = path + ".tmp-" + unique_suffix();
  if (!write_bytes(tmp, data)) {
    remove(tmp.c_str());
    return false;
  }
  if (!replace_file(tmp, path)) {
    remove(tmp.c_str());
    return false;
  }
  return true;
}

}  // namespace

// =============================================================================
bool report_enabled() {
  const char *e = getenv("TULPAR_CACHE_RAPOR");
  return e && *e && strcmp(e, "0") != 0;
}

void report(const char *f, ...) {
  if (!report_enabled()) return;
  char buf[2048];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  fprintf(stderr, "[onbellek] %s\n", buf);
  fflush(stderr);
}

void report_material(const Key &k) {
  const char *e = getenv("TULPAR_CACHE_RAPOR");
  if (!e || strcmp(e, "2") != 0 || !k.ok) return;
  fprintf(stderr, "[onbellek] anahtar %s:\n%s", k.hex.c_str(), k.material.c_str());
  fflush(stderr);
}

std::string root_dir() {
  if (const char *d = getenv("TULPAR_CACHE_DIR"); d && *d) return d;
#if PLATFORM_WINDOWS
  if (const char *l = getenv("LOCALAPPDATA"); l && *l) return std::string(l) + "\\tulpar\\cache";
  if (const char *t = getenv("TEMP"); t && *t) return std::string(t) + "\\tulpar\\cache";
  return "";
#elif PLATFORM_MACOS
  if (const char *h = getenv("HOME"); h && *h) return std::string(h) + "/Library/Caches/tulpar";
  return "";
#else
  if (const char *x = getenv("XDG_CACHE_HOME"); x && x[0] == '/') return std::string(x) + "/tulpar";
  if (const char *h = getenv("HOME"); h && *h) return std::string(h) + "/.cache/tulpar";
  return "";
#endif
}

bool usable(std::string &why) {
  const char *nc = getenv("TULPAR_AOT_NOCACHE");
  if (nc && *nc && strcmp(nc, "0") != 0) {
    why = "TULPAR_AOT_NOCACHE";
    return false;
  }
  // GOZLEM degiskeni AYARLIYSA (degeri ne olursa olsun: TULPAR_DBG_VER gibileri
  // yalniz varligina bakiyor) derleme gozlemleniyor demektir: isabet onu atlardi.
  for (const char *v : kObserveVars) {
    if (getenv(v)) {
      why = std::string(v) + " (derlemeyi gozlemleyen degisken)";
      return false;
    }
  }
  if (root_dir().empty()) {
    why = "kok dizin yok (HOME / LOCALAPPDATA / TULPAR_CACHE_DIR)";
    return false;
  }
  return true;
}

Key compute_key(Mode mode, const char *source, const char *source_path,
                const char *output_name) {
  Key k;
  if (!source) {
    k.why = "kaynak yok";
    return k;
  }
  std::string m;
  m.reserve(4096);
  m += "tulpar-onbellek 1\n";
  m += std::string("surum ") + tulpar::kVersion + "\n";
  {
    const std::string exe = self_exe();
    if (exe.empty()) {
      k.why = "surucu ikilisi bulunamadi";
      return k;
    }
    m += "surucu " + exe + " " + file_identity(exe) + "\n";
    for (const auto &lib : loaded_llvm_libs())
      if (lib != exe) m += "llvm " + lib + " " + file_identity(lib) + "\n";
  }
  {
    char *triple = LLVMGetDefaultTargetTriple();
    char *cpu = LLVMGetHostCPUName();
    m += std::string("hedef ") + (triple ? triple : "?") + " cpu=" + (cpu ? cpu : "?") + "\n";
    if (triple) LLVMDisposeMessage(triple);
    if (cpu) LLVMDisposeMessage(cpu);
  }
  m += std::string("dil ") + (tulpar::i18n::is_turkish_locale() ? "tr" : "en") + "\n";
  const std::string cwd = cwd_abs();
  m += "cwd " + cwd + "\n";
  if (mode == Mode::Build) {
    const std::string out = output_name ? output_name : "";
    m += "kip derle cikti=" + out + " mutlak=" + canonical(out) + "\n";
  } else {
    m += "kip calistir\n";
  }
  const std::string src(source);
  const std::string sp = source_path ? source_path : "";
  m += "kaynak " + sp + " mutlak=" + (sp.empty() ? "" : canonical(sp)) + " " +
       tulpar::sha256_hex(src) + "\n";
  {
    std::string toml;
    m += "toml " + (read_bytes("tulpar.toml", toml) ? tulpar::sha256_hex(toml) : std::string("yok")) +
         "\n";
  }

  ScanState st;
  // TULPAR_CACHE_SINAMA=tarama-yok: oz denetimin POZITIF KONTROLU
  // (tests/onbellek.sh). Import taramasi atlanir; yerel bir modulu olan
  // program derlenince kodgen o dosyayi okur, oz denetim "kapsanmayan girdi"
  // demeli ve sonuc YAYIMLANMAMALI — demezse oz denetim hicbir sey olcmuyor.
  const char *sinama = getenv("TULPAR_CACHE_SINAMA");
  if (sinama && strcmp(sinama, "tarama-yok") == 0) {
    m += "sinama tarama-yok\n";
  } else {
    // Ana dosyanin import'lari onun dizinine gore (kodgenin
    // llvm_backend_compile'i ile ayni; oyun geri bildirimi #6).
    scan_imports(src, tulpar::imports::dir_of(sp), 0, st);
    m += st.material;
  }

  // Yerel eklentiler (K303): bildirim + moduller ICERIK, arsivler KIMLIK.
  {
    const auto &exts = tulpar::ext::extensions();
    const int plat = (int)tulpar::ext::host_platform();
    for (const auto &e : exts) {
      std::string man;
      read_bytes(e.manifest_path, man);
      m += "eklenti " + e.name + " " + e.version + " koken=" + e.origin + " " + e.manifest_path +
           " " + tulpar::sha256_hex(man) + "\n";
      st.inputs.insert(canonical(e.manifest_path));
      for (const auto &mod : e.modules) {
        std::string body;
        const bool ok = read_bytes(mod.path, body);
        m += "  modul " + mod.name + " " + mod.path + " " +
             (ok ? tulpar::sha256_hex(body) : std::string("yok")) + "\n";
        st.inputs.insert(canonical(mod.path));
      }
      const auto &L = e.link[plat];
      m += fmt("  link var=%d grup=%d\n", L.present ? 1 : 0, L.group ? 1 : 0);
      for (const auto &d : L.lib_dirs) {
        m += "  dizin " + d + "\n";
        for (const auto &l : L.libs) {
          for (const std::string &c : {"lib" + l + ".a", l + ".lib", "lib" + l + ".so",
                                       "lib" + l + ".dylib", l + ".dll", "lib" + l + ".dll.a"}) {
            const std::string p = d + "/" + c;
            const std::string id = file_identity(p);
            if (id != "yok") m += "  arsiv " + p + " " + id + "\n";
          }
        }
      }
      for (const auto &l : L.libs) m += "  lib " + l + "\n";
      for (const auto &f : L.flags) {
        m += "  bayrak " + f + "\n";
        if ((f.find('/') != std::string::npos || f.find('\\') != std::string::npos) &&
            is_regular_file(f))
          m += "  bayrak-dosya " + f + " " + file_identity(f) + "\n";
      }
    }
  }

  // Baglama: surucu (TULPAR_CC ya da varsayilan) + ld + runtime/tame/OpenSSL
  // arsivleri, link satirinin ARAMA DIZINLERININ HEPSINDE (hangisinin
  // secilecegi mtime'a bagli: build_link_search_dirs; hepsini katmak guvenli).
  {
    const std::string drv = aot_cache_link_driver();
    m += "bag-surucu " + drv + "\n";
    size_t p = 0;
    while (p < drv.size()) {
      while (p < drv.size() && drv[p] == ' ') p++;
      size_t q = drv.find(' ', p);
      if (q == std::string::npos) q = drv.size();
      const std::string word = drv.substr(p, q - p);
      if (!word.empty() && word[0] != '-') {
        const std::string res = resolve_in_path(word);
        m += "  " + word + " -> " + (res.empty() ? std::string("?") : res + " " + file_identity(res)) +
             "\n";
      }
      p = q;
    }
    const std::string ld = resolve_in_path("ld");
    if (!ld.empty()) m += "ld " + ld + " " + file_identity(ld) + "\n";
    // Linux'ta surucu `-fuse-ld=lld|mold` tasiyabilir (aot_link_driver,
    // 2026-10-06): o bagliyicinin kimligi de anahtarda.
    for (const char *alt : {"lld", "mold"}) {
      if (drv.find(std::string("-fuse-ld=") + alt) == std::string::npos) continue;
      const std::string ap = resolve_in_path(std::string(alt) == "lld" ? "ld.lld" : "mold");
      m += std::string("ld.") + alt + " " + (ap.empty() ? std::string("?") : ap + " " + file_identity(ap)) + "\n";
    }
    for (const auto &d : parse_search_dirs(aot_cache_link_search_dirs())) {
      for (const char *a : {"libtulpar_runtime.a", "libtulpar_tame.a", "libssl.a", "libcrypto.a"}) {
        const std::string pth = d + "/" + a;
        const std::string id = file_identity(pth);
        if (id != "yok") m += "arsiv " + pth + " " + id + "\n";
      }
    }
  }

  // Ortam: DISLANMAYAN her TULPAR_* + baglayicinin okuduklari. Windows'ta ad
  // buyuk/kucuk harf duyarsiz (getenv de oyle) — buyuk harfe cevrilir.
  {
    std::vector<std::string> lines;
    std::vector<std::string> entries;
#if PLATFORM_WINDOWS
    // Surecin ortam blogu ("AD=DEGER\0...\0\0"): getenv/_putenv ile ayni
    // kaynak; CRT'nin _environ makrosuna (kati kipte gizlenebilir) baglanmaz.
    if (LPCH block = GetEnvironmentStringsA()) {
      for (const char *p = block; *p; p += strlen(p) + 1) entries.emplace_back(p);
      FreeEnvironmentStringsA(block);
    }
#else
    for (char **e = environ; e && *e; ++e) entries.emplace_back(*e);
#endif
    for (const std::string &entry : entries) {
      const size_t eqp = entry.find('=');
      if (eqp == std::string::npos || eqp == 0) continue;  // Windows'un "=C:" girdileri
      std::string name = entry.substr(0, eqp);
#if PLATFORM_WINDOWS
      name = upper(name);
#endif
      const std::string val = entry.substr(eqp + 1);
      if (name.compare(0, 7, "TULPAR_") == 0) {
        if (rule_match(kRuntimeRules, sizeof(kRuntimeRules) / sizeof(kRuntimeRules[0]), name))
          continue;
        if (rule_match(kControlRules, sizeof(kControlRules) / sizeof(kControlRules[0]), name))
          continue;
        lines.push_back(name + "=" + val);
      } else {
        for (const char *t : kToolchainEnv)
          if (name == t) lines.push_back(name + "=" + val);
      }
    }
    std::sort(lines.begin(), lines.end());
    for (const auto &l : lines) m += "env " + l + "\n";
  }

  k.material = std::move(m);
  k.hex = tulpar::sha256_hex(k.material).substr(0, 32);
  k.inputs.assign(st.inputs.begin(), st.inputs.end());
  k.ok = true;
  return k;
}

// ---- Oz denetim ---------------------------------------------------------------
void record_begin() {
  std::lock_guard<std::mutex> lk(g_rec_mu);
  g_recorded.clear();
  g_recording = true;
}

void note_input(const char *path) {
  if (!path || !*path) return;
  std::lock_guard<std::mutex> lk(g_rec_mu);
  if (!g_recording) return;
  g_recorded.push_back(canonical(path));
}

std::vector<std::string> record_end() {
  std::lock_guard<std::mutex> lk(g_rec_mu);
  g_recording = false;
  std::vector<std::string> out;
  out.swap(g_recorded);
  return out;
}

bool inputs_covered(const Key &k, const std::vector<std::string> &read, std::string &missing) {
  for (const auto &p : read) {
    if (!std::binary_search(k.inputs.begin(), k.inputs.end(), p)) {
      missing = p;
      return false;
    }
  }
  return true;
}

// ---- stderr yakalama ------------------------------------------------------------
bool StderrCapture::begin(const std::string &path) {
  if (active_ || g_cap_fd >= 0) return false;
  fflush(stderr);
#if PLATFORM_WINDOWS
  // _O_TEMPORARY: son tutamac kapaninca dosya silinir.
  const int fd = _open(path.c_str(), _O_RDWR | _O_CREAT | _O_TRUNC | _O_BINARY | _O_TEMPORARY,
                       _S_IREAD | _S_IWRITE);
#else
  const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
#endif
  if (fd < 0) return false;
#if !PLATFORM_WINDOWS
  unlink(path.c_str());  // adsiz gecici: cokmede de artik dosya kalmaz
#endif
  const int saved = CAP_DUP(2);
  if (saved < 0) {
    CAP_CLOSE(fd);
    return false;
  }
  if (CAP_DUP2(fd, 2) < 0) {
    CAP_CLOSE(fd);
    CAP_CLOSE(saved);
    return false;
  }
  g_cap_saved = saved;
  g_cap_fd = fd;
  if (!g_cap_atexit) {
    atexit(cap_on_exit);
    g_cap_atexit = true;
  }
  for (size_t i = 0; i < sizeof(kCapSignals) / sizeof(kCapSignals[0]); i++) {
    SigFn prev = signal(kCapSignals[i], cap_on_signal);
    g_cap_prev[i] = (prev == SIG_ERR) ? nullptr : prev;
  }
  active_ = true;
  return true;
}

std::string StderrCapture::end() {
  if (!active_) return "";
  active_ = false;
  fflush(stderr);
  for (size_t i = 0; i < sizeof(kCapSignals) / sizeof(kCapSignals[0]); i++)
    signal(kCapSignals[i], g_cap_prev[i] ? g_cap_prev[i] : SIG_DFL);
  std::string text;
  if (g_cap_fd >= 0) {
    CAP_LSEEK(g_cap_fd, 0, SEEK_SET);
    char buf[4096];
    for (;;) {
      const int n = (int)CAP_READ(g_cap_fd, buf, sizeof buf);
      if (n <= 0) break;
      text.append(buf, (size_t)n);
    }
  }
  cap_forward_raw();  // asil stderr'e yazar, yonlendirmeyi geri alir
  return text;
}

StderrCapture::~StderrCapture() {
  if (active_) end();
}

// ---- Calistirma deposu -----------------------------------------------------------
bool run_lookup(const Key &k, std::string &exe, std::string &diag) {
  if (!k.ok) return false;
  const std::string p = entry_path(k.hex);
  if (!is_regular_file(p)) return false;
  touch_now(p);
  exe = p;
  diag.clear();
  read_bytes(diag_path(k.hex), diag);
  return true;
}

std::string run_staging_base() {
  const std::string d = run_dir();
  if (!ensure_dir(d)) return "";
  return d + "/tmp-" + unique_suffix();
}

bool run_publish(const Key &k, const std::string &built, const std::string &diag,
                 std::string &final_exe) {
  if (!k.ok || !is_regular_file(built)) return false;
  final_exe = entry_path(k.hex);
  // Tani ONCE: ikili gorunur oldugu an tanisi da hazir olsun (ikili = isaret).
  const std::string dp = diag_path(k.hex);
  if (!diag.empty()) {
    if (!write_atomic(dp, diag)) return false;
  } else {
    remove(dp.c_str());
  }
#if PLATFORM_WINDOWS
  // Bayraksiz MoveFileEx hedef VARSA basarisiz olur: kosan bir .exe'nin (ya da
  // yarisi kazanan baska surecin ikilisinin) uzerine asla yazilmaz.
  if (MoveFileExA(built.c_str(), final_exe.c_str(), 0)) return true;
  const DWORD err = GetLastError();
  if (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS) {
    DeleteFileA(built.c_str());  // kilitliyse tmp-* temizligi alir
    touch_now(final_exe);
    return true;
  }
  return false;
#else
  // link(): hedef VARSA EEXIST — asla yerine koymaz (rename koyardi). Ayni
  // anahtar = ayni girdiler, yani yarisi kazananin ikilisi bizimkiyle esdeger.
  if (link(built.c_str(), final_exe.c_str()) == 0) {
    unlink(built.c_str());
    return true;
  }
  if (errno == EEXIST) {
    unlink(built.c_str());
    touch_now(final_exe);
    return true;
  }
  // Sabit baglanti desteklenmiyor (FAT, bazi FUSE): yalniz hedef YOKSA tasi.
  if (is_regular_file(final_exe)) {
    unlink(built.c_str());
    touch_now(final_exe);
    return true;
  }
  return rename(built.c_str(), final_exe.c_str()) == 0;
#endif
}

void evict() {
  const std::string d = run_dir();
  if (d.empty()) return;
  struct Ent {
    std::string path, hex;
    unsigned long long size;
    long long mtime;
  };
  std::vector<Ent> bins;
  unsigned long long total = 0;
  const long long now = (long long)time(nullptr);
  std::error_code ec;
  for (fs::directory_iterator it(fs::path(d), ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    const std::string p = it->path().string();
    struct stat st;
    if (stat(p.c_str(), &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG) continue;
    // Coken bir derlemenin artigi: bir saatten eskiyse sil.
    if (name.compare(0, 4, "tmp-") == 0) {
      if (now - (long long)st.st_mtime > 3600) {
        remove(p.c_str());
        continue;
      }
    }
    total += (unsigned long long)st.st_size;
    std::string hex = name;
    if (!std::string(kExeSuffix).empty() && hex.size() > 4 &&
        hex.compare(hex.size() - 4, 4, kExeSuffix) == 0)
      hex.resize(hex.size() - 4);
    if (is_hex32(hex) && name == hex + kExeSuffix)
      bins.push_back({p, hex, (unsigned long long)st.st_size, (long long)st.st_mtime});
  }
  const unsigned long long cap = cap_bytes();
  if (total <= cap) return;
  std::sort(bins.begin(), bins.end(), [](const Ent &a, const Ent &b) { return a.mtime < b.mtime; });
  const unsigned long long low = cap / 10 * 8;  // tavanin %80'ine in
  unsigned long long freed_n = 0, freed_b = 0;
  for (const auto &e : bins) {
    if (total <= low) break;
    // Son 60 s icinde kullanilan girdiye dokunma: baska bir surec onu tam
    // su an calistiriyor olabilir (isabet mtime'i tazeler).
    if (now - e.mtime < 60) continue;
    if (remove(e.path.c_str()) != 0) continue;  // Windows: kosan .exe silinmez
    total -= e.size;
    freed_n++;
    freed_b += e.size;
    struct stat st;
    const std::string dp = diag_path(e.hex);
    if (stat(dp.c_str(), &st) == 0) {
      total -= std::min<unsigned long long>(total, (unsigned long long)st.st_size);
      remove(dp.c_str());
    }
  }
  if (freed_n) report("tavan %llu MB asildi: %llu eski girdi silindi (%.1f MB)", cap >> 20, freed_n,
                      freed_b / 1048576.0);
}

// ---- Derleme dizini ---------------------------------------------------------------
namespace {
std::string build_entry(const std::string &exe_path) {
  const std::string d = build_dir();
  if (d.empty()) return "";
  return d + "/" + tulpar::sha256_hex(canonical(exe_path)).substr(0, 32);
}
}  // namespace

bool build_lookup(const Key &k, const std::string &exe_path, std::string &diag) {
  if (!k.ok) return false;
  const std::string e = build_entry(exe_path);
  if (e.empty()) return false;
  std::string body;
  if (!read_bytes(e, body)) return false;
  // Bicim: "tulpar-onbellek-derle 1\nanahtar <hex>\nikili <kimlik>\nyol <p>\n" + tani
  const std::string want_head = "tulpar-onbellek-derle 1\nanahtar " + k.hex + "\nikili " +
                                file_identity(exe_path) + "\nyol " + canonical(exe_path) + "\n";
  if (body.compare(0, want_head.size(), want_head) != 0) return false;
  diag = body.substr(want_head.size());
  return true;
}

void build_record(const Key &k, const std::string &exe_path, const std::string &diag) {
  if (!k.ok) return;
  const std::string d = build_dir();
  if (!ensure_dir(d)) return;
  const std::string id = file_identity(exe_path);
  if (id == "yok" || id == "dizin") return;
  write_atomic(build_entry(exe_path), "tulpar-onbellek-derle 1\nanahtar " + k.hex + "\nikili " + id +
                                          "\nyol " + canonical(exe_path) + "\n" + diag);
}

void build_forget(const std::string &exe_path) {
  const std::string e = build_entry(exe_path);
  if (!e.empty()) remove(e.c_str());
}

// ---- `tulpar cache` -----------------------------------------------------------------
namespace {
struct DirStat {
  unsigned long long files = 0, bytes = 0, entries = 0;
};
DirStat dir_stat(const std::string &d, bool count_bins) {
  DirStat s;
  std::error_code ec;
  for (fs::directory_iterator it(fs::path(d), ec), end; !ec && it != end; it.increment(ec)) {
    struct stat st;
    const std::string p = it->path().string();
    if (stat(p.c_str(), &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG) continue;
    s.files++;
    s.bytes += (unsigned long long)st.st_size;
    std::string name = it->path().filename().string();
    if (count_bins) {
      std::string hex = name;
      if (!std::string(kExeSuffix).empty() && hex.size() > 4) hex.resize(hex.size() - 4);
      if (is_hex32(hex) && name == hex + kExeSuffix) s.entries++;
    } else if (is_hex32(name)) {
      s.entries++;
    }
  }
  return s;
}

// Yalniz BIZIM adlandirdigimiz dosyalar silinir: <hex>[.exe], <hex>.diag,
// tmp-*, *.tmp-*. Dizinde baska bir sey varsa (kullanici koymus) dokunulmaz.
bool ours(const std::string &name) {
  if (name.compare(0, 4, "tmp-") == 0) return true;
  if (name.find(".tmp-") != std::string::npos) return true;
  std::string hex = name;
  if (hex.size() > 5 && hex.compare(hex.size() - 5, 5, ".diag") == 0) hex.resize(hex.size() - 5);
  else if (hex.size() > 4 && hex.compare(hex.size() - 4, 4, ".exe") == 0) hex.resize(hex.size() - 4);
  return is_hex32(hex);
}
}  // namespace

int cache_cmd_main(int argc, char **argv) {
  const char *sub = argc >= 3 ? argv[2] : "info";
  const std::string root = root_dir();
  if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "help") == 0) {
    printf("%s\n", tulpar::i18n::tr_en(
                       "Kullanim: tulpar cache [info|clean|dir]\n"
                       "  info   onbellegin yeri, girdi sayisi, boyutu ve tavani (varsayilan)\n"
                       "  clean  butun girdileri sil\n"
                       "  dir    kok dizini yaz\n"
                       "Ortam: TULPAR_AOT_NOCACHE=1 kapatir; TULPAR_CACHE_DIR kok dizin;\n"
                       "TULPAR_CACHE_MAX_MB tavan (varsayilan 512); TULPAR_CACHE_RAPOR=1 kararlar.",
                       "Usage: tulpar cache [info|clean|dir]\n"
                       "  info   location, entry count, size and cap (default)\n"
                       "  clean  delete every entry\n"
                       "  dir    print the root directory\n"
                       "Env: TULPAR_AOT_NOCACHE=1 disables; TULPAR_CACHE_DIR root;\n"
                       "TULPAR_CACHE_MAX_MB cap (default 512); TULPAR_CACHE_RAPOR=1 decisions."));
    return 0;
  }
  if (root.empty()) {
    fprintf(stderr, "%s\n", tulpar::i18n::tr_en(
                                "Hata: onbellek dizini belirlenemedi (HOME / LOCALAPPDATA / "
                                "TULPAR_CACHE_DIR).",
                                "Error: cannot determine the cache directory (HOME / LOCALAPPDATA "
                                "/ TULPAR_CACHE_DIR)."));
    return 1;
  }
  if (strcmp(sub, "dir") == 0) {
    printf("%s\n", root.c_str());
    return 0;
  }
  if (strcmp(sub, "info") == 0) {
    const DirStat r = dir_stat(run_dir(), true);
    const DirStat b = dir_stat(build_dir(), false);
    std::string why;
    const bool on = usable(why);
    printf("%s %s\n", tulpar::i18n::tr_en("dizin:", "directory:"), root.c_str());
    printf("%s %llu %s, %.1f MB (%s %llu MB)\n",
           tulpar::i18n::tr_en("calistirma:", "run:"), r.entries,
           tulpar::i18n::tr_en("ikili", "binaries"), r.bytes / 1048576.0,
           tulpar::i18n::tr_en("tavan", "cap"), cap_bytes() >> 20);
    printf("%s %llu %s\n", tulpar::i18n::tr_en("build:", "build:"), b.entries,
           tulpar::i18n::tr_en("cikti kaydi", "output records"));
    printf("%s %s\n", tulpar::i18n::tr_en("durum:", "state:"),
           on ? tulpar::i18n::tr_en("acik", "on")
              : (std::string(tulpar::i18n::tr_en("kapali — ", "off — ")) + why).c_str());
    return 0;
  }
  if (strcmp(sub, "clean") == 0) {
    unsigned long long n = 0, bytes = 0;
    for (const std::string &d : {run_dir(), build_dir()}) {
      std::error_code ec;
      std::vector<std::string> del;
      for (fs::directory_iterator it(fs::path(d), ec), end; !ec && it != end; it.increment(ec)) {
        if (ours(it->path().filename().string())) del.push_back(it->path().string());
      }
      for (const auto &p : del) {
        struct stat st;
        const unsigned long long sz = (stat(p.c_str(), &st) == 0) ? (unsigned long long)st.st_size : 0;
        if (remove(p.c_str()) == 0) {
          n++;
          bytes += sz;
        }
      }
    }
    printf("%s %llu %s, %.1f MB (%s)\n", tulpar::i18n::tr_en("silindi:", "removed:"), n,
           tulpar::i18n::tr_en("dosya", "files"), bytes / 1048576.0, root.c_str());
    return 0;
  }
  fprintf(stderr, "%s'%s' — tulpar cache [info|clean|dir]\n",
          tulpar::i18n::tr_en("Bilinmeyen alt komut: ", "Unknown subcommand: "), sub);
  return 2;
}

}  // namespace cache
}  // namespace tulpar
