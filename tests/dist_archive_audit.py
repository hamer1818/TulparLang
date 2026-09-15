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

UC AILE DENETLENIYOR
--------------------
  1. aot_tm_* / aot_tm3_*  (tame)   -> libtulpar_tame_{web,android}.a
  2. codegen'in adiyla bildirdigi cekirdek aot_* -> libtulpar_runtime_*.a
  3. aot_eng_* / teng_*    (motor koprusu) -> libtulpar_engine_android.a

Ucuncusu 2026-09-15'e kadar `OTHER_ARCHIVE_PREFIXES` ile ELENIYORDU: 156
builtin'lik aile hic denetlenmiyordu. Motor koprusu arsivi icin AYRI bir
kural var (asagida ENGINE_ARCHIVES): arsiv VARSA eksik sembol HATA. Gerekce
tame android satirlarindakinin tersi degil, devami — arsiv orada duruyorsa
gelistiricinin NDK'si VAR (o arsivi uretmis), yani kirmizi duzeltilebilir.
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
#   aot_eng_*             -> libtulpar_engine_* (ENGINE_ARCHIVES bolumu bakiyor)
# DIKKAT: burasi bir ELEME listesi, "denetlenmiyor" listesi DEGIL. Bir aileyi
# buraya ekleyip ona bakan bir bolum yazmamak = sessiz kapsam kaybi; aot_eng_*
# 2026-09-15'e kadar tam olarak bu durumdaydi.
OTHER_ARCHIVE_PREFIXES = ("aot_tm_", "aot_tm3_", "aot_eng_")


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
        # Tablolardan gelen semboller (tame + engine kopru) ayri satirlarda duruyor.
        syms.update(re.findall(r'\{\s*"[a-z0-9_]+"\s*,\s*"(aot_[A-Za-z0-9_]+)"\s*,\s*\d+\s*\}', src))
    for rel in ("src/aot/engine_builtins_table.inc",):
        path = os.path.join(ROOT, rel)
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as fh:
                syms.update(re.findall(r'"(aot_[A-Za-z0-9_]+)"', fh.read()))
    return sorted(x for x in syms if not x.startswith(OTHER_ARCHIVE_PREFIXES))


