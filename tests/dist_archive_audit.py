#!/usr/bin/env python3
"""Önceden derlenmiş web/Android arşivlerini builtin TABLOSUNA karşı denetler.

NEDEN ZAMAN DAMGASI YETMİYOR
----------------------------
`build.sh suites` zaten "arsiv BAYAT" diye uyarıyordu ve o uyarı doğruydu —
ama "olabilir" diyordu, "kırık" demiyordu. Sonuç ölçüldü: wasm/dist beş gün
bayat kaldı, bu sürede scene3d'nin HER web derlemesi `undefined symbol` ile
patlıyordu ve sarı satırı herkes (bu denetimi yazan dahil) gürültü sanıp
geçiyordu. android/dist'te eksik sembol sayısı 31'e çıkmıştı.

Zaman damgası "kaynak daha yeni" der; sembol denetimi "şu altı fonksiyon
arşivde YOK, yani bu hedefin her derlemesi link'te ölür" der. İkincisi
eyleme geçirilebilir.

FAIL/WARN AYRIMI
----------------
Arşiv YOKSA atlanıyor: o hedef kullanılmıyor demektir ve emsdk'sı olmayan
bir geliştiriciyi kırmızıya boğmak yanlış olur.

Arşiv VAR ama eksikse:
  • web     → HATA. emsdk depoda vendored, yani geliştirici her zaman
              tazeleyebilir. Düzeltilebilir bir kırmızı işe yarar.
  • android → UYARI. NDK vendored DEĞİL; geliştiricide yoksa yapabileceği
              bir şey yok ve düzeltilemeyen bir kırmızı, kırmızıyı görmezden
              gelmeyi öğretir — bu denetimin kapatmaya çalıştığı hatanın ta
              kendisi.

IKI AILE DENETLENIYOR
--------------------
  1. aot_tm_* / aot_tm3_*  (tame)   -> libtulpar_tame_{web,android}.a
  2. codegen'in adiyla bildirdigi cekirdek aot_* -> libtulpar_runtime_*.a

Ucuncu bir aile daha vardi — motor koprusu (aot_eng_* / teng_*). Motor
2026-09-20'de ayri depoya (tulpar-engine) tasindi; o ailenin denetimi de
oraya gitti. Bu depoda artik eng_* builtin'i YOK, yani burada aranmasi
"kapsam kaybi" degil, konu disi.
"""
import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Arşiv yolu, insan adı, eksik sembol HATA mı (True) yoksa UYARI mı (False).
# Tame arsivleri (aot_tm_* ailesi) — tablo tabanli denetim.
ARCHIVES = [
    ("wasm/dist/libtulpar_tame_web.a", "web", True, "wasm/build_tame_web.sh"),
    ("android/dist/arm64-v8a/libtulpar_tame_android.a", "android/arm64-v8a",
     False, "android/build_tame_android.sh"),
    ("android/dist/x86_64/libtulpar_tame_android.a", "android/x86_64",
     False, "android/build_tame_android.sh"),
]


def table_symbols():
    """`k_tame_builtins` satırlarındaki aot_* sembolleri."""
    path = os.path.join(ROOT, "src/aot/llvm_backend.cpp")
    with open(path, encoding="utf-8", errors="replace") as fh:
        src = fh.read()
    m = re.search(r"k_tame_builtins\s*\[\s*\]\s*=\s*\{(.*?)\n\s*\}\s*;", src, re.S)
    if not m:
        print("dist denetimi: k_tame_builtins tablosu bulunamadi", file=sys.stderr)
        sys.exit(1)
    rows = re.findall(
        r'\{\s*"(tm[0-9_a-z]*)"\s*,\s*"(aot_[A-Za-z0-9_]+)"\s*,\s*(\d+)\s*\}',
        m.group(1))
    if not rows:
        print("dist denetimi: tablo bos okundu", file=sys.stderr)
        sys.exit(1)
    return [sym for _name, sym, _arity in rows]


