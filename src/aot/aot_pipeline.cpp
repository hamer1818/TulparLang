#include "aot_pipeline.hpp"
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../common/localization.hpp"
#include "../common/platform.h"
#include "../pkg/manifest.hpp"  // [android] bölümü: paket adi/ikon/yon/surum
#include "../lsp/document_index.hpp"
#include "llvm_backend.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
// stat(): apk betiği / asset dizini var mı — HER platformda gerekir
// (MinGW/MSVC de sys/stat.h sağlar; sys/wait.h'nin aksine Windows'ta var).
#include <sys/stat.h>
#ifndef S_ISDIR  // MSVC S_ISDIR makrosunu tanımlamaz
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#if !PLATFORM_WINDOWS
#include <csignal>   // SIGINT
#include <vector>
#include <sys/wait.h> // WIFSIGNALED / WTERMSIG on system() status
#include <dirent.h>  // find_android_ndk: NDK klasör taraması
#include <unistd.h>  // find_android_ndk: access() ile llvm-ar denetimi
#endif
#include <chrono>
#include <string>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#if PLATFORM_MACOS
  #include <mach-o/dyld.h>
#endif

// Optional per-phase wallclock breakdown. Enable with TULPAR_AOT_TIME=1.
static int aot_timing_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *e = getenv("TULPAR_AOT_TIME");
    cached = (e && *e && *e != '0') ? 1 : 0;
  }
  return cached;
}

// Per-phase `[AOT] …` progress chatter. Off by default so `tulpar build`
// prints only the final "Successfully created" confirmation (and any
// errors); set TULPAR_AOT_VERBOSE=1 to see the parse/codegen/link steps.
// The silent run path (`tulpar foo.tpr`) builds its own quiet backend and
// never reaches these prints.
static int aot_verbose_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *e = getenv("TULPAR_AOT_VERBOSE");
    cached = (e && *e && *e != '0') ? 1 : 0;
  }
  return cached;
}
#define AOT_PROGRESS(...) do { if (aot_verbose_enabled()) printf(__VA_ARGS__); } while (0)

struct AOTPhaseTimer {
  const char *name;
  std::chrono::steady_clock::time_point start;
  AOTPhaseTimer(const char *n) : name(n) {
    if (aot_timing_enabled()) start = std::chrono::steady_clock::now();
  }
  ~AOTPhaseTimer() {
    if (!aot_timing_enabled()) return;
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    fprintf(stderr, "[AOT-TIME] %-12s %4lldms\n", name, (long long)ms);
  }
};

// Platform-specific link flags / temp paths.
//
// `AOT_LINK_LIB_FLAGS` only carries platform-fixed switches and the
// link line itself; library search paths (`-L`) are computed at
// runtime in build_link_search_dirs() so installed `tulpar.exe`
// finds `libtulpar_runtime.a` next to itself, while developer
// builds still pick it up from `./build-<platform>/`.
//
// Note on `--export-all-symbols` / `-rdynamic`:
// Tulpar's `call(name)` builtin uses dlsym/GetProcAddress to dispatch to
// user-defined functions by name (wings/router handlers, lib/test.tpr
// runners, ...). By default an executable does NOT export its own internal
// symbols, so without these flags the lookup silently fails on the actual
// function and the runtime hits its "Function not found" path. We pay the
// minor binary-size cost to make `call()` work uniformly across Windows
// (MinGW) and Linux/macOS.
//
// Note on `-lssl -lcrypto`:
// `libtulpar_runtime.a` ships `http_fetch.cpp.o` which references OpenSSL
// (`SSL_CTX_new`, `OPENSSL_init_ssl`, ...) when the driver was built with
// `TULPAR_HAS_TLS=1`. Static archives don't bake their own deps in, so the
// AOT-pipeline's hand-rolled clang line has to forward those flags itself
// or every user binary fails with "undefined reference to OPENSSL_init_ssl"
// at link time. Linux CI hit this for the entire examples/ suite once the
// runner started actually invoking `tulpar --aot`. The flag is gated on
// `TULPAR_HAS_TLS` so a TLS-disabled build (no OpenSSL on the host) still
// produces a working linker line.
#if defined(TULPAR_HAS_TLS)
  #if PLATFORM_WINDOWS
    // MSYS2's static libcrypto.a pulls in CertFindCertificateInStore,
    // CertCloseStore, CertOpenSystemStoreW (winstore_store provider) +
    // raw socket calls (getsockopt, WSA*) on top of the usual ws2_32
    // surface. Without -lcrypt32 the produced exe fails to link with
    // a parade of `undefined reference to __imp_CertFindCertificateInStore`.
    // ws2_32 is already in the Windows base flags below, so we don't
    // duplicate it here.
    #define AOT_TLS_LINK_FLAGS " -lssl -lcrypto -lcrypt32"
  #else
    #define AOT_TLS_LINK_FLAGS " -lssl -lcrypto"
  #endif
#else
  #define AOT_TLS_LINK_FLAGS ""
#endif

// Note on Windows static linking:
// User-produced AOT binaries pulled `libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
// and `libwinpthread-1.dll` from MinGW out of the linker. None of these
// ship with stock Windows, so a binary built on a developer's machine and
// copied to a fresh Win10/11 install would fail at launch with
// STATUS_DLL_NOT_FOUND (0xC0000135) — defeating the "tulpar build foo.tpr
// produces a standalone exe" promise. `-static` switches the link mode
// for the whole AOT line (every -l<name> resolves to its `.a` form);
// `-static-libgcc -static-libstdc++` are belt-and-suspenders so the GCC
// support libs go in even if a downstream change reintroduces a -Bdynamic
// segment. ws2_32 / kernel32 etc. only have import-lib form, so `-static`
// happily pulls them from the same `.a` files MinGW always uses for them.
// Trade-off: roughly +3 MB per produced exe (from ~3 MB to ~5–6 MB).
// Worth it: the user can now zip/email a single .exe to any 64-bit
// Windows box and have it run.
#if PLATFORM_WINDOWS
  // Link order matters under MinGW's GNU ld: an archive only resolves
  // symbols requested by libraries that came BEFORE it on the command
  // line. libssl / libcrypto pull in WSA*, getsockopt, CertFind*, etc.,
  // so they must appear LEFT of `-lws2_32 -lcrypt32`. The pre-PR-#92
  // order had ws2_32 before libssl and produced a wall of
  // `undefined reference to __imp_WSAGetLastError` once OpenSSL was
  // available at build time on Windows.
  #define AOT_LINK_LIB_FLAGS \
      "-Wl,--export-all-symbols " \
      "-static -static-libgcc -static-libstdc++ " \
      "-ltulpar_runtime" AOT_TLS_LINK_FLAGS \
      " -lws2_32 -lwsock32"
  #define AOT_LINK_PIE_FLAG ""
  #define AOT_EXE_SUFFIX ".exe"
  #define AOT_TMP_RUN_BASE ".tulpar_run"
#else
  #define AOT_LINK_LIB_FLAGS \
      "-rdynamic " \
      "-ltulpar_runtime -lm -lpthread -ldl" AOT_TLS_LINK_FLAGS
  #define AOT_LINK_PIE_FLAG "-no-pie"
  #define AOT_EXE_SUFFIX ""
  #define AOT_TMP_RUN_BASE "/tmp/.tulpar_run"
#endif

// Resolve the directory holding the running tulpar binary. Used so
// AOT-compiled programs link against `libtulpar_runtime.a` shipped
// next to `tulpar.exe` (or `tulpar`) by the installer, rather than
// relying on a hard-coded `./build-windows/` style fallback.
//
// Empty string means "couldn't figure it out" — the caller still
// adds dev-tree fallbacks so a fresh-from-`build.sh` checkout works.
static std::string get_executable_dir() {
#if PLATFORM_WINDOWS
  char buf[MAX_PATH];
  DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return "";
  std::string p(buf, len);
  size_t slash = p.find_last_of("\\/");
  return (slash == std::string::npos) ? "" : p.substr(0, slash);
#elif PLATFORM_MACOS
  char buf[1024];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) return "";
  std::string p(buf);
  size_t slash = p.find_last_of('/');
  return (slash == std::string::npos) ? "" : p.substr(0, slash);
#else
  char buf[1024];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) return "";
  buf[len] = '\0';
  std::string p(buf);
  size_t slash = p.find_last_of('/');
  return (slash == std::string::npos) ? "" : p.substr(0, slash);
#endif
}