def archive_symbols(path, prefixes=("aot_",)):
    """Arşivdeki semboller; okuyacak araç yoksa None (denetim atlanır).

    `prefixes`: hangi aileler okunacak. Motor koprusu iki aile birden
    kullaniyor (`aot_eng_*_ptr` codegen'in bildirdigi ad, `teng_*` onun
    cagirdigi C ABI); ikisi de TANIMLI olmali, yoksa link'te patlar.
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


# --- Motor koprusu ailesi (aot_eng_* / teng_*) -------------------------------
# Arsiv yolu, insan adi, tazeleme betigi. Bu satirlarda UYARI yok, HATA var:
# dosya orada duruyorsa gelistirici NDK'ya sahip (o arsivi kendisi uretmis),
# yani eksik sembol DUZELTILEBILIR bir kirmizidir. Tame android satirlarindaki
# "duzeltilemez kirmizi ogretmez" gerekcesi burada gecerli degil.
ENGINE_ARCHIVES = [
    ("android/dist/arm64-v8a/libtulpar_engine_android.a", "android/arm64-v8a-engine",
     "engine/tools/build_bridge_android.sh"),
    ("android/dist/x86_64/libtulpar_engine_android.a", "android/x86_64-engine",
     "engine/tools/build_bridge_android.sh"),
]

# Motoru BILEREK tasimayan hedefler. Gorunmez `return` ile atlanmiyor: main()
# her kosumda bu satirlari GEREKCESIYLE ekrana yaziyor. "Neden bu hedef
# denetlenmedi" sorusunun cevabi ekranda yoksa kapsam kaybi gorunmez olur.
ENGINE_ABSENT_TARGETS = [
    ("web", "Emscripten'de Vulkan/WSI yok — motor web'e hic derlenmiyor; "
            "aot_pipeline.cpp web link kolunda motor arsivi gecmez, "
            "wasm/build_tame_web.sh motor arsivi uretmez"),
]

# Uretilen dosyalar (gen_engine_bindings.py SPEC -> bunlar). Her biri baska bir
# noktayi besliyor; biri bayat kalirsa o nokta sessizce kor olur.
ENGINE_GENERATED = [
    ("src/aot/engine_builtins_table.inc", "codegen tablosu"),
    ("runtime/engine_bindings.cpp", "VMValue ABI govdesi"),
    ("src/typeinfer/engine_builtins_sigs.inc", "tip cikarimi imzalari"),
    ("src/lsp/engine_builtins.inc", "LSP tamamlama"),
]


def engine_spec():
    """gen_engine_bindings.py'deki SPEC: [(ad, arite), ...] — TEK dogruluk kaynagi.

    Uretici dosya `__main__` korumali, yani ice aktarmanin yan etkisi yok.
    Okunamazsa None: cagiran bunu KIRMIZI sayar (sessizce atlanan bir kaynak,
    denetimin kapsamini yok eder).
    """
    path = os.path.join(ROOT, "engine/tools/gen_engine_bindings.py")
    if not os.path.exists(path):
        return None
    try:
        spec = importlib.util.spec_from_file_location("gen_engine_bindings", path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        rows = [(row[0], len(row[2])) for row in mod.SPEC]
    except Exception as exc:                      # noqa: BLE001 — sebebi yaz, yut ma
        print("HATA: gen_engine_bindings.py okunamadi (%s)" % exc)
        return None
    return rows or None


def check_engine_generated_fresh(spec):
    """SPEC <-> uretilmis 4 dosya ayrisik mi (uretici kosulmus mu).

    Arsiv GEREKTIRMEZ, her makinede kosar — kapinin negatif kontrolu de bu:
    temiz agacta 156/156 esit. Yakaladigi kusur: SPEC'e satir eklenip
    `python3 engine/tools/gen_engine_bindings.py` unutulunca codegen builtin'i
    tanimaz, typeinfer imzasiz cagri gorur, LSP tamamlamaz — ve derleme YESIL
    kalir, cunku eksik olan sey uretilmis bir dosyadir, yazilan bir dosya degil.
    """
    bad = 0
    names = [n for n, _a in spec]
    tbl_path = os.path.join(ROOT, "src/aot/engine_builtins_table.inc")
    if os.path.exists(tbl_path):
        with open(tbl_path, encoding="utf-8", errors="replace") as fh:
            rows = re.findall(r'\{\s*"([a-z0-9_]+)"\s*,\s*"(aot_[A-Za-z0-9_]+)"\s*,\s*(\d+)\s*\}',
                              fh.read())
        arity = dict((n, a) for n, a in spec)
        drift = [n for n, _sym, _ar in rows if n not in arity]
        drift += [n for n in names if n not in [r[0] for r in rows]]
        wrong = ["%s (%s != %d)" % (n, ar, arity[n])
                 for n, _sym, ar in rows if n in arity and int(ar) != arity[n]]
        if drift or wrong:
            print("HATA: motor codegen tablosu SPEC ile AYRISIK — "
                  "%d ad, %d arite farki" % (len(drift), len(wrong)))
            for x in (drift + wrong)[:8]:
                print("    %s" % x)
            bad += 1
    for rel, label in ENGINE_GENERATED:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            print("HATA: uretilmis motor dosyasi YOK: %s (%s)" % (rel, label))
            bad += 1
            continue
        with open(path, encoding="utf-8", errors="replace") as fh:
            body = fh.read()
        missing = [n for n in names if n not in body]
        if missing:
            print("HATA: %s (%s) BAYAT — SPEC'teki %d builtin yok: %s"
                  % (rel, label, len(missing), ", ".join(missing[:5])))
            bad += 1
    if bad:
        print("    tazele: python3 engine/tools/gen_engine_bindings.py")
    return bad


def android_engine_link_libs():
    """aot_pipeline.cpp'nin ANDROID motor link kolundaki -l<arsiv> listesi.

    Elle yazilmis bir liste DEGIL, cunku elle yazilan liste link satiriyla
    ayrisir ve ayrisma sessizdir. Desen tutmazsa None doner ve cagiran bunu
    KIRMIZI sayar: "desen bulamadim, o yuzden temiz" bu deponun en pahali
    hata sinifi (Tuzaklar 8aj).
    """
    path = os.path.join(ROOT, "src/aot/aot_pipeline.cpp")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as fh:
        src = fh.read()
    m = re.search(r"backend->uses_engine(.*?)\n\s*:\s*\"-ltulpar_tame_android",
                  src, re.S)
    if not m:
        return None
    libs = re.findall(r"-l(engine_[a-z0-9_]+|tulpar_engine_android)", m.group(1))
    return libs or None


def check_engine_companion_archives(libs):
    """Link'in istedigi her motor arsivi (a) uretici betikte, (b) var olan her
    ABI dizininde duruyor mu.

    Sembol denetimi yetmiyor: libtulpar_engine_android.a tastamam olsa bile
    libengine_jolt.a kopyalanmadiysa her `import "engine"` android derlemesi
    link'te oluyor ve masaustunde hicbir sey kizarmiyor.
    """
    bad = 0
    sh_rel = "engine/tools/build_bridge_android.sh"
    sh_path = os.path.join(ROOT, sh_rel)
    if os.path.exists(sh_path):
        with open(sh_path, encoding="utf-8", errors="replace") as fh:
            sh = fh.read()
        gone = [x for x in libs if x not in sh]
        if gone:
            print("HATA: link satirindaki %d motor arsivi uretici betikte (%s) "
                  "gecmiyor — hic kopyalanmaz: %s" % (len(gone), sh_rel, ", ".join(gone)))
            bad += 1
    for rel, label, refresh in ENGINE_ARCHIVES:
        full = os.path.join(ROOT, rel)
        if not os.path.exists(full):
            continue                  # atlandigi zaten yazildi
        d = os.path.dirname(full)
        gone = [x for x in libs if not os.path.exists(os.path.join(d, "lib%s.a" % x))]
        if gone:
            print("HATA: %s dizininde link'in istedigi %d arsiv YOK: %s"
                  % (label, len(gone), ", ".join(gone)))
            print("    tazele: %s" % refresh)
            bad += 1
    return bad


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

    # --- Motor koprusu arsivleri (aot_eng_* + teng_*) -----------------------
    eng_wanted = []
    espec = engine_spec()
    if espec is None:
        print("HATA: motor koprusu SPEC tablosu okunamadi — bu denetim "
              "KAPSAMINI kaybetti (engine/tools/gen_engine_bindings.py)")
        fail = True
    else:
        if check_engine_generated_fresh(espec):
            fail = True
        # Iki aile birden: codegen'in bildirdigi ad ve onun cagirdigi C ABI.
        eng_wanted = ["aot_%s_ptr" % n for n, _a in espec]
        eng_c_wanted = ["t%s" % n for n, _a in espec]
        for rel, label, refresh in ENGINE_ARCHIVES:
            path = os.path.join(ROOT, rel)
            if not os.path.exists(path):
                print("dist denetimi: %s atlandi: %s yok — once %s"
                      % (label, rel, refresh))
                continue
            have = archive_symbols(path, ("aot_eng_", "teng_"))
            if have is None:
                print("dist denetimi: %s okunamadi (llvm-nm/nm yok) — atlandi" % label)
                continue
            checked += 1
            for wanted_set, aile in ((eng_wanted, "kopru builtin'i (aot_eng_*_ptr)"),
                                     (eng_c_wanted, "motor C ABI'si (teng_*)")):
                missing = [x for x in wanted_set if x not in have]
                if not missing:
                    continue
                print("HATA: %s arsivinde %d %s YOK — `import \"engine\"` eden "
                      "her android derlemesi link'te patlar" % (label, len(missing), aile))
                for sym in missing[:10]:
                    print("    %s" % sym)
                if len(missing) > 10:
                    print("    ... (+%d)" % (len(missing) - 10))
                print("    tazele: %s" % refresh)
                fail = True
        libs = android_engine_link_libs()
        if libs is None:
            print("HATA: aot_pipeline.cpp'de android motor link kolu bulunamadi — "
                  "desen bayat, arsiv listesi denetimi KAPSAMSIZ")
            fail = True
        elif check_engine_companion_archives(libs):
            fail = True
    # Motorun BILEREK olmadigi hedefler: gerekcesiyle yaz, sessizce atlama.
    for label, why in ENGINE_ABSENT_TARGETS:
        print("dist denetimi: %s hedefinde motor koprusu ARANMADI — %s" % (label, why))

    if check_ndk_search_agrees():
        print("    surucu ve betik AYNI yerlere bakmali (ikisi de Android "
              "hedefinin parcasi)")
        fail = True
    if fail:
        return 1
    if dirty:
        # "Temiz" YAZMA: uyarı basıp temiz demek, uyarıyı gürültüye çevirir —
        # bu denetimin kapatmaya çalıştığı hatanın ta kendisi.
        print("dist arsiv denetimi: %d arsivin %d tanesi BAYAT (yukariya bak) "
              "— motor koprusu: %d builtin denetlendi"
              % (checked, dirty, len(eng_wanted)))
        return 0
    print("dist arsiv denetimi temiz (%d arsiv, %d tame builtin, %d cekirdek "
          "sembol, %d motor koprusu builtin x2 aile)"
          % (checked, len(wanted), len(core), len(eng_wanted)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