# --- Cekirdek runtime sembolleri --------------------------------------------
# NEDEN: tablo denetimi yalniz `aot_tm_*` ailesine bakiyordu. Olculdu
# (2026-09-15): wasm/dist bayatken denetim "TEMIZ" dedi, ama her web derlemesi
# `undefined symbol: aot_intern_string` ile patliyordu — cekirdek runtime
# sembolu tabloda yok, dolayisiyla denetimin kor noktasindaydi. Codegen
# sembolleri ADIYLA bildiriyor (LLVMAddFunction(module, "aot_...")); o adlarin
# hepsi hedefin runtime arsivinde TANIMLI olmali.
RUNTIME_ARCHIVES = [
    ("wasm/dist/libtulpar_runtime_web.a", "web-runtime", True, "wasm/build_tame_web.sh"),
    ("android/dist/arm64-v8a/libtulpar_runtime_android.a", "android/arm64-v8a-runtime",
     False, "android/build_tame_android.sh"),
    ("android/dist/x86_64/libtulpar_runtime_android.a", "android/x86_64-runtime",
     False, "android/build_tame_android.sh"),
]

# Hedefte BILEREK olmayan aileler: eksiklikleri hata degil (sebebi yazili).
EXPECTED_MISSING = {
    # Async: stackful coroutine ucontext ister; Emscripten'de ve bionic'te yok.
    # Async ailesi: stackful coroutine ucontext ister (Emscripten'de ve bionic'te yok).
    # TLS ailesi: OpenSSL yok; bu hedeflerde TLS derlenmiyor (TULPAR_HAS_TLS tanimsiz).
    "web-runtime": ("aot_spawn", "aot_await", "aot_promise", "aot_async", "aot_event_loop",
                    "aot_io_register", "aot_sleep_async", "aot_gather", "aot_tls_"),
    "android/arm64-v8a-runtime": ("aot_spawn", "aot_await", "aot_async", "aot_promise",
                                  "aot_io_register", "aot_sleep_async", "aot_gather", "aot_tls_"),
    "android/x86_64-runtime": ("aot_spawn", "aot_await", "aot_async", "aot_promise",
                               "aot_io_register", "aot_sleep_async", "aot_gather", "aot_tls_"),
}


# Cekirdek runtime arsivinde ARANMAYACAK aileler: bunlar BASKA arsivlerde
# yasiyor ve yalnizca ilgili program onlari kullanirsa linke girer.
#   aot_tm_* / aot_tm3_*  -> libtulpar_tame_*   (tablo denetimi zaten bakiyor)
# DIKKAT: burasi bir ELEME listesi, "denetlenmiyor" listesi DEGIL. Bir aileyi
# buraya ekleyip ona bakan bir bolum yazmamak = sessiz kapsam kaybi; aot_eng_*
# 2026-09-15'e kadar tam olarak bu durumdaydi (o aile artik ayri depoda).
OTHER_ARCHIVE_PREFIXES = ("aot_tm_", "aot_tm3_")


def codegen_runtime_symbols():
    """Codegen'in ADIYLA bildirdigi aot_* sembolleri (LLVMAddFunction cagrilari)."""
    syms = set()
    for rel in ("src/aot/llvm_backend.cpp", "src/aot/llvm_types.cpp", "src/aot/llvm_values.cpp"):
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
        syms.update(re.findall(r'LLVMAddFunction\s*\([^,]+,\s*"(aot_[A-Za-z0-9_]+)"', src))
        # Tablodan gelen semboller (tame) ayri satirlarda duruyor.
        syms.update(re.findall(r'\{\s*"[a-z0-9_]+"\s*,\s*"(aot_[A-Za-z0-9_]+)"\s*,\s*\d+\s*\}', src))
    return sorted(x for x in syms if not x.startswith(OTHER_ARCHIVE_PREFIXES))


def archive_symbols(path, prefixes=("aot_",)):
    """Arşivdeki semboller; okuyacak araç yoksa None (denetim atlanır).

    `prefixes`: hangi aileler okunacak.
    """
    pat = (r"^[0-9a-fA-F]* *[A-TV-Za-tv-z] +((?:%s)[A-Za-z0-9_]+)$"
           % "|".join(re.escape(x) for x in prefixes))
    for tool in ("llvm-nm", "nm"):
        try:
            out = subprocess.run([tool, path], capture_output=True, text=True)
        except FileNotFoundError:
            continue
        if out.returncode == 0 and out.stdout.strip():
            # YALNIZ TANIMLI semboller: `nm` ciktisinda tur harfi U ise sembol
            # arsivde TANIMSIZ (baska arsivden bekleniyor) demektir; onu "var"
            # saymak denetimi sahte yesile cevirirdi.
            return set(re.findall(pat, out.stdout, re.M))
    return None


