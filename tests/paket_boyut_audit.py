#!/usr/bin/env python3
"""Paket boyutu denetimi.

NEDEN
-----
"Sessizce bozulan" sinifindan: mobil kurulum boyutu bir butcedir
(PLAN EK G.3: temel modul < 200 MB). Butce ancak OLCULUP esige baglanirsa
butcedir; yoksa "bir ara bakariz" olur.

NOT: bu denetim eskiden SPIR-V tazeligini ve motor acilis suresini de
olcuyordu. Ikisi de motorun shader agacini / eng_init'ini okuyordu; motor
2026-09-20'de ayri depoya (tulpar-engine) tasinirken onlar da oraya gitti.

ESIKLER
-------
Olculen degerin yaklasik iki kati secildi: amac "bir gun buyudu" degil,
"bir anda ZIPLADI" demek. Tek bir degisiklik boyutu iki katina cikardiysa
sebebi bilinmelidir (yanlislikla eklenen bir varlik, silinmeyen hata ayiklama
bilgisi, statik baglanan yeni bir kutuphane).

Cikis: 0 temiz / atlandi, 1 esik asildi.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MB = 1024 * 1024

# (yol, insan adi, esik bayt, gerekce)
# (yol, insan adi, esik bayt, gerekce) — esikler OLCULEN degerin ~2 kati.
SIZE_LIMITS = [
    ("libtulpar_runtime.a", "masaustu runtime arsivi", 8 * MB, "olculen 3.28 MB"),
    ("wasm/dist/libtulpar_runtime_web.a", "web runtime arsivi", 5 * MB, "olculen 2.06 MB"),
    ("wasm/dist/libtulpar_tame_web.a", "web tame arsivi", 5 * MB, "olculen 1.84 MB"),
    ("android/dist/arm64-v8a/libtulpar_runtime_android.a", "android runtime (arm64)", 7 * MB, "olculen 2.72 MB"),
]
# Arsiv GRUPLARI: tek tek degil, baglanacak yigin onemli. (Motor arsivleri
# 2026-09-20'de ayri depoya tasindi; su an grup denetlenecek aile yok.)
ARCHIVE_GROUPS = []
# Uretilen paketler: varsa olculur (yoksa atlanir — herkeste emsdk/NDK yok).
ARTIFACT_LIMITS = [
    (".wasm", "web wasm", 8 * MB, "arcade oyunu ~1.7 MB; oyun basina esik"),
    (".apk", "android apk", 32 * MB, "aksiyon APK 11.6 MB (iki ABI, STRIPLENMIS); 71 MB idi"),
]


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


def main():
    dirs = sys.argv[1:] or []
    bad = 0
    print("paket boyutu:")
    bad += check_sizes()
    bad += check_artifacts(dirs)
    print("android strip + sembol:")
    bad += check_android_strip(dirs)
    if bad:
        print("paket denetimi: %d SORUN" % bad)
        return 1
    print("paket denetimi temiz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
