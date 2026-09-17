#!/usr/bin/env python3
"""Paket boyutu + acilis suresi + SPIR-V tazelik denetimi.

NEDEN BU UC SEY BIR ARADA
-------------------------
Ucu de "sessizce bozulan" sinifindan (Tuzaklar 8aj/8am):

1. BOYUT: mobil kurulum boyutu bir butcedir (PLAN EK G.3: temel modul < 200 MB).
   Butce ancak OLCULUP esige baglanirsa butcedir; yoksa "bir ara bakariz" olur.
2. ACILIS: motor kurulumu (Vulkan + fizik + kaynak) uzarsa kimse fark etmez,
   cunku oyun yine calisir. Esik koymak farki gorunur kilar.
3. SPIR-V TAZELIK: shader kaynaklari GLSL, ama depoya giren sey URETILMIS
   *_spv.h dizileri. Kaynak degisip .h yeniden uretilmezse derleme YESIL kalir
   ve GPU ESKI shader'i kosturur — tam anlamiyla sessiz yanlis sonuc. Ayrica
   PLAN Faz 6 kapisi "runtime'da shader derlemesi yok" diyor; bunun kaniti
   SPIR-V'nin derleme zamaninda uretilip depoda durmasidir.

ESIKLER
-------
Olculen degerin yaklasik iki kati secildi: amac "bir gun buyudu" degil,
"bir anda ZIPLADI" demek. Tek bir degisiklik boyutu iki katina cikardiysa
sebebi bilinmelidir (yanlislikla eklenen bir varlik, silinmeyen hata ayiklama
bilgisi, statik baglanan yeni bir kutuphane).

Cikis: 0 temiz / atlandi, 1 esik asildi ya da SPIR-V bayat.
"""
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MB = 1024 * 1024

# (yol, insan adi, esik bayt, gerekce)
# (yol, insan adi, esik bayt, gerekce) — esikler OLCULEN degerin ~2 kati.
SIZE_LIMITS = [
    ("libtulpar_runtime.a", "masaustu runtime arsivi", 8 * MB, "olculen 3.28 MB"),
    ("wasm/dist/libtulpar_runtime_web.a", "web runtime arsivi", 5 * MB, "olculen 2.06 MB"),
    ("wasm/dist/libtulpar_tame_web.a", "web tame arsivi", 5 * MB, "olculen 1.84 MB"),
    ("android/dist/arm64-v8a/libtulpar_runtime_android.a", "android runtime (arm64)", 7 * MB, "olculen 2.72 MB"),
    ("android/dist/arm64-v8a/libtulpar_engine_android.a", "android kopru arsivi (arm64)", 5 * MB, "olculen 1.76 MB"),
]
# Motor arsivleri TOPLAMI: tek tek degil, cunku onemli olan baglanacak yigin.
# Jolt striplenmemis hata ayiklama bilgisiyle tek basina ~53 MB — bu sayi .so'ya
# inmez (linker olu kodu atar, strip kalanini alir), ama ZIPLARSA sebebi bilinmeli.
ARCHIVE_GROUPS = [
    ("android/dist/arm64-v8a", "engine_", "motor arsivleri toplami (arm64)", 160 * MB, "olculen ~73 MB"),
    ("android/dist/x86_64", "engine_", "motor arsivleri toplami (x86_64)", 160 * MB, "olculen ~66 MB"),
]
# Uretilen paketler: varsa olculur (yoksa atlanir — herkeste emsdk/NDK yok).
ARTIFACT_LIMITS = [
    (".wasm", "web wasm", 8 * MB, "arcade oyunu ~1.7 MB; oyun basina esik"),
    (".apk", "android apk", 32 * MB, "aksiyon APK 11.6 MB (iki ABI, STRIPLENMIS); 71 MB idi"),
]
STARTUP_LIMIT_S = 6.0  # masaustu, pencersiz: Vulkan + fizik + font + 1 kare


def human(n):
    return "%.2f MB" % (n / MB) if n >= MB else "%.1f KB" % (n / 1024.0)


def check_sizes():
    bad = 0
    for rel, label, limit, why in SIZE_LIMITS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            print("  atlandi: %s yok (%s)" % (label, rel))
            continue
        n = os.path.getsize(path)
        mark = "TAMAM" if n <= limit else "ESIK ASILDI"
        print("  %-34s %10s / %-10s %s  (%s)" % (label, human(n), human(limit), mark, why))
        if n > limit:
            bad += 1
    for d, prefix, label, limit, why in ARCHIVE_GROUPS:
        dpath = os.path.join(ROOT, d)
        if not os.path.isdir(dpath):
            print("  atlandi: %s yok (%s)" % (label, d))
            continue
        names = [n for n in os.listdir(dpath) if n.startswith("lib" + prefix) and n.endswith(".a")]
        if not names:
            print("  atlandi: %s bos" % label)
            continue
        total = sum(os.path.getsize(os.path.join(dpath, n)) for n in names)
        mark = "TAMAM" if total <= limit else "ESIK ASILDI"
        print("  %-34s %10s / %-10s %s  (%d arsiv, %s)" % (label, human(total), human(limit), mark, len(names), why))
        if total > limit:
            bad += 1
    return bad