# (arsiv yolu, insan adi, eksiklik HATA mi, tazeleme betigi, kaynak listesi
#  "driver" ise surucuden okunan liste kullanilir)
FRESHNESS_ARCHIVES = [
    ("wasm/dist/libtulpar_runtime_web.a", "web-runtime", True,
     "wasm/build_tame_web.sh", "driver"),
    ("wasm/dist/libtulpar_tame_web.a", "web-tame", True,
     "wasm/build_tame_web.sh", "driver"),
    ("android/dist/arm64-v8a/libtulpar_runtime_android.a",
     "android/arm64-v8a-runtime", False, "android/build_tame_android.sh", "driver"),
    ("android/dist/arm64-v8a/libtulpar_tame_android.a",
     "android/arm64-v8a-tame", False, "android/build_tame_android.sh", "driver"),
    ("android/dist/x86_64/libtulpar_runtime_android.a",
     "android/x86_64-runtime", False, "android/build_tame_android.sh", "driver"),
    ("android/dist/x86_64/libtulpar_tame_android.a",
     "android/x86_64-tame", False, "android/build_tame_android.sh", "driver"),
]


def driver_staleness_sources():
    """aot_pipeline.cpp'deki `warn_if_prebuilt_archive_stale` srcs[] listesi.

    None donerse cagiran KIRMIZI sayar: desen bayatlamissa bu denetim
    kaynaksiz kalir ve sessizce hicbir sey karsilastirmaz.
    """
    path = os.path.join(ROOT, "src/aot/aot_pipeline.cpp")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as fh:
        src = fh.read()
    m = re.search(r"warn_if_prebuilt_archive_stale.*?srcs\[\]\s*=\s*\{(.*?)\}\s*;",
                  src, re.S)
    if not m:
        return None
    files = re.findall(r'"([^"]+\.(?:c|cpp|h|hpp))"', m.group(1))
    return files or None


def check_archive_freshness(driver_srcs):
    """Var olan her arsiv, kaynaklarindan YENI mi?

    Doner: (hata_var_mi, bayat_sayisi). Arsiv yoksa atlanir (o hedef
    kullanilmiyor). Zaman damgasi kaba ama EYLEME GECIRILEBILIR bir olcut:
    "tazele" komutu ekranda yaziyor.
    """
    bad = 0
    stale = 0
    for rel, label, hard, refresh, which in FRESHNESS_ARCHIVES:
        full = os.path.join(ROOT, rel)
        if not os.path.exists(full):
            continue
        srcs = driver_srcs
        amtime = os.path.getmtime(full)
        newer = []
        for s in srcs:
            sp = os.path.join(ROOT, s)
            if not os.path.exists(sp):
                continue
            if os.path.getmtime(sp) > amtime:
                newer.append(s)
        if not newer:
            continue
        head = "HATA" if hard else "UYARI"
        print("%s: %s arsivi BAYAT — %d kaynak dosyasi arsivden YENI. Sembol "
              "denetimi bunu goremez: var olan bir sembolun IMZASI degistiyse "
              "sembol hala 'var' gorunur, ama web'de 'function signature "
              "mismatch', android'de argumani okumayan kod demektir."
              % (head, label, len(newer)))
        for s in newer[:5]:
            print("    %s" % s)
        if len(newer) > 5:
            print("    ... (+%d)" % (len(newer) - 5))
        print("    tazele: %s" % refresh)
        stale += 1
        if hard:
            bad += 1
    return bad, stale


# NDK araması İKİ yerde yazılı: sürücü (aot_pipeline.cpp, derlemeyi yapan) ve
# betik (build_tame_android.sh, arşivleri üreten). Ayrışırlarsa biri NDK'yı
# bulur öteki bulmaz — ölçüldü: ikisi de yalnız `~/Android/android-ndk-*`'a
# bakıyordu ve Android Studio'nun kurduğu `~/Android/Sdk/ndk/*` görünmüyordu,
# yani makinede çalışır NDK dururken Android hedefi kullanılamıyordu.
NDK_SPOTS = ["ANDROID_HOME", "Android/Sdk/ndk", "Library/Android/sdk/ndk",
             "android-ndk-"]