// Build the `-L<dir>` switches passed to clang++ at link time.
//
// Order matters: clang searches the first path that contains the
// requested library first, so the installer location should win
// over the dev-tree fallbacks if both happen to be present.
static std::string build_link_search_dirs() {
  std::string out;
  auto add = [&](const std::string &dir) {
    if (dir.empty()) return;
    out += "-L\"";
    out += dir;
    out += "\" ";
  };

  // Geliştirme ağacındaki adaylar. `./build.sh` bunları köke kopyalıyor ama
  // doğrudan `cmake --build build-linux` kullanan (yani artımlı derleyen)
  // biri için kökteki kopya BAYAT kalıyor ve exe dizini önce arandığı için
  // taze arşivi GÖLGELİYOR. Sonuç sinsiydi: yeni eklenen bir builtin
  // "undefined reference" ile bağlanamıyor, oysa derleme başarılı.
  //
  // Çözüm: exe dizinindeki arşiv geliştirme ağacındakinden ESKİYSE, arama
  // sırası ters çevriliyor. Kurulu bir tulpar'da geliştirme dizini zaten
  // yok, yani orada davranış değişmiyor.
  // Adaylar hem ÇALIŞMA DİZİNİNE hem EXE DİZİNİNE göre üretiliyor.
  //
  // Yalnız CWD'ye göre üretmek sinsi bir hataydı: `packages/<ad>` altından
  // koşan testler (paketin `import "<ad>"`i çalışma dizinine göre çözüldüğü
  // için oradan koşmak ZORUNDA) `./build-linux`i `packages/<ad>/build-linux`
  // diye arıyor, bulamıyor ve "libtulpar_runtime.a mevcut mu?" diyip
  // düşüyorlardı. Geliştirici makinesinde bu görünmüyordu, çünkü orada
  // arşivin kökte bir kopyası duruyor ve exe dizini onu buluyor; CI'da o
  // kopya yoksa kırmızıya dönüyordu — nitekim döndü.
  std::string exe_dir = get_executable_dir();
  std::vector<std::string> dev_dirs;
  auto add_dev = [&](const char *rel) {
    dev_dirs.push_back(std::string("./") + rel);
    if (!exe_dir.empty()) dev_dirs.push_back(exe_dir + "/" + rel);
  };
#if PLATFORM_WINDOWS
  add_dev("build-windows");
  add_dev("build-windows/Release");
#elif PLATFORM_MACOS
  add_dev("build-macos");
#else
  add_dev("build-linux");
#endif
  add_dev("build");
  bool dev_first = false;
  if (!exe_dir.empty()) {
    struct stat se;
    if (stat((exe_dir + "/libtulpar_runtime.a").c_str(), &se) == 0) {
      for (const auto &d : dev_dirs) {
        struct stat sd;
        if (stat((d + "/libtulpar_runtime.a").c_str(), &sd) == 0 &&
            sd.st_mtime > se.st_mtime) {
          dev_first = true;
          break;
        }
      }
    }
  }

  if (dev_first) {
    for (const auto &d : dev_dirs) add(d);
  }
  if (!exe_dir.empty()) {
    add(exe_dir);          // installer drops libtulpar_runtime.a here
    add(exe_dir + "/lib"); // package-manager-style /lib subdir variant
  }
  if (!dev_first) {
    for (const auto &d : dev_dirs) add(d);
  }

  // Last resort override — operators with custom layouts can set
  // TULPAR_RUNTIME_DIR=/some/path to point at the runtime archive
  // without recompiling.
  if (const char *env = getenv("TULPAR_RUNTIME_DIR"); env && *env) {
    add(env);
  }

  return out;
}

// Optional extra flags spliced into the final clang++ AOT link command. Set
// TULPAR_AOT_LINK_FLAGS to forward switches to the link step — e.g.
// "-fsanitize=address" to leak/UB-check the AOT'd binary against an ASan-built
// libtulpar_runtime.a. Returns a leading-space-prefixed string (or empty).
static std::string aot_extra_link_flags() {
  const char *e = getenv("TULPAR_AOT_LINK_FLAGS");
  if (!e || !*e) return "";
  return std::string(" ") + e;
}

// --- Web hedefi (wasm32-unknown-emscripten) ---------------------------------
// `tulpar build --target=web` main.cpp'den bu bayrağı kurar. Codegen'e
// backend->target_web olarak taşınır (VMValue ABI'sini sret+byval'a çevirir
// ve emit'te wasm32 triple'ı seçer); link adımı clang++ yerine em++ ile
// wasm/dist arşivlerine (build_tame_web.sh çıktıları) yapılır.
static int g_target_web = 0;

void aot_set_target_web(int enable) {
  g_target_web = enable ? 1 : 0;
  // Backend'e de kur: declare_runtime_functions llvm_backend_create'in
  // İÇİNDE koşar, tip şekilleri (sret vs SysV) o anda belirlenir.
  llvm_backend_set_target_web(g_target_web);
}

// --- Android hedefi (aarch64/x86_64-linux-android) --------------------------
// `tulpar build --target=android` main.cpp'den bu bayrağı kurar. Aynı
// derlenmiş modülden İKİ obje emit edilir (arm64-v8a = gerçek cihaz,
// x86_64 = Android Studio emülatörü), her biri NDK clang++'ıyla kendi
// libtulpargame.so'suna linklenir ve <out>_apk/ altına APK staging düzeni
// (lib/<abi>/ + AndroidManifest.xml) yazılır. İmzalı .apk üretimi
// android/package_apk.sh'nin işi. VMValue ABI'si Linux'taki {i64,i64}
// coercion'ıyla aynı kalır (AAPCS64 ve android-x86_64 SysV bunu bekler),
// o yüzden web'in aksine backend'e ayrı bir bayrak taşınmaz.
static int g_target_android = 0;

void aot_set_target_android(int enable) { g_target_android = enable ? 1 : 0; }

// `--apk`: staging'in ardından android/package_apk.sh'yi de çalıştır → tek
// komutta imzalı, kurulabilir .apk. Betik WSL/Windows-interop ayrıntılarını
// zaten çözüyor; driver yalnızca onu bulup çağırır.
static int g_android_apk = 0;
// `--aab`: .apk yerine Play Store'a yüklenebilir .aab üret (android/package_aab.sh).
static int g_android_aab = 0;

void aot_set_android_apk(int enable) { g_android_apk = enable ? 1 : 0; }
void aot_set_android_aab(int enable) { g_android_aab = enable ? 1 : 0; }

// Paketleme betiğini bul: TULPAR_ANDROID_TOOLS ortam değişkeni (dizin), sonra
// tulpar'ın yanı (kurulum düzeni), sonra çalışma dizini (dev-tree). `name` =
// "package_apk.sh" ya da "package_aab.sh".
static std::string find_android_script(const char *name) {
  auto ok = [](const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? p : std::string();
  };
  if (const char *env = getenv("TULPAR_ANDROID_TOOLS"); env && *env) {
    std::string p = ok(std::string(env) + "/" + name);
    if (!p.empty()) return p;
  }
  std::string exe_dir = get_executable_dir();
  if (!exe_dir.empty()) {
    std::string p = ok(exe_dir + "/android/" + name);
    if (!p.empty()) return p;
  }
  return ok(std::string("./android/") + name);
}

// NDK kökü. Boş dönerse çağıran hata basar. Android hedefi şimdilik yalnız
// Linux/macOS host'tan derleniyor (Windows host'ta VMValue codegen'i sret
// ABI'sine kayar — NDK arşivleriyle uyumsuz olur), Windows'ta boş döner.
//
// ARAMA android/build_tame_android.sh ile AYNI yerlere bakmalı. Eskiden ikisi
// de yalnız `~/Android/android-ndk-*`'a (tek başına indirilen NDK) bakıyordu;
// Android Studio ise NDK'yı SDK'nın İÇİNE, `<sdk>/ndk/<sürüm>` altına koyuyor.
// Sonuç ölçüldü: makinede çalışır bir NDK dururken hem betik hem sürücü
// "NDK bulunamadı" diyordu. Bir kural iki yerde yazılınca ikisi de aynı
// eksikle yaşıyor — burada tam olarak bu oldu.
static bool ndk_usable(const std::string &dir) {
  if (dir.empty()) return false;
  // Araç zinciri klasörü ana bilgisayara göre adlanıyor.
#if PLATFORM_MACOS
  const char *host = "darwin-x86_64";
#else
  const char *host = "linux-x86_64";
#endif
  std::string ar = dir + "/toolchains/llvm/prebuilt/" + host + "/bin/llvm-ar";
  return access(ar.c_str(), X_OK) == 0;
}

// `base` altındaki EN YENİ alt klasör (sürüm dizinleri: 27.x, 30.x ...).
// `prefix` boşsa her alt klasör aday; doluysa yalnız o önekle başlayanlar.
static std::string newest_subdir(const std::string &base, const char *prefix) {
  DIR *d = opendir(base.c_str());
  if (!d) return "";
  std::string best;
  int best_major = -1;
  while (struct dirent *e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    if (prefix && *prefix && strncmp(e->d_name, prefix, strlen(prefix)) != 0)
      continue;
    std::string cand = base + "/" + e->d_name;
    if (!ndk_usable(cand)) continue;
    // SÜRÜM sırası, sözlük sırası değil: düz karşılaştırma "9.x"i "30.x"in
    // üstüne koyardı. İlk sayı yeter — NDK sürümleri "30.0.157..." biçiminde.
    const char *p = e->d_name;
    while (*p && (*p < '0' || *p > '9')) p++;   // "android-ndk-27b" önekini atla
    int major = 0;
    for (; *p >= '0' && *p <= '9'; p++) major = major * 10 + (*p - '0');
    if (major > best_major || (major == best_major && cand > best)) {
      best_major = major;
      best = cand;
    }
  }
  closedir(d);
  return best;
}

