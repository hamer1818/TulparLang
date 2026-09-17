// Minimal test cercevesi: STL yok, ayirma yok (kayit sabit diziye).
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace tulpar::engine::test {

using TestFn = void (*)();
struct Case {
  const char *name;
  TestFn fn;
};
struct Registry {
  // 512: bugun 397 test var. TAVAN SESSIZ DEGIL — asilirsa `overflow` sayar ve
  // kosum KIRMIZI doner (bkz. Registrar). 2026-09-17'de tam bu tavan sessizce
  // asildi: PR #322 test sayisini 165'ten 397'ye cikardi, tavan 256'ydi ve 141
  // test HIC KAYDOLMADI. Ozet satiri "256 passed, 0 failed" diyordu; o sayi bir
  // OLCUM DEGIL, tavanin kendisiydi ve kimse sormadi.
  static constexpr int kMax = 512;
  static Case cases[kMax];
  static int count;
  static int failures;      // mevcut testteki CHECK basarisizliklari
  static int failures_total;
  static int skipped;       // GORUNUR atlanan testler (ozet satirinda)
  static int overflow;      // kapasiteye SIGMAYAN test sayisi (sessiz olamaz)
};
// Test kosamadi (donanim/arac yok): sebep basilir ve ozet satirina girer.
// Sessiz `return` YASAK — atlanan test yesil sayilmasin (Tuzaklar 1m).
inline void skip(const char *reason) {
  std::printf("    ATLANDI: %s\n", reason);
  Registry::skipped++;
}
// Gecici dizin: $TMPDIR, yoksa /tmp. Android'de /tmp YOK; adb shell'de
// TMPDIR=/data/local/tmp, APK icinde host internalDataPath verir.
inline const char *tmp_dir() {
  const char *t = std::getenv("TMPDIR");
  return (t && *t) ? t : "/tmp";
}
// mkstemp/mkdtemp sablonu: "<tmp>/<stem>_XXXXXX".
inline void tmp_template(char *buf, size_t n, const char *stem) {
  std::snprintf(buf, n, "%s/%s_XXXXXX", tmp_dir(), stem);
}
struct Registrar {
  Registrar(const char *name, TestFn fn) {
    if (Registry::count < Registry::kMax) {
      Registry::cases[Registry::count++] = Case{name, fn};
      return;
    }
    // SESSIZCE DUSURME YOK. Eskiden burada `else` yoktu: kapasiteyi asan test
    // hic kaydolmuyordu ve ozet "256 passed" diyordu — tavan sayisi, olcum
    // gibi gorunuyordu. Artik hem basiliyor hem sayiliyor; test_main bu sayac
    // sifir degilse kosumu KIRMIZI bitiriyor.
    Registry::overflow++;
    std::printf("[engine_tests] KAYIT TASMASI: '%s' kaydedilemedi (kapasite %d)\n", name, Registry::kMax);
  }
};

// `new`/`delete` cifti derleyicice ELENEBILIR (C++14 allocation elision,
// GCC/Clang -O2+ yapar). Ayirma sayan testlerde isaretci KACMALI: bu engel
// derleyiciye "p gozlemlendi" der. Olculdu 2026-09-14: engelsiz
// `new int(1); delete` sayaci artirmadi ve kapi testi yanlis dustu.
inline void escape(const void *p) { __asm__ volatile("" : : "r"(p) : "memory"); }


// CI macOS'un GPU'su bir VM cihazi ("Apple Paravirtual device", MoltenVK uzerinden):
// golge karsilastirmasi, doku ornekleme, nokta isik gibi PIKSEL kapilari orada
// farkli/bos sonuc verdi (2026-09-14, 6 test). Piksel kapilari gercek cihazda
// olculur (RTX, Mali, gfxstream); sanal GPU'da gorunur ATLANDI, sessiz yesil degil.
inline bool gpu_is_virtual(const char *device_name) {
  return device_name && std::strstr(device_name, "Paravirtual") != nullptr;
}
} // namespace tulpar::engine::test

namespace tulpar::engine::rhi { class Device; }

namespace tulpar::engine::test {
// Katmanin Arm (Mali) BestPractices kurallari GERCEKTEN acik mi?
//
// LOD kirpan bir sampler yaratir ve Arm uyari sayacinin artip artmadigina
// bakar. `true` = kurallar TANINMIYOR, yani Mali kapisi hicbir sey OLCEMEZ ve
// gorunur atlanmalidir. CI'daki apt katmani (Ubuntu 24.04, VVL 1.3.275) bu
// durumda; yerel LunarG SDK'si ve telefon (1.4.357) kurallari biliyor.
//
// TEK BIR YERDE duruyor cunku bu tam olarak surukleniden dogan bir hataydi:
// uc yeni kapi (render_graph_post / gpu_cull / temporal) pozitif kontrolu
// `renderer_mali_best_practices_gate`'ten KOPYALADI ama korumayi kopyalamadi
// ve CI Linux'ta dordu birden kirmizi dondu (2026-09-16). Kopya yerine ortak
// fonksiyon: bir daha ayrisamaz.
//
// Cagiran, `true` donerse KENDI temizligini yapip skip() cagirir — kaynaklari
// burada serbest birakamayiz.
bool arm_rules_missing(rhi::Device &dev);
// Yukaridaki atlama icin ortak metin (uc kapi ayni seyi soylesin).
inline const char *kArmRulesMissingReason =
    "dogrulama katmani Arm BestPractices kurallarini tanimiyor (surum) — Mali kapisi olculemedi";
} // namespace tulpar::engine::test

#define ENGINE_TEST(name)                                                       \
  static void name();                                                           \
  static ::tulpar::engine::test::Registrar _reg_##name(#name, name);            \
  static void name()

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      ::tulpar::engine::test::Registry::failures++;                             \
    }                                                                           \
  } while (0)