def check_artifacts(extra_dirs):
    """Verilen dizinlerde uretilmis paketleri olcer (varsa)."""
    bad = 0
    seen = 0
    for d in extra_dirs:
        if not os.path.isdir(d):
            print("  atlandi: '%s' dizini yok (cagiran yanlis yol mu verdi?)" % d)
            continue
        for name in sorted(os.listdir(d)):
            for ext, label, limit, why in ARTIFACT_LIMITS:
                if not name.endswith(ext):
                    continue
                n = os.path.getsize(os.path.join(d, name))
                seen += 1
                mark = "TAMAM" if n <= limit else "ESIK ASILDI"
                print("  %-34s %10s / %-10s %s  (%s)" % (label + " " + name, human(n), human(limit), mark, why))
                if n > limit:
                    bad += 1
    if seen == 0:
        print("  atlandi: olculecek uretilmis paket yok (bakilan dizin: %s; "
              "once tulpar build --target=web/android)"
              % (", ".join(extra_dirs) if extra_dirs else "yok — dizin verilmedi"))
    return bad


def check_android_strip(dirs):
    """APK staging dizinlerinde: .so STRIPLENMIS mi ve semboller SAKLANMIS mi.

    NEDEN AYRI BIR DENETIM: boyut esigi tek basina yetmez. Birisi strip'i
    kaldirirsa .so 36 MB'a geri ciker ve esik bunu yakalar — ama TERS yonde bir
    hata sessiz kalir: strip yapilip sembol kopyasi ALINMAZSA paket kucuk kalir,
    esik yesil verir, ve cihazdaki yigin izinde fonksiyon adlari sessizce
    kaybolur. Bu depoda o adlar bir kez gercek bir derleyici hatasini tam
    yerinden gosterdi (Tuzaklar 8ap), yani kaybi ucuz degil.
    """
    bad = 0
    seen = 0
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for root, _dirs, _files in os.walk(d):
            if not root.endswith(("lib/arm64-v8a", "lib/x86_64")):
                continue
            so = os.path.join(root, "libtulpargame.so")
            if not os.path.exists(so):
                continue
            seen += 1
            abi = os.path.basename(root)
            stage = os.path.dirname(os.path.dirname(root))
            sym = os.path.join(stage, "symbols", abi, "libtulpargame.so")
            n = os.path.getsize(so)
            # .symtab varligi = striplenmemis. `readelf -S` yerine bayt tarama:
            # arac gerektirmesin (CI'da binutils olmayabilir).
            with open(so, "rb") as fh:
                blob = fh.read()
            stripped = b".symtab" not in blob
            if not stripped:
                print("  HATA: %s .so STRIPLENMEMIS (%s) — kurulum boyutu ~6x buyuk" % (abi, human(n)))
                bad += 1
            elif not os.path.exists(sym):
                print("  HATA: %s .so striplenmis ama SEMBOL KOPYASI YOK (%s) — cihazdaki yigin izi"
                      " adsiz kalir, geri cozulemez" % (abi, sym))
                bad += 1
            else:
                print("  %-12s .so %9s (striplenmis), semboller %9s saklandi" %
                      (abi, human(n), human(os.path.getsize(sym))))
    if seen == 0:
        print("  atlandi: APK staging dizini verilmedi (once tulpar build --target=android)")
    return bad