static std::string find_android_ndk() {
#if PLATFORM_WINDOWS
  return "";
#else
  if (const char *env = getenv("TULPAR_ANDROID_NDK"); env && *env) {
    if (ndk_usable(env)) return env;
  }
  std::vector<std::pair<std::string, const char *>> spots;
  for (const char *var : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
    if (const char *sdk = getenv(var); sdk && *sdk)
      spots.push_back({std::string(sdk) + "/ndk", ""});
  }
  const char *home = getenv("HOME");
  if (home && *home) {
    std::string h(home);
    spots.push_back({h + "/Android/Sdk/ndk", ""});        // Android Studio (Linux)
    spots.push_back({h + "/Library/Android/sdk/ndk", ""}); // Android Studio (macOS)
    spots.push_back({h + "/Android", "android-ndk-"});     // tek başına indirme
  }
  for (const auto &sp : spots) {
    std::string got = newest_subdir(sp.first, sp.second);
    if (!got.empty()) return got;
  }
  return "";
#endif
}

// android/dist/<abi> arama yolu: dev-tree, exe yanı, TULPAR_ANDROID_LIB_DIR.
static std::string build_android_link_search_dirs(const char *abi) {
  std::string out;
  auto add = [&](const std::string &dir) {
    if (dir.empty()) return;
    out += "-L\"";
    out += dir;
    out += "\" ";
  };
  std::string exe_dir = get_executable_dir();
  if (!exe_dir.empty()) add(exe_dir + "/android/dist/" + abi);
  add(std::string("./android/dist/") + abi);
  if (const char *env = getenv("TULPAR_ANDROID_LIB_DIR"); env && *env)
    add(std::string(env) + "/" + abi);
  return out;
}

// Uygulama kimliği — tulpar.toml [android] bölümünden doldurulur; her alanın
// tarihi varsayılanı vardır, yani toml'suz davranış birebir aynı kalır.
struct AndroidAppConfig {
  std::string package = "dev.tulparlang.game";
  std::string label;                       // boş → çıktı taban adı
  std::string orientation = "landscape";   // landscape | portrait | sensor
  std::string version_code = "1";
  std::string version_name = "1.0";
  bool has_icon = false;                   // res/mipmap/ic_launcher.png kondu
  std::string splash_color = "#10121A";    // açılış/splash arka planı (koyu lacivert)
};

// "#RRGGBB" doğrula (aksi halde varsayılana dön) — aapt2 color kaynağına
// düz gireceği için basit bir #hex kontrolü yeterli.
static bool valid_hex_color(const std::string &s) {
  if (s.size() != 7 || s[0] != '#') return false;
  for (size_t i = 1; i < s.size(); i++) {
    char c = s[i];
    bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    if (!hex) return false;
  }
  return true;
}

// Manifest metin değerleri için minimal XML kaçışı (label kullanıcıdan gelir).
static std::string xml_escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
  return out;
}

// Staging dizinine minimal NativeActivity manifesti yazar. hasCode=false:
// APK'da hiç Java/DEX yok — NativeActivity framework'ten gelir, oyun
// libtulpargame.so'dur (meta-data android.app.lib_name).
static void write_android_manifest(const std::string &stage,
                                   const AndroidAppConfig &cfg) {
  std::string path = stage + "/AndroidManifest.xml";
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  std::string icon_attr;
  // İkon varsa hem klasik hem yuvarlak (roundIcon) slotu — ikisi de adaptive
  // ic_launcher'ı gösterir (res/mipmap-anydpi-v26/ic_launcher.xml).
  if (cfg.has_icon)
    icon_attr = " android:icon=\"@mipmap/ic_launcher\""
                " android:roundIcon=\"@mipmap/ic_launcher\"";
  fprintf(f,
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
          "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
          "    package=\"%s\"\n"
          "    android:versionCode=\"%s\" android:versionName=\"%s\">\n"
          "  <uses-sdk android:minSdkVersion=\"26\" "
          "android:targetSdkVersion=\"34\"/>\n"
          // titret()/vibrate() için; normal izin — kurulumda otomatik verilir.
          "  <uses-permission android:name=\"android.permission.VIBRATE\"/>\n"
          // İnternet: online skor tablosu / http_get gibi ağ çağrıları için
          // (normal izin, kurulumda otomatik verilir; kullanılmasa da zararsız).
          "  <uses-permission android:name=\"android.permission.INTERNET\"/>\n"
          "  <application android:label=\"%s\" android:hasCode=\"false\"\n"
          "      android:theme=\"@style/TulparSplash\"\n"
          "      android:extractNativeLibs=\"true\"%s>\n"
          "    <activity android:name=\"android.app.NativeActivity\"\n"
          "        android:theme=\"@style/TulparSplash\"\n"
          "        android:configChanges=\"orientation|keyboardHidden|screenSize\"\n"
          "        android:screenOrientation=\"%s\"\n"
          "        android:exported=\"true\">\n"
          "      <meta-data android:name=\"android.app.lib_name\" "
          "android:value=\"tulpargame\"/>\n"
          "      <intent-filter>\n"
          "        <action android:name=\"android.intent.action.MAIN\"/>\n"
          "        <category android:name=\"android.intent.category.LAUNCHER\"/>\n"
          "      </intent-filter>\n"
          "    </activity>\n"
          "  </application>\n"
          "</manifest>\n",
          xml_escape(cfg.package).c_str(), xml_escape(cfg.version_code).c_str(),
          xml_escape(cfg.version_name).c_str(), xml_escape(cfg.label).c_str(),
          icon_attr.c_str(), xml_escape(cfg.orientation).c_str());
  fclose(f);
}

// Splash teması + renk kaynakları + (ikon varsa) adaptive-icon yaz. Bunlar
// res/ altına gider; package_apk.sh res/ görürse `aapt2 compile --dir res` ile
// derler. Splash: activity teması windowBackground'ı splash rengine boyar →
// soğuk başlatmadaki SİYAH flaş yerine markalı arka plan (G3). minSdk 26 olduğu
// için adaptive-icon (res/mipmap-anydpi-v26) her cihazda geçerli.
static void write_android_resources(const std::string &stage,
                                    const AndroidAppConfig &cfg) {
  std::string color = valid_hex_color(cfg.splash_color) ? cfg.splash_color
                                                        : std::string("#10121A");
  std::string vdir = stage + "/res/values";
  std::string mk = "mkdir -p \"" + vdir + "\"";
  if (system(mk.c_str()) != 0) return;

  // colors.xml — splash + adaptive-icon arka planı aynı renk.
  {
    std::string p = vdir + "/colors.xml";
    if (FILE *f = fopen(p.c_str(), "wb")) {
      fprintf(f,
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n"
              "  <color name=\"tulpar_splash_bg\">%s</color>\n"
              "</resources>\n",
              color.c_str());
      fclose(f);
    }
  }
  // styles.xml — tam ekran, başlıksız; windowBackground = splash rengi.
  {
    std::string p = vdir + "/styles.xml";
    if (FILE *f = fopen(p.c_str(), "wb")) {
      fprintf(f,
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n"
              "  <style name=\"TulparSplash\" "
              "parent=\"@android:style/Theme.NoTitleBar.Fullscreen\">\n"
              "    <item name=\"android:windowBackground\">"
              "@color/tulpar_splash_bg</item>\n"
              "  </style>\n</resources>\n");
      fclose(f);
    }
  }
  // Adaptive icon (ikon varsa): ön plan = kullanıcının PNG'si, arka plan =
  // splash rengi. PNG'yi foreground adı (ic_fg) ile de kopyala — ic_launcher
  // API26+'da bu XML'e çözülür, foreground kendine referans veremez.
  if (cfg.has_icon) {
    std::string cp = "cp \"" + stage + "/res/mipmap/ic_launcher.png\" \"" +
                     stage + "/res/mipmap/ic_fg.png\" 2>/dev/null";
    (void)!system(cp.c_str());
    std::string adir = stage + "/res/mipmap-anydpi-v26";
    std::string mk2 = "mkdir -p \"" + adir + "\"";
    if (system(mk2.c_str()) == 0) {
      std::string p = adir + "/ic_launcher.xml";
      if (FILE *f = fopen(p.c_str(), "wb")) {
        fprintf(f,
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<adaptive-icon "
                "xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
                "  <background android:drawable=\"@color/tulpar_splash_bg\"/>\n"
                "  <foreground android:drawable=\"@mipmap/ic_fg\"/>\n"
                "</adaptive-icon>\n");
        fclose(f);
      }
    }
  }
}

