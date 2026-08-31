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
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Arşiv yolu, insan adı, eksik sembol HATA mı (True) yoksa UYARI mı (False).
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


def archive_symbols(path):
    """Arşivdeki semboller; okuyacak araç yoksa None (denetim atlanır)."""
    for tool in ("llvm-nm", "nm"):
        try:
            out = subprocess.run([tool, path], capture_output=True, text=True)
        except FileNotFoundError:
            continue
        if out.returncode == 0 and out.stdout.strip():
            return set(re.findall(r"\b(aot_[A-Za-z0-9_]+)\b", out.stdout))
    return None


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
    if fail:
        return 1
    if dirty:
        # "Temiz" YAZMA: uyarı basıp temiz demek, uyarıyı gürültüye çevirir —
        # bu denetimin kapatmaya çalıştığı hatanın ta kendisi.
        print("dist arsiv denetimi: %d arsivin %d tanesi BAYAT (yukariya bak)"
              % (checked, dirty))
        return 0
    print("dist arsiv denetimi temiz (%d arsiv, %d builtin)" % (checked, len(wanted)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