def check_spirv():
    """Her GLSL kaynagi icin *_spv.h GUNCEL mi: yeniden derle, baytlari karsilastir."""
    glslc = shutil.which("glslc")
    sh_dir = os.path.join(ROOT, "engine/rhi/shaders")
    if not os.path.isdir(sh_dir):
        print("  atlandi: shader dizini yok")
        return 0
    srcs = sorted(n for n in os.listdir(sh_dir) if n.endswith((".vert", ".frag", ".comp")))
    if not glslc:
        # glslc yoksa en azindan KARSILIGI VAR MI denetle: eksik .h sessiz kirilmadir.
        missing = [n for n in srcs
                   if not os.path.exists(os.path.join(sh_dir, n.replace(".", "_") + "_spv.h"))]
        if missing:
            print("  HATA: %d shader kaynaginin uretilmis basligi YOK: %s" % (len(missing), ", ".join(missing[:5])))
            return 1
        print("  atlandi: glslc yok (yalniz varlik denetlendi, %d shader)" % len(srcs))
        return 0
    stale = []
    for name in srcs:
        hdr = os.path.join(sh_dir, name.replace(".", "_") + "_spv.h")
        if not os.path.exists(hdr):
            stale.append(name + " (baslik yok)")
            continue
        out = subprocess.run([glslc, "-O", "--target-env=vulkan1.1", "-o", "-", os.path.join(sh_dir, name)],
                             capture_output=True)
        if out.returncode != 0:
            stale.append(name + " (derlenmiyor)")
            continue
        data = out.stdout
        # Baslik 32-bit KELIME dizisi tutar (0x%08x), bayt degil — bayt olarak
        # okumaya calisan bir denetim hic eslesme bulamaz ve HER shader'i bayat
        # ilan eder (bu denetimin ilk surumu tam bunu yapti).
        fresh = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
        with open(hdr, encoding="utf-8", errors="replace") as fh:
            body = fh.read()
        have = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{8})\b", body)]
        m = re.search(r"_spv_size\s*=\s*(\d+)", body)
        if have != fresh:
            # Sayilar esit olabilir: fark ICERIKTE. "376 != 376 kelime" yazan
            # bir tani, okuyan kisiye kapinin bozuk oldugunu dusundurur —
            # gercek farki (ilk ayrisan kelime) gostermek gerekiyor.
            if len(have) != len(fresh):
                stale.append("%s (%d != %d kelime)" % (name, len(have), len(fresh)))
            else:
                i = next(k for k in range(len(have)) if have[k] != fresh[k])
                stale.append("%s (kelime %d: baslikta 0x%08x, kaynaktan 0x%08x)"
                             % (name, i, have[i], fresh[i]))
        elif m and int(m.group(1)) != len(data):
            stale.append("%s (bayt sayaci %s != %d)" % (name, m.group(1), len(data)))
    if stale:
        print("  HATA: %d SPIR-V basligi BAYAT — GPU eski shader'i kosturur:" % len(stale))
        for s in stale[:8]:
            print("      %s" % s)
        print("      tazele: python3 engine/tools/compile_shaders.py")
        return 1
    print("  %d shader kaynagi ve uretilmis basligi KELIME ESIT (runtime'da derleme yok)" % len(srcs))
    return 0


def check_startup():
    """Pencersiz motor kurulumu: eng_init -> ilk kare. Olcum: tek karelik kosum."""
    tulpar = None
    # `build/` de aday: CI workflow'u orada derliyor (build.sh'in kendi
    # yorumu: "CI `build/` icinde derliyor"). Listede yoksa CI'da ikili
    # bulunamaz ve acilis olcumu sessizce atlanir.
    for cand in ("tulpar", "build-linux/tulpar", "build-macos/tulpar", "build/tulpar"):
        p = os.path.join(ROOT, cand)
        if os.path.exists(p):
            tulpar = p
            break
    game = os.path.join(ROOT, "examples/engine_ilk_oyun.tpr")
    if not tulpar or not os.path.exists(game):
        print("  atlandi: tulpar ikilisi ya da ornek yok")
        return 0
    env = dict(os.environ)
    env.update({"TULPAR_ENGINE_HEADLESS": "1", "TULPAR_ENGINE_LOG": "1", "DISPLAY": ""})
    t0 = time.time()
    try:
        r = subprocess.run([tulpar, game], capture_output=True, text=True, env=env, timeout=180)
    except subprocess.TimeoutExpired:
        print("  HATA: acilis 180 s icinde bitmedi")
        return 1
    dt = time.time() - t0
    if r.returncode < 0:
        # Sinyalle olmek ORTAM EKSIKLIGI DEGIL: bu bir cokme. Ayirt edilmezse
        # cokme "GPU yok" gibi atlanir ve kapi hic kizarmaz (bu deponun en
        # pahali hata sinifi: sessiz yesil).
        print("  HATA: motor sinyal %d ile COKTU — cikti: %s"
              % (-r.returncode, (r.stdout.strip().splitlines()[-3:] or ["yok"])))
        return 1
    if "motor hazir" not in r.stdout:
        print("  atlandi: motor kurulamadi (GPU yok?) — cikis %d, cikti: %s"
              % (r.returncode, (r.stdout.strip().splitlines()[-1:] or ["yok"])))
        return 0
    # Not: sure AOT DERLEMESINI de icerir (tulpar kaynagi derleyip kosturur),
    # yani bu "oyuncunun gordugu acilis" degil, gelistiricinin gordugu tam tur.
    mark = "TAMAM" if dt <= STARTUP_LIMIT_S else "ESIK ASILDI"
    print("  %-34s %9.2f s / %-8s %s  (AOT derleme + motor kurulumu + 1 kare)" % ("acilis (pencersiz, tam tur)", dt, "%.1f s" % STARTUP_LIMIT_S, mark))
    return 0 if dt <= STARTUP_LIMIT_S else 1


def main():
    dirs = sys.argv[1:] or []
    bad = 0
    print("paket boyutu:")
    bad += check_sizes()
    bad += check_artifacts(dirs)
    print("android strip + sembol:")
    bad += check_android_strip(dirs)
    print("SPIR-V tazelik:")
    bad += check_spirv()
    print("acilis suresi:")
    bad += check_startup()
    if bad:
        print("paket denetimi: %d SORUN" % bad)
        return 1
    print("paket denetimi temiz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