// Oyun asset'lerini (ses/sprite/font dosyaları) staging assets/'ine kopyala.
// package_apk.sh bunu `aapt2 link -A` ile APK'ya koyar; raylib Android'de
// AAssetManager üzerinden AYNI göreceli yollarla okur — yani masaüstünde
// çalışan `load_texture("top.png")` cihazda değişiklik istemez. Kaynak dizin:
// TULPAR_ANDROID_ASSETS env'i (web'in TULPAR_WEB_ASSETS'inin eşi) ya da
// tulpar.toml [android] assets anahtarı.
static void stage_android_assets(const std::string &stage,
                                 const std::string &dir) {
  if (dir.empty()) return;
  struct stat st;
  if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    fprintf(stderr,
            tulpar::i18n::tr_en(
                "[AOT] [android] assets dizini bulunamadi: %s (atlaniyor)\n",
                "[AOT] [android] assets dir not found: %s (skipping)\n"),
            dir.c_str());
    return;
  }
  // Montaj noktası = dizinin YAZILDIĞI göreceli yol (web'in --preload-file
  // davranışıyla aynı): assets="assets" iken oyun "assets/top.png" okur →
  // APK'da assets/assets/top.png. Mutlak yol verilirse taban adı kullanılır
  // (göreceli referans ancak öyle eşleşebilir).
  std::string mount = dir;
  while (!mount.empty() && mount.back() == '/') mount.pop_back();
  if (!mount.empty() && mount[0] == '/') {
    size_t slash = mount.find_last_of('/');
    mount = mount.substr(slash + 1);
  }
  std::string cmd = "mkdir -p \"" + stage + "/assets/" + mount +
                    "\" && cp -a \"" + dir + "/.\" \"" + stage + "/assets/" +
                    mount + "/\"";
  if (system(cmd.c_str()) != 0) {
    fprintf(stderr,
            tulpar::i18n::tr_en(
                "[AOT] [android] assets kopyalanamadi: %s\n",
                "[AOT] [android] assets copy failed: %s\n"),
            dir.c_str());
    return;
  }
  AOT_PROGRESS("[AOT] Assets staged: %s -> %s/assets/%s/\n", dir.c_str(),
               stage.c_str(), mount.c_str());
}

// tulpar.toml [android] bölümünü oku (varsa) + ikon ve asset'leri staging'e
// kopyala. Dönen cfg her durumda geçerli varsayılanlarla doludur.
static AndroidAppConfig load_android_app_config(const std::string &stage,
                                                const char *fallback_label) {
  AndroidAppConfig cfg;
  cfg.label = fallback_label;
  struct stat st;
  if (stat("tulpar.toml", &st) != 0) {
    // toml yok — asset'ler yine env ile verilebilir.
    if (const char *env = getenv("TULPAR_ANDROID_ASSETS"); env && *env)
      stage_android_assets(stage, env);
    return cfg;
  }
  tulpar::Manifest m;
  std::string err;
  if (!tulpar::manifest_load("tulpar.toml", m, err)) {
    fprintf(stderr, "[AOT] tulpar.toml: %s\n", err.c_str());
    if (const char *env = getenv("TULPAR_ANDROID_ASSETS"); env && *env)
      stage_android_assets(stage, env);
    return cfg;
  }
  {
    // env > toml (web hedefiyle aynı öncelik).
    const char *env = getenv("TULPAR_ANDROID_ASSETS");
    if (env && *env) stage_android_assets(stage, env);
    else stage_android_assets(stage, m.android_assets);
  }
  if (!m.android_package.empty()) cfg.package = m.android_package;
  if (!m.android_label.empty()) cfg.label = m.android_label;
  if (!m.android_version_code.empty()) cfg.version_code = m.android_version_code;
  if (!m.android_version_name.empty()) cfg.version_name = m.android_version_name;
  if (!m.android_splash_color.empty()) {
    if (valid_hex_color(m.android_splash_color)) {
      cfg.splash_color = m.android_splash_color;
    } else {
      fprintf(stderr, "%s\n",
              tulpar::i18n::tr_en(
                  "[AOT] [android] splash_color '#RRGGBB' olmali; varsayilan "
                  "kullaniliyor.",
                  "[AOT] [android] splash_color must be '#RRGGBB'; using the "
                  "default."));
    }
  }
  if (!m.android_orientation.empty()) {
    if (m.android_orientation == "landscape" ||
        m.android_orientation == "portrait" ||
        m.android_orientation == "sensor") {
      cfg.orientation = m.android_orientation;
    } else {
      fprintf(stderr, "%s\n",
              tulpar::i18n::tr_en(
                  "[AOT] [android] orientation landscape|portrait|sensor "
                  "olmali; landscape kullaniliyor.",
                  "[AOT] [android] orientation must be "
                  "landscape|portrait|sensor; using landscape."));
    }
  }
  if (!m.android_icon.empty()) {
    FILE *src = fopen(m.android_icon.c_str(), "rb");
    if (!src) {
      fprintf(stderr,
              tulpar::i18n::tr_en(
                  "[AOT] [android] icon bulunamadi: %s (varsayilan ikon "
                  "kullanilacak)\n",
                  "[AOT] [android] icon not found: %s (falling back to the "
                  "default icon)\n"),
              m.android_icon.c_str());
    } else {
      std::string dir = stage + "/res/mipmap";
      std::string mk = "mkdir -p \"" + dir + "\"";
      if (system(mk.c_str()) != 0) { /* fopen asagida zaten hata verir */ }
      std::string dst_path = dir + "/ic_launcher.png";
      FILE *dst = fopen(dst_path.c_str(), "wb");
      if (dst) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
        fclose(dst);
        cfg.has_icon = true;
      }
      fclose(src);
    }
  }
  return cfg;
}

// em++'ın -L arama yolları: dev-tree wasm/dist + tulpar exe'sinin yanı +
// TULPAR_WEB_LIB_DIR ortam değişkeni.
static std::string build_web_link_search_dirs() {
  std::string out;
  auto add = [&](const std::string &dir) {
    if (dir.empty()) return;
    out += "-L\"";
    out += dir;
    out += "\" ";
  };
  std::string exe_dir = get_executable_dir();
  if (!exe_dir.empty()) {
    add(exe_dir + "/wasm/dist"); // kurulum yanına kopyalanmış layout
    add(exe_dir + "/lib");
  }
  add("./wasm/dist"); // dev-tree (repo kökünden çalıştırma)
  if (const char *env = getenv("TULPAR_WEB_LIB_DIR"); env && *env) add(env);
  return out;
}

// ARŞİV TAZELİĞİ: wasm/dist ve android/dist elle derleniyor (bu makinede
// emsdk/NDK olmayabilir, CI'da hiç yok). `tame_impl.c`'ye yeni bir sembol
// eklenip arşiv tazelenmeyince link `undefined symbol: aot_tm_...` diyor ve
// bu, sorunun ne olduğunu SÖYLEMEYEN bir mesaj: eksik olan kodun kendisi
// değil, arşivin bayatlığı. Aynı hataya en az üç kez çarpıldı.
//
// Kaynak arşivden yeniyse link ÖNCESİ söyleniyor. Uyarı, hata değil: arşiv
// bayat olsa bile içinde gereken semboller varsa link tutar ve derlemeyi
// durdurmak gereksiz olurdu.
static void warn_if_prebuilt_archive_stale(const char *dist_dir,
                                           const char *rebuild_cmd) {
  static const char *srcs[] = {
      "runtime/tame_impl.c", "runtime/tame_bindings.cpp",
      "src/vm/runtime_bindings.cpp", "src/vm/vm.cpp",
  };
  struct stat as;
  time_t oldest_archive = 0;
  bool found = false;
  DIR *d = opendir(dist_dir);
  if (!d) return;  // arşiv yoksa link zaten kendi hatasını verir
  struct dirent *ent;
  while ((ent = readdir(d)) != nullptr) {
    const char *n = ent->d_name;
    size_t ln = strlen(n);
    if (ln < 3 || strcmp(n + ln - 2, ".a") != 0) continue;
    std::string full = std::string(dist_dir) + "/" + n;
    if (stat(full.c_str(), &as) != 0) continue;
    if (!found || as.st_mtime < oldest_archive) oldest_archive = as.st_mtime;
    found = true;
  }
  closedir(d);
  if (!found) return;

  for (const char *src : srcs) {
    struct stat ss;
    if (stat(src, &ss) != 0) continue;   // repo kökünden çalışılmıyor
    if (ss.st_mtime > oldest_archive) {
      fprintf(stderr,
              tulpar::i18n::tr_en(
                  "[AOT] UYARI: %s arsivleri %s dosyasindan ESKI. Yeni bir\n"
                  "      runtime sembolu eklendiyse link 'undefined symbol'\n"
                  "      der. Tazele: %s\n",
                  "[AOT] WARNING: archives in %s are OLDER than %s. If a new\n"
                  "      runtime symbol was added the link will fail with\n"
                  "      'undefined symbol'. Refresh with: %s\n"),
              dist_dir, src, rebuild_cmd);
      return;   // tek uyarı yeter
    }
  }
}