def check_ndk_search_agrees():
    files = ["src/aot/aot_pipeline.cpp", "android/build_tame_android.sh"]
    texts = {}
    for rel in files:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            return 0
        with open(path, encoding="utf-8", errors="replace") as fh:
            texts[rel] = fh.read()
    bad = 0
    for spot in NDK_SPOTS:
        missing = [rel for rel in files if spot not in texts[rel]]
        if missing:
            print("HATA: NDK aramasi ayrisik — '%s' su dosyalarda yok: %s"
                  % (spot, ", ".join(missing)))
            bad += 1
    return bad


def main():
    wanted = table_symbols()
    fail = False
    checked = 0
    dirty = 0
    for rel, label, hard, refresh in ARCHIVES:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue          # hedef kullanılmıyor
        have = archive_symbols(path)
        if have is None:
            print("dist denetimi: %s okunamadi (llvm-nm/nm yok) — atlandi" % label)
            continue
        checked += 1
        missing = [s for s in wanted if s not in have]
        if not missing:
            continue
        head = "HATA" if hard else "UYARI"
        print("%s: %s arsivinde %d builtin YOK — bu hedefin her derlemesi "
              "link'te patlar" % (head, label, len(missing)))
        for sym in missing[:10]:
            print("    %s" % sym)
        if len(missing) > 10:
            print("    ... (+%d)" % (len(missing) - 10))
        print("    tazele: %s" % refresh)
        dirty += 1
        if hard:
            fail = True
    # --- Cekirdek runtime arsivleri -----------------------------------------
    core = codegen_runtime_symbols()
    for rel, label, hard, refresh in RUNTIME_ARCHIVES:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        have = archive_symbols(path)
        if have is None:
            print("dist denetimi: %s okunamadi (llvm-nm/nm yok) — atlandi" % label)
            continue
        checked += 1
        skip = EXPECTED_MISSING.get(label, ())
        missing = [x for x in core if x not in have and not x.startswith(skip)]
        if not missing:
            continue
        head = "HATA" if hard else "UYARI"
        print("%s: %s arsivinde codegen'in bildirdigi %d cekirdek sembol YOK — "
              "bu hedefin her derlemesi link'te patlar" % (head, label, len(missing)))
        for sym in missing[:10]:
            print("    %s" % sym)
        if len(missing) > 10:
            print("    ... (+%d)" % (len(missing) - 10))
        print("    tazele: %s" % refresh)
        dirty += 1
        if hard:
            fail = True

    # --- Arsiv tazeligi (imza degisikligi sinifi) ---------------------------
    driver_srcs = driver_staleness_sources()
    if driver_srcs is None:
        print("HATA: aot_pipeline.cpp'de warn_if_prebuilt_archive_stale srcs[] "
              "listesi okunamadi — tazelik denetimi KAYNAKSIZ kaldi")
        fail = True
    else:
        fbad, fstale = check_archive_freshness(driver_srcs)
        if fbad:
            fail = True
        dirty += fstale

    if check_ndk_search_agrees():
        print("    surucu ve betik AYNI yerlere bakmali (ikisi de Android "
              "hedefinin parcasi)")
        fail = True
    if fail:
        return 1
    if dirty:
        # "Temiz" YAZMA: uyarı basıp temiz demek, uyarıyı gürültüye çevirir —
        # bu denetimin kapatmaya çalıştığı hatanın ta kendisi.
        print("dist arsiv denetimi: %d arsiv denetlendi, %d sorun BAYAT/EKSIK "
              "(yukariya bak)" % (checked, dirty))
        return 0
    print("dist arsiv denetimi temiz (%d arsiv, %d tame builtin, %d cekirdek "
          "sembol, %d arsivde tazelik)"
          % (checked, len(wanted), len(core),
             sum(1 for r in FRESHNESS_ARCHIVES if os.path.exists(os.path.join(ROOT, r[0])))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