// Web hedefinin HTML kabuğu. em++'a .html ürettirmiyoruz: emcc'nin HTML
// çıktısı html-minifier-terser (npm) ister ve vendored SDK'larda yoktur;
// kendi kabuğumuz hem bağımlılıksız hem de oyuna uygun (koyu, ortalanmış
// canvas). Emscripten GLFW canvas'ı Module.canvas'tan bulur.
static void write_web_shell_html(const char *exe_filename) {
  char html_path[512];
  snprintf(html_path, sizeof(html_path), "%s.html", exe_filename);
  const char *base = strrchr(exe_filename, '/');
#if PLATFORM_WINDOWS
  const char *base2 = strrchr(exe_filename, '\\');
  if (base2 && (!base || base2 > base)) base = base2;
#endif
  base = base ? base + 1 : exe_filename;
  FILE *f = fopen(html_path, "wb");
  if (!f) return;
  fprintf(f,
          "<!doctype html>\n<html lang=\"tr\">\n<head>\n"
          "<meta charset=\"utf-8\">\n"
          "<meta name=\"viewport\" content=\"width=device-width,"
          "initial-scale=1\">\n"
          "<title>%s — Tulpar Tame</title>\n"
          "<style>html,body{margin:0;height:100%%;background:#14141e;"
          "display:flex;align-items:center;justify-content:center;"
          "overflow:hidden}canvas{max-width:100%%;max-height:100%%;"
          "outline:none}#err{position:fixed;left:0;right:0;bottom:0;"
          "max-height:45%%;overflow:auto;background:#300;color:#faa;"
          "font:12px monospace;padding:8px;white-space:pre-wrap;"
          "display:none;z-index:9}"
          // --- Dokunmatik kontroller (yalniz dokunmatik cihazlarda gorunur) ---
          "#touch{display:none;position:fixed;left:0;right:0;bottom:0;z-index:8;"
          "justify-content:space-between;align-items:flex-end;padding:14px;"
          "pointer-events:none;-webkit-user-select:none;user-select:none}"
          "#touch>div{pointer-events:auto}"
          "#touch button{width:58px;height:58px;margin:3px;border-radius:12px;"
          "border:1px solid #3a3a5a;background:rgba(40,40,64,.62);color:#ffd45e;"
          "font:700 22px system-ui;touch-action:none;-webkit-tap-highlight-color:transparent}"
          "#touch button.act{background:rgba(255,212,94,.55);color:#111}"
          "#touch .row{display:flex;justify-content:center}"
          "#touch .big{width:74px;height:74px;font-size:20px}"
          "#touch .act-group{display:flex;align-items:center}"
          "@media (pointer:coarse){#touch{display:flex}}</style>\n"
          "</head>\n<body>\n"
          "<canvas id=\"canvas\" oncontextmenu=\"event.preventDefault()\" "
          "tabindex=\"-1\"></canvas>\n"
          // Dokunmatik gamepad: yon pad'i (sol) + aksiyon/R (sag). Butonlar
          // sentetik KeyboardEvent uretir (dogrulandi: raylib/emscripten bunlari
          // okuyor); masaustunde @media pointer:coarse ile gizli.
          "<div id=\"touch\">"
          "<div class=\"pad\">"
          "<div class=\"row\"><button id=\"tU\">&#9650;</button></div>"
          "<div class=\"row\"><button id=\"tL\">&#9664;</button>"
          "<button id=\"tD\">&#9660;</button>"
          "<button id=\"tR\">&#9654;</button></div></div>"
          "<div class=\"act-group\"><button id=\"tS\">R</button>"
          "<button id=\"tA\" class=\"big\">&#9679;</button></div>"
          "</div>\n"
          "<pre id=\"err\"></pre>\n"
          "<script>\n"
          "// Hata/stderr'i sayfada goster — konsol acmadan teshis icin.\n"
          "var errBox=document.getElementById('err');\n"
          "function showErr(m){errBox.style.display='block';"
          "errBox.textContent+=m+'\\n';}\n"
          "window.onerror=function(m,s,l,c,e)"
          "{showErr('[onerror] '+m+' @'+s+':'+l);};\n"
          "window.addEventListener('unhandledrejection',function(ev)"
          "{showErr('[promise] '+ev.reason);});\n"
          "var Module={canvas:document.getElementById('canvas'),"
          "printErr:function(t){console.error(t);showErr(t);},"
          "onAbort:function(w){showErr('[abort] '+w);}};\n"
          "</script>\n"
          "<script src=\"%s.js\"></script>\n"
          // Dokunmatik butonlari klavye olaylarina bagla (pointerdown->keydown,
          // birak->keyup). Tutulan yon (key_down) ve tek dokunus (key_pressed)
          // ikisi de calisir. Canvas + document + window'a gonderilir.
          "<script>\n"
          "(function(){var C=document.getElementById('canvas');"
          "function K(code,kc){return{code:code,"
          "key:code.indexOf('Arrow')===0?code:(code==='Space'?' ':'r'),"
          "keyCode:kc,which:kc,bubbles:true,cancelable:true};}"
          "function fire(t,k){var e=new KeyboardEvent(t,k);"
          "(C||document).dispatchEvent(e);document.dispatchEvent(e);"
          "window.dispatchEvent(e);}"
          "function bind(id,code,kc){var el=document.getElementById(id);"
          "if(!el)return;var k=K(code,kc);"
          "var d=function(e){e.preventDefault();fire('keydown',k);"
          "el.classList.add('act');};"
          "var u=function(e){e.preventDefault();fire('keyup',k);"
          "el.classList.remove('act');};"
          "el.addEventListener('pointerdown',d);"
          "el.addEventListener('pointerup',u);"
          "el.addEventListener('pointerleave',u);"
          "el.addEventListener('pointercancel',u);}"
          "bind('tL','ArrowLeft',37);bind('tR','ArrowRight',39);"
          "bind('tU','ArrowUp',38);bind('tD','ArrowDown',40);"
          "bind('tA','Space',32);bind('tS','KeyR',82);})();\n"
          "</script>\n"
          "</body>\n</html>\n",
          base, base);
  fclose(f);
}

// tame (2D oyun kütüphanesi) link bayrakları — yalnız program "tame" import
// ettiğinde (veya doğrudan bir tm_* builtin çağırdığında) eklenir; sıradan
// binary'ler GL/pencere bağımlılığı almaz. libtulpar_tame.a = vendored raylib
// + aot_tm_* binding'leri (bkz. CMakeLists "Tame" bölümü).
//
// Sıralama: tame, AOT_LINK_LIB_FLAGS'ten (yani -ltulpar_runtime'dan) ÖNCE
// gelmeli — GNU ld arşivleri soldan sağa çözer; kullanıcı objesinin aot_tm_*
// referanslarını tame karşılar, tame'in vm_make_* referanslarını sağındaki
// libtulpar_runtime.a karşılar.
//
// Linux'ta -lX11/-lGL gerekmez: raylib'in GLFW'si X11 kütüphanelerini ve GL'i
// çalışma zamanında dlopen'lar (glad + GLFW modül yükleyicisi); -ldl zaten
// temel bayraklarda var.
static const char *tame_link_flags(int uses_tame) {
  if (!uses_tame) return "";
#if PLATFORM_WINDOWS
  return " -ltulpar_tame -lopengl32 -lgdi32 -lwinmm";
#elif PLATFORM_MACOS
  return " -ltulpar_tame -framework Cocoa -framework IOKit"
         " -framework CoreVideo -framework OpenGL"
         " -framework CoreAudio -framework AudioToolbox";
#else
  return " -ltulpar_tame";
#endif
}

// Parse source code to AST. Caller-provided `source_filename` is
// optional and only used by parse-time diagnostics for the file path
// in `--> path:line` headers.
static ASTNode_C *parse_source(const char *source,
                               const char *source_filename = nullptr) {
  // Hand source + filename to the parser for Rust-style diagnostics.
  parser_set_diagnostic_context(source, source_filename);
  Lexer *lexer = lexer_create(source);

  int token_capacity = 100;
  int token_count = 0;
  Token **tokens = static_cast<Token **>(malloc(sizeof(Token *) * token_capacity));

  Token *token;
  while ((token = lexer_next_token(lexer))->type() != TOKEN_EOF) {
    if (token_count >= token_capacity) {
      token_capacity *= 2;
      tokens = (Token **)realloc(tokens, sizeof(Token *) * token_capacity);
    }
    tokens[token_count++] = token;
  }
  tokens[token_count++] = token; // EOF

  lexer_free(lexer);

  Parser_C *parser = parser_create(tokens, token_count);
  ASTNode_C *ast = parser_parse(parser);

  // Note: tokens are still referenced by AST, careful with cleanup
  parser_free(parser);

  // Free tokens after parser (AST copies what it needs)
  for (int i = 0; i < token_count; i++) {
    token_free(tokens[i]);
  }
  free(tokens);

  return ast;
}

// Compile Tulpar source to object file.
//
// The `_with_filename` variant is the canonical entry point — it pipes the
// source filename through to LLVMBackend so codegen diagnostics can render
// `--> path/file.tpr:42` headers. The plain `aot_compile` is kept as a
// thin wrapper that defaults the filename to NULL (legacy behaviour: shows
// `(stdin)` in diagnostics).
AOTResult aot_compile(const char *source, const char *output_name) {
  return aot_compile_with_filename(source, output_name, nullptr);
}

AOTResult aot_compile_with_filename(const char *source,
                                    const char *output_name,
                                    const char *source_filename) {
  return aot_compile_with_filename_debug(source, output_name,
                                         source_filename, /*emit_debug=*/0);
}

AOTResult aot_compile_with_filename_debug(const char *source,
                                          const char *output_name,
                                          const char *source_filename,
                                          int emit_debug_info) {
  ASTNode_C *ast;
  {
    AOTPhaseTimer t("parse");
    AOT_PROGRESS("[AOT] Parsing source...\n");
    ast = parse_source(source, source_filename);
  }
  if (!ast) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to parse source\n"));
    return AOT_ERROR_PARSE;
  }

  LLVMBackend *backend;
  {
    AOTPhaseTimer t("backend-init");
    AOT_PROGRESS("[AOT] Creating LLVM backend...\n");
    backend = llvm_backend_create("tulpar_aot_module");
  }
  if (!backend) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to create LLVM backend\n"));
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }

  // Hand the source text to the backend so codegen errors can render a
  // Rust-style line excerpt + caret. (Borrow only — caller owns the buffer.)
  backend->source_text = source;
  backend->source_filename = source_filename;
  backend->emit_debug_info = emit_debug_info;
  // Web hedefi declare_runtime_functions'tan ÖNCE set edilmeli — VMValue
  // çağrı tiplerinin şekli (sret+byval vs SysV) buna bağlı.
  backend->target_web = g_target_web;

  // Plan 07 PR 2: open the debug-info graph before codegen runs so
  // subsequent passes can hang `DISubprogram` / `!dbg` metadata off
  // the compile unit set up here. No-op when --debug was not passed.
  llvm_backend_init_debug_info(backend, source_filename);

  {
    AOTPhaseTimer t("codegen");
    AOT_PROGRESS("[AOT] Generating LLVM IR...\n");
    llvm_backend_compile(backend, ast);
  }

  if (backend->had_error) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en(
        "[AOT] Error: Codegen reported errors above; aborting build.\n"));
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }

  // Debug: dump raw front-end IR BEFORE optimization. Enable with
  // TULPAR_AOT_EMIT_LL_PRE=1 to inspect codegen output pre-opt.
  {
    const char *e = getenv("TULPAR_AOT_EMIT_LL_PRE");
    if (e && *e && *e != '0') {
      char ir_file[256];
      snprintf(ir_file, sizeof(ir_file), "%s.pre.ll", output_name);
      llvm_backend_emit_ir_file(backend, ir_file);
    }
  }

  {
    AOTPhaseTimer t("optimize");
    AOT_PROGRESS("[AOT] Optimizing...\n");
    llvm_backend_optimize(backend);
  }

  // Plan 07 PR 2: close the debug-info graph before any IR consumer
  // (emit_ir_file / emit_object) walks it. Finalize must come AFTER
  // optimisation so any optimiser pass that touches `!dbg` metadata
  // has run; doing it before would leave dangling references that
  // the verifier rejects. No-op when --debug was not passed.
  llvm_backend_finalize_debug_info(backend);

  // Generate output filename
  //
  // Web hedefinde çıktı adı bir taban addır: `.html` (kabuk), `.js` ve `.wasm`
  // hep buna EKLENİR. Kullanıcı doğal olarak `-o game.html` yazdığında bu
  // `game.html.html` + `game.html.js` üretiyordu. Sondaki `.html`'i bir kez
  // soy: `game` ve `game.html` artık aynı (doğru) çıktıyı verir.
  std::string web_base;
  if (g_target_web && output_name) {
    web_base = output_name;
    const std::string dot_html = ".html";
    if (web_base.size() > dot_html.size() &&
        web_base.compare(web_base.size() - dot_html.size(), dot_html.size(),
                         dot_html) == 0) {
      web_base.erase(web_base.size() - dot_html.size());
      output_name = web_base.c_str();
    }
  }

  char obj_filename[256];
  char exe_filename[256];
  snprintf(obj_filename, sizeof(obj_filename), "%s.o", output_name);
  snprintf(exe_filename, sizeof(exe_filename), "%s", output_name);

  // Emit IR (Debug). Off by default — costs measurable I/O on bigger inputs
  // and almost nobody reads the .ll. Enable with TULPAR_AOT_EMIT_LL=1.
  {
    const char *e = getenv("TULPAR_AOT_EMIT_LL");
    if (e && *e && *e != '0') {
      AOTPhaseTimer t("emit-ll");
      char ir_file[256];
      snprintf(ir_file, sizeof(ir_file), "%s.ll", output_name);
      llvm_backend_emit_ir_file(backend, ir_file);
    }
  }

  // Android hedefi: iki ABI için obje + NDK linki + APK staging, sonra çık.
  if (g_target_android) {
    // Arşiv tazeliği NDK aramasından ÖNCE. Sıra önemli: NDK'sız bir makinede
    // sürücü "NDK gerekir" deyip çıkıyordu, yani bayat arşiv uyarısı o yola
    // hiç varmıyordu — üstelik bayatlığı en kolay gözden kaçacağı makine tam
    // olarak orası. Zaman damgası karşılaştırması hiçbir araç zinciri
    // istemiyor, o yüzden burada hiçbir şeye mal olmuyor.
    for (const char *abi : {"arm64-v8a", "x86_64"}) {
      std::string dist = std::string("android/dist/") + abi;
      warn_if_prebuilt_archive_stale(dist.c_str(),
                                     "android/build_tame_android.sh");
    }
    std::string ndk = find_android_ndk();
    std::string tc = ndk + "/toolchains/llvm/prebuilt/linux-x86_64/bin/";
    if (ndk.empty()) {
      fprintf(stderr, "%s\n",
              tulpar::i18n::tr_en(
                  "[AOT] Android hedefi icin NDK gerekir: TULPAR_ANDROID_NDK "
                  "ayarlayin ya da Android Studio'dan NDK kurun "
                  "(~/Android/Sdk/ndk/*) "
                  "(android/build_tame_android.sh ayni NDK'yi kullanir).",
                  "[AOT] The android target needs the NDK: set "
                  "TULPAR_ANDROID_NDK or install the NDK from Android "
                  "Studio (~/Android/Sdk/ndk/*) "
                  "(android/build_tame_android.sh uses the same NDK)."));
      llvm_backend_destroy(backend);
      ast_node_free(ast);
      return AOT_ERROR_LINK;
    }
    struct AbiSpec {
      const char *abi;
      const char *clangxx;
      const char *triple;
    };
    const AbiSpec abis[] = {
        {"arm64-v8a", "aarch64-linux-android34-clang++",
         "aarch64-linux-android34"},
        {"x86_64", "x86_64-linux-android34-clang++", "x86_64-linux-android34"},
    };
    std::string stage = std::string(output_name) + "_apk";
    std::string extra = aot_extra_link_flags();
    for (const AbiSpec &a : abis) {
      std::string libdir = stage + "/lib/" + a.abi;
      {
        std::string mk = "mkdir -p \"" + libdir + "\"";
        if (system(mk.c_str()) != 0) { /* emit asamasi zaten hata verir */ }
      }
      std::string obj = stage + "/" + a.abi + ".o";
      AOT_PROGRESS("[AOT] Emitting %s object: %s\n", a.abi, obj.c_str());
      if (llvm_backend_emit_object_for_triple(backend, obj.c_str(),
                                              a.triple) != 0) {
        fprintf(stderr, "%s",
                tulpar::i18n::tr_for_en(
                    "[AOT] Error: Failed to emit object file\n"));
        llvm_backend_destroy(backend);
        ast_node_free(ast);
        return AOT_ERROR_EMIT;
      }
      // -u ANativeActivity_onCreate: sembol native_app_glue arşivinde;
      //   çekilmezse Android loader aktiviteyi başlatamaz.
      // -Wl,-z,max-page-size=16384: Android 15+ 16KB sayfa imajları
      //   (emülatör dahil) 16K hizali .so ister.
      std::string cmd = tc + a.clangxx + " -shared -static-libstdc++" +
                        " -u ANativeActivity_onCreate" +
                        // --no-undefined: eksik sembolü dlopen anında değil
                        // LİNK anında yakala (async stub'ları eksikse burada
                        // patlar, cihazda UnsatisfiedLinkError yerine).
                        " -Wl,--no-undefined" +
                        " -Wl,-z,max-page-size=16384" + " -o \"" + libdir +
                        "/libtulpargame.so\" \"" + obj + "\" " +
                        build_android_link_search_dirs(a.abi) +
                        "-ltulpar_tame_android -ltulpar_runtime_android "
                        "-landroid -llog -lEGL -lGLESv2 -lOpenSLES -lm -ldl" +
                        extra + " 2>&1";
      {
        std::string dist = std::string("android/dist/") + a.abi;
        warn_if_prebuilt_archive_stale(dist.c_str(),
                                       "android/build_tame_android.sh");
      }
      AOT_PROGRESS("[AOT] Linking %s: libtulpargame.so\n", a.abi);
      int rc;
      {
        AOTPhaseTimer t("link-android");
        rc = system(cmd.c_str());
      }
      if (rc != 0) {
        fprintf(stderr,
                tulpar::i18n::tr_for_en(
                    "[AOT] Error: Linking failed (code %d). Check clang "
                    "installation and libraries.\n"),
                rc);
        fprintf(stderr, "%s\n",
                tulpar::i18n::tr_en(
                    "[AOT] Android linki icin android/dist arsivleri gerekir: "
                    "once android/build_tame_android.sh calistirin.",
                    "[AOT] The android link needs the android/dist archives: "
                    "run android/build_tame_android.sh first."));
        llvm_backend_destroy(backend);
        ast_node_free(ast);
        return AOT_ERROR_LINK;
      }
    }
    // Uygulama kimliği: tulpar.toml [android] (yoksa tarihi varsayılanlar;
    // etiket çıktı adının taban kısmı). İkon varsa staging res/'ine kopyalanır.
    const char *label = output_name;
    if (const char *slash = strrchr(output_name, '/')) label = slash + 1;
    AndroidAppConfig app_cfg = load_android_app_config(stage, label);
    write_android_manifest(stage, app_cfg);
    write_android_resources(stage, app_cfg);   // splash teması + adaptive ikon
    printf("[AOT] Successfully created: %s/ (lib/arm64-v8a + lib/x86_64 + "
           "AndroidManifest.xml)\n",
           stage.c_str());
    if (g_android_aab) {
      // --aab: staging'i Play Store'a yüklenebilir imzalı .aab'a paketle.
      std::string script = find_android_script("package_aab.sh");
      if (script.empty()) {
        fprintf(stderr, "%s\n",
                tulpar::i18n::tr_en(
                    "[AOT] --aab icin android/package_aab.sh bulunamadi: "
                    "TULPAR_ANDROID_TOOLS ile betigin dizinini gosterin ya da "
                    "depo kokunden calistirin.",
                    "[AOT] --aab needs android/package_aab.sh: point "
                    "TULPAR_ANDROID_TOOLS at its directory or run from the "
                    "repo root."));
        llvm_backend_destroy(backend);
        ast_node_free(ast);
        return AOT_ERROR_LINK;
      }
      std::string aab = std::string(output_name) + ".aab";
      std::string cmd =
          "bash \"" + script + "\" \"" + stage + "\" \"" + aab + "\"";
      AOT_PROGRESS("[AOT] Packaging AAB: %s\n", aab.c_str());
      int rc;
      {
        AOTPhaseTimer t("package-aab");
        rc = system(cmd.c_str());
      }
      if (rc != 0) {
        fprintf(stderr,
                tulpar::i18n::tr_en(
                    "[AOT] Hata: AAB paketleme basarisiz (kod %d) — "
                    "aapt2/bundletool/java kurulumunu kontrol edin.\n",
                    "[AOT] Error: AAB packaging failed (code %d) — check "
                    "aapt2/bundletool/java.\n"),
                rc);
        llvm_backend_destroy(backend);
        ast_node_free(ast);
        return AOT_ERROR_LINK;
      }
      printf("[AOT] AAB: %s\n", aab.c_str());
      printf("%s\n",
             tulpar::i18n::tr_en(
                 "[AOT] Play Console'a yukle (kendi upload anahtarinla imzala).",
                 "[AOT] Upload to Play Console (sign with your own upload "
                 "key)."));
    } else if (g_android_apk) {
      // --apk: tek komut akışı — staging'i hemen imzalı .apk'ya paketle.
      std::string script = find_android_script("package_apk.sh");
      if (script.empty()) {
        fprintf(stderr, "%s\n",
                tulpar::i18n::tr_en(
                    "[AOT] --apk icin android/package_apk.sh bulunamadi: "
                    "TULPAR_ANDROID_TOOLS ile betigin dizinini gosterin ya da "
                    "depo kokunden calistirin.",
                    "[AOT] --apk needs android/package_apk.sh: point "
                    "TULPAR_ANDROID_TOOLS at its directory or run from the "
                    "repo root."));
        llvm_backend_destroy(backend);
        ast_node_free(ast);
        return AOT_ERROR_LINK;
      }
      std::string apk = std::string(output_name) + ".apk";
      std::string cmd =
          "bash \"" + script + "\" \"" + stage + "\" \"" + apk + "\"";
      AOT_PROGRESS("[AOT] Packaging APK: %s\n", apk.c_str());
      int rc;
      {
        AOTPhaseTimer t("package-apk");
        rc = system(cmd.c_str());
      }
      if (rc != 0) {
        fprintf(stderr,
                tulpar::i18n::tr_en(
                    "[AOT] Hata: APK paketleme basarisiz (kod %d) — "
                    "aapt2/zipalign/apksigner kurulumunu kontrol edin "
                    "(Android SDK build-tools).\n",
                    "[AOT] Error: APK packaging failed (code %d) — check "
                    "aapt2/zipalign/apksigner (Android SDK build-tools).\n"),
                rc);
        llvm_backend_destroy(backend);
        ast_node_free(ast);
        return AOT_ERROR_LINK;
      }
      printf("[AOT] APK: %s\n", apk.c_str());
      printf("%s\n",
             tulpar::i18n::tr_en(
                 "[AOT] Kurulum: adb install -r <apk>  (ya da "
                 "android/install_run.sh <apk>).",
                 "[AOT] Install: adb install -r <apk>  (or "
                 "android/install_run.sh <apk>)."));
    } else {
      printf("%s\n",
             tulpar::i18n::tr_en(
                 "[AOT] APK icin: --apk bayragi ya da android/package_apk.sh.",
                 "[AOT] To get an .apk: pass --apk or run "
                 "android/package_apk.sh."));
    }
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_OK;
  }

  {
    AOTPhaseTimer t("emit-obj");
    AOT_PROGRESS("[AOT] Emitting object file: %s\n", obj_filename);
    if (llvm_backend_emit_object(backend, obj_filename) != 0) {
      fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to emit object file\n"));
      llvm_backend_destroy(backend);
      ast_node_free(ast);
      return AOT_ERROR_EMIT;
    }
  }

  // Link using clang++ (need C++ runtime for tulpar_runtime).
  // `-g` is forwarded to clang when --debug was requested so debug
  // sections emitted in the object file survive linking into the
  // final binary. Today the object has no `!dbg` metadata yet
  // (Plan 07 PR 2 wires up LLVMDIBuilder), so `-g` is effectively
  // a no-op until that lands — but plumbing the switch through now
  // keeps the CLI surface stable across the PR series.
  AOT_PROGRESS("[AOT] Linking executable: %s\n", exe_filename);
  std::string search_dirs = build_link_search_dirs();
  std::string extra_flags = aot_extra_link_flags();
  const char *debug_flag = emit_debug_info ? "-g " : "";
  char link_cmd[2048];
  if (g_target_web) {
    // Web hedefi: em++ (Emscripten) linkler → <out>.html + .js + .wasm.
    // - USE_GLFW=3: raylib PLATFORM_WEB, Emscripten'in GLFW JS
    //   implementasyonunu kullanır (rglfw.c web arşivinde yok).
    // - ASYNCIFY: Tulpar oyunları bloklu `while (running())` döngüsü yazar;
    //   raylib'in EndDrawing'i web'de emscripten_sleep çağırır — ASYNCIFY
    //   bunu tarayıcının event-loop'una çevirir.
    // - Arşiv sırası native ile aynı: tame, runtime'dan önce.
    // - TULPAR_WEB_ASSETS=<dizin> → --preload-file (oyun varlıkları sanal
    //   dosya sistemine aynı yolda gömülür).
    warn_if_prebuilt_archive_stale("wasm/dist", "wasm/build_tame_web.sh");
    std::string web_dirs = build_web_link_search_dirs();
    std::string preload;
    if (const char *assets = getenv("TULPAR_WEB_ASSETS"); assets && *assets) {
      preload = std::string(" --preload-file ") + assets;
    }
    // Çıktı .js (+ .wasm): emcc'nin .html üreticisi npm bağımlılığı ister;
    // HTML kabuğunu link sonrası write_web_shell_html kendimiz yazarız.
    // ASYNCIFY_STACK_SIZE: unwind sırasında canlı wasm local'leri bu
    // tampona yazılır; Tulpar main'i entry-hoisted VMValue alloca'larıyla
    // dolu olduğundan varsayılan 4KB kolayca taşar ("Aborted(Asyncify
    // stack overflow)") — 128KB güvenli.
    // EXPORTED_RUNTIME_METHODS=HEAPF32: raylib'in ses arka ucu (miniaudio,
    // raudio.c) ScriptProcessorNode geri-çağrısında `Module.HEAPF32.buffer`
    // okur. Emscripten (>=3.x, burada 5.0) HEAP view'lerini artık Module'e
    // OTOMATİK bağlamaz → `Module.HEAPF32` undefined → ses açan her oyun
    // (load_sound/load_music) her audio-frame'de "Cannot read properties of
    // undefined (reading 'buffer')" ile çöker. HEAPF32'yi Module'e export
    // etmek (büyümede otomatik yeniden atanır) bunu giderir; sessiz oyunlar
    // için de zararsız.
    snprintf(
        link_cmd, sizeof(link_cmd),
        "em++ %s -o %s.js -O2 -sUSE_GLFW=3 -sASYNCIFY "
        "-sASYNCIFY_STACK_SIZE=131072 "
        "-sALLOW_MEMORY_GROWTH -sSTACK_SIZE=2097152 "
        "-sEXPORTED_RUNTIME_METHODS=HEAPF32 "
        "%s-ltulpar_tame_web -ltulpar_runtime_web%s%s 2>&1",
        obj_filename, exe_filename, web_dirs.c_str(), preload.c_str(),
        extra_flags.c_str());
  } else {
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang++ %s%s -o %s%s %s %s%s%s%s 2>&1",
      debug_flag, obj_filename, exe_filename, AOT_EXE_SUFFIX,
      AOT_LINK_PIE_FLAG, search_dirs.c_str(),
      tame_link_flags(backend->uses_tame), " " AOT_LINK_LIB_FLAGS,
      extra_flags.c_str());
  }

  int link_result;
  {
    AOTPhaseTimer t("link");
    link_result = system(link_cmd);
  }
  if (link_result != 0) {
    fprintf(stderr, tulpar::i18n::tr_for_en(
            "[AOT] Error: Linking failed (code %d). Check clang installation and libraries.\n"),
            link_result);
    if (g_target_web) {
      fprintf(stderr, "%s\n",
              tulpar::i18n::tr_en(
                  "[AOT] Web hedefi icin em++ gerekir: 'source "
                  "wasm/emsdk/emsdk_env.sh' calistirin ve wasm/dist "
                  "arsivlerinin (wasm/build_tame_web.sh) mevcut olduguna "
                  "emin olun.",
                  "[AOT] The web target needs em++: run 'source "
                  "wasm/emsdk/emsdk_env.sh' and make sure the wasm/dist "
                  "archives (wasm/build_tame_web.sh) exist."));
    }
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_LINK;
  } else {
    if (g_target_web) {
      write_web_shell_html(exe_filename);
      printf("[AOT] Successfully created: %s.html (+ .js, .wasm)\n",
             exe_filename);
    } else {
      printf("[AOT] Successfully created: %s\n", exe_filename);
    }
  }

  llvm_backend_destroy(backend);
  ast_node_free(ast);

  return AOT_OK;
}

// Compile and immediately run (for development/testing)
AOTResult aot_compile_and_run(const char *source) {
  AOTResult result = aot_compile(source, "tulpar_temp");
  if (result != AOT_OK) {
    return result;
  }

  // Try to execute
  AOT_PROGRESS("[AOT] Executing generated binary...\n");
#if PLATFORM_WINDOWS
  // cmd.exe does not auto-search the current directory unless an explicit
  // path is given, so prefix with .\ to ensure the binary is found.
  int run_status = system(".\\tulpar_temp.exe");
#else
  int run_status = system("./tulpar_temp");
#endif
  (void)run_status;

  return AOT_OK;
}

// Silent compile to native binary (no output, temp files)
static AOTResult aot_compile_silent(const char *source,
                                    const char *output_name,
                                    const char *source_filename) {
  ASTNode_C *ast = parse_source(source, source_filename);
  if (!ast) {
    return AOT_ERROR_PARSE;
  }

  LLVMBackend *backend = llvm_backend_create("tulpar_aot_module");
  if (!backend) {
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }
  backend->quiet = 1; // Suppress [AOT] messages
  backend->source_text = source;
  backend->source_filename = source_filename;

  llvm_backend_compile(backend, ast);
  if (backend->had_error) {
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }
  llvm_backend_optimize(backend);

  char obj_filename[256];
  char exe_filename[256];
  snprintf(obj_filename, sizeof(obj_filename), "%s.o", output_name);
  snprintf(exe_filename, sizeof(exe_filename), "%s", output_name);

  if (llvm_backend_emit_object(backend, obj_filename) != 0) {
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_EMIT;
  }

  // Link silently (suppress output)
  std::string silent_search_dirs = build_link_search_dirs();
  std::string silent_extra_flags = aot_extra_link_flags();
  char link_cmd[2048];
#if PLATFORM_WINDOWS
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang++ %s -o %s%s %s %s%s%s%s 2>NUL",
      obj_filename, exe_filename, AOT_EXE_SUFFIX,
      AOT_LINK_PIE_FLAG, silent_search_dirs.c_str(),
      tame_link_flags(backend->uses_tame), " " AOT_LINK_LIB_FLAGS,
      silent_extra_flags.c_str());
#else
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang++ %s -o %s%s %s %s%s%s%s 2>/dev/null",
      obj_filename, exe_filename, AOT_EXE_SUFFIX,
      AOT_LINK_PIE_FLAG, silent_search_dirs.c_str(),
      tame_link_flags(backend->uses_tame), " " AOT_LINK_LIB_FLAGS,
      silent_extra_flags.c_str());
#endif

  int link_result = system(link_cmd);

  // Cleanup object file
  remove(obj_filename);

  llvm_backend_destroy(backend);
  ast_node_free(ast);

  if (link_result != 0) {
    return AOT_ERROR_LINK;
  }

  return AOT_OK;
}

// Silent compile and run - used as default execution mode.
// Compiles to temp binary, runs it, cleans up. No [AOT] output.
// `tulpar script.tpr a b` çağrısındaki fazladan argümanlar. Program bunları
// `args()` ile okuyor; iletilmezse Tulpar ile yazılmış hiçbir CLI aracı
// `tulpar` üzerinden çalıştırıldığında argüman göremiyordu (yalnız `build`
// ile üretilen ikili görüyordu, ki bu tutarsızlıktı).
static std::string g_tulpar_run_args;

void aot_set_run_args(const char *quoted) {
  g_tulpar_run_args = quoted ? quoted : "";
}

AOTResult aot_compile_and_run_silent(const char *source) {
  return aot_compile_and_run_silent_with_filename(source, nullptr);
}

AOTResult aot_compile_and_run_silent_with_filename(const char *source,
                                                   const char *source_filename) {
#if PLATFORM_WINDOWS
  const char *base = "tulpar_run_tmp";
  AOTResult result = aot_compile_silent(source, base, source_filename);
  if (result != AOT_OK) {
    return result;
  }
  // cmd.exe does not auto-search the current directory unless an explicit
  // path is given, so prefix with .\ to ensure the binary is found.
  std::string run_cmd = ".\\tulpar_run_tmp.exe";
  if (!g_tulpar_run_args.empty()) run_cmd += " " + g_tulpar_run_args;
  int run_result = system(run_cmd.c_str());
  remove("tulpar_run_tmp.exe");
  remove("tulpar_run_tmp.ll");
  remove("tulpar_run_tmp.o");
#else
  const char *base = "/tmp/.tulpar_run";
  AOTResult result = aot_compile_silent(source, base, source_filename);
  if (result != AOT_OK) {
    return result;
  }
  std::string run_cmd = "/tmp/.tulpar_run";
  if (!g_tulpar_run_args.empty()) run_cmd += " " + g_tulpar_run_args;
  int run_result = system(run_cmd.c_str());
  remove("/tmp/.tulpar_run");
  remove("/tmp/.tulpar_run.ll");
#endif

  // The compile + link already succeeded above (we returned early otherwise),
  // so a non-zero status here means the PROGRAM ran and exited non-zero — not
  // a toolchain failure. Report it as AOT_RAN_NONZERO so the driver propagates
  // the failure without the misleading "compile/link failed" banner.
#if !PLATFORM_WINDOWS
  // Ctrl+C (SIGINT) on a long-running server is a normal stop, not a failure.
  if (WIFSIGNALED(run_result) && WTERMSIG(run_result) == SIGINT) {
    return AOT_OK;
  }
#endif
  return (run_result == 0) ? AOT_OK : AOT_RAN_NONZERO;
}

// Check-only pipeline: parse + codegen pass, no optimisation, no object
// emission, no link. Used by the LSP server (`tulpar --lsp`) to gather
// structured diagnostics on every keystroke. Cost is dominated by
// llvm_backend_compile, but staying off the linker keeps end-to-end
// latency in the ~100ms range on typical files.
AOTResult aot_check_only(const char *source, const char *source_filename) {
  return aot_check_and_index(source, source_filename, nullptr);
}

AOTResult aot_check_and_index(const char *source, const char *source_filename,
                              void *out_index) {
  ASTNode_C *ast = parse_source(source, source_filename);
  if (!ast) {
    return AOT_ERROR_PARSE;
  }

  LLVMBackend *backend = llvm_backend_create("tulpar_lsp_check");
  if (!backend) {
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }
  backend->quiet = 1;
  backend->source_text = source;
  backend->source_filename = source_filename;

  llvm_backend_compile(backend, ast);
  AOTResult result = backend->had_error ? AOT_ERROR_CODEGEN : AOT_OK;

  // Build the symbol index *after* codegen but before ast_node_free —
  // codegen might mutate AST node fields (e.g. importing module bodies),
  // so we capture the post-import state for hover/completion.
  if (out_index) {
    auto *idx = static_cast<tulpar::DocumentIndex *>(out_index);
    tulpar::document_index_build(ast, source, *idx);
  }

  llvm_backend_destroy(backend);
  ast_node_free(ast);
  return result;
}
