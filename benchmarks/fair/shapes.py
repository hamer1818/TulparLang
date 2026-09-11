#!/usr/bin/env python3
"""Dizi ERISIM SEKILLERI — `matmul` acigi tam olarak nerede?

Neden bu dosya var (2026-09-11 bagimsiz denetimi): `FINDINGS.md`'de dort
seklin olcusu TABLO olarak duruyordu ama onu ureten bir kosucu YOKTU. O
tablo, S11 kararinin ("float-dizi unboxing refactor'u YAPILMADI, ~117
deginme") tek dayanagi. B bolumunun standarti "tablo elle yazilmiyor"; bu
tablo elle yaziliydi. Karar kaydi vardi, OLCUMU yeniden uretilemiyordu.

Ne olcuyor: 40M elemanli int dizileri uzerinde YALNIZ erisim sekli
degisiyor, veri ve is ayni:

    lineer okuma     t += a[i]
    hesapli indeks   t += a[i*2]
    iki dizi         t += a[i] * b[i]
    oku-yaz          a[i] = a[i] + b[i]      <- matmul'un ic dongusu

Beklenen sonuc (FINDINGS M-serisi): indeks aritmetigi BEDAVA, maliyet
ELEMAN YAZMA yolunda. Sayilar makineye gore kayar; SIRALAMA kaymaz ve
iddia budur.

--------------------------------------------------------------------------
⚠ BU OLCUMUN IKI TUZAGI VAR. Ikisine de denetim sirasinda DUSULDU:

1. TEKRAR SARMALAYICISI OLCUMU 20x BOZUYOR. "En iyi 5" icin olculen
   donguyu bir `for (int r = 0; r < 5; ...)` icine koymak, Tulpar'da lineer
   okumayi 5 ms'den 107 ms'ye cikardi — ic dongu bir optimizasyon yolunu
   kaybediyor. Yani "en iyi 5" almak, olculen seyi DEGISTIRIYOR. Bu yuzden
   her sekil TEK GECIS kosar ve tekrar, SURECI yeniden baslatarak alinir.

2. CIKTI KULLANILMAZSA DONGU YOK OLUR. Ilk yazimda program `dt`yi basip
   toplami basmiyordu; LLVM'in olcumu bastan silmesi icin bu yeterli
   (R11 taramasinin da ayni dersi var, #23). Her sekil toplami BASAR ve
   C ile Tulpar'in toplamlari BIREBIR karsilastirilir — tutmazsa satir
   gecersizdir, sayi ne olursa olsun.

3. DONGU SINIRI `len(a)` OLMALI — `n` DEGIL. Ayni is, ayni cikti, ama
   40M lineer okuma `len(a)` siniriyla 5 ms, `n` siniriyla 18 ms suruyor
   (2026-09-11 olcumu, 3,5x). Sinir diziye baglandiginda derleyici indeksin
   sinir icinde oldugunu KANITLIYOR ve sinir denetimini dusuruyor ("kanitli
   erisim"; `./build.sh suites` bunu "dizi elemani 32-bit (kanitli erisim
   i32)" satiriyla kilitliyor). Depo kendi kiyasinda da boyle yaziyor
   (`benchmarks/fair/arrayiter.tpr`). `n` ile olcen biri Tulpar'i 3,5x
   yavas sanir — dilin degil, yazim seklinin olcusudur.
   Istisna: "hesapli indeks" seklinde sinir `lim = n / 2`dir; adim 2
   oldugundan kanit yolu kurulamaz ve bu SEKLIN KENDI ozelligidir.
--------------------------------------------------------------------------

Kullanim:
    python3 benchmarks/fair/shapes.py              # tablo
    SHAPE_N=10000000 python3 benchmarks/fair/shapes.py
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TULPAR = ROOT / "tulpar"
N = int(os.environ.get("SHAPE_N", "40000000"))
REPEATS = int(os.environ.get("SHAPE_REPEATS", "3"))

RED, GREEN, RESET = "\033[0;31m", "\033[0;32m", "\033[0m"

# (ad, C govdesi, Tulpar govdesi, Tulpar on-tanimi, ikinci dizi gerekli mi)
# Her govde `t`yi besler; her iki dil de `t`yi basar (cikti mutabakati).
#
# ⚠ `b` YALNIZ GEREKTIGINDE ayrilir. Ilk yazimda her sekil iki diziyi birden
# ayiriyordu ve tek-dizi sekilleri, KULLANMADIKLARI 320 MB'lik ikinci
# tahsisin sayfa-hatasi/onbellek etkisini olcuyordu: "lineer okuma" 0,5x
# yerine 1,8x cikiyordu. Olculen sey sekil olmali, kurulum degil.
SHAPES = [
    ("lineer okuma",
     "for (long i = 0; i < n; i++) t += a[i];",
     "for (int i = 0; i < len(a); i = i + 1) { t = t + a[i]; }",
     "", False),
    ("hesapli indeks",
     "for (long i = 0; i < lim; i++) t += a[i * 2];",
     "for (int i = 0; i < lim; i = i + 1) { t = t + a[i * 2]; }",
     "int lim = n / 2;", False),
    ("iki dizi",
     "for (long i = 0; i < n; i++) t += a[i] * b[i];",
     "for (int i = 0; i < len(a); i = i + 1) { t = t + a[i] * b[i]; }",
     "", True),
    ("oku-yaz",
     "for (long i = 0; i < n; i++) a[i] = a[i] + b[i];\n  t = a[n - 1];",
     "for (int i = 0; i < len(a); i = i + 1) { a[i] = a[i] + b[i]; }\n  t = a[n - 1];",
     "", True),
]

C_TMPL = """#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static long long us(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
int main(void) {
  long n = %(n)d;
  long lim = n / 2; (void)lim;
  long long *a = malloc((size_t)n * sizeof(long long));
%(balloc)s  for (long i = 0; i < n; i++) { a[i] = i; }
%(bfill)s
  long long t = 0;
  long long t0 = us();
  %(body)s
  long long dt = us() - t0;
  printf("%%lld %%lld\\n", dt / 1000, t);   /* ms, toplam */
  return 0;
}
"""

TPR_TMPL = """int n = %(n)d;
int[] a = array_fill(n, 0);
%(bdecl)sfor (int i = 0; i < n; i = i + 1) { a[i] = i; }
%(pre)s
int t = 0;
int t0 = time_ms();
%(body)s
int dt = time_ms() - t0;
print(toString(dt) + " " + toString(t));
"""


def run_once(cmd, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd,
                       env=dict(os.environ, DISPLAY="", WAYLAND_DISPLAY=""))
    if r.returncode != 0:
        return None, None
    parts = r.stdout.strip().split()
    if len(parts) != 2:
        return None, None
    return int(parts[0]), parts[1]


def best_of(cmd, cwd=None):
    """SURECI yeniden baslatarak tekrarla — ic dongu sarmalayicisi YASAK
    (bkz. dosya basligi, tuzak 1)."""
    best, total = None, None
    for _ in range(REPEATS):
        ms, t = run_once(cmd, cwd)
        if ms is None:
            return None, None
        if best is None or ms < best:
            best = ms
        total = t
    return best, total


def main():
    if not TULPAR.exists():
        print("HATA: ./tulpar yok — once ./build.sh calistirin.")
        return 2
    cc = "gcc" if subprocess.run(["which", "gcc"], capture_output=True).returncode == 0 else None
    if cc is None:
        print("HATA: gcc yok — karsilastirma tabani olusmuyor.")
        return 2

    tmp = Path(tempfile.mkdtemp(prefix="shapes_"))
    print("Dizi erisim sekilleri — %d eleman, tek gecis, en iyi %d surec"
          % (N, REPEATS))
    print("  %-16s %9s %9s %8s  %s" % ("sekil", "C ms", "Tulpar ms", "oran", "cikti"))

    fails, rows = [], []
    for idx, (name, cbody, tbody, pre, needs_b) in enumerate(SHAPES):
        csrc = tmp / ("s%d.c" % idx)
        csrc.write_text(C_TMPL % {
            "n": N, "body": cbody,
            "balloc": ("  long long *b = malloc((size_t)n * sizeof(long long));\n"
                       if needs_b else ""),
            "bfill": ("  for (long i = 0; i < n; i++) b[i] = 1;\n" if needs_b else ""),
        }, encoding="utf-8")
        cbin = tmp / ("s%d_c" % idx)
        if subprocess.run([cc, "-O2", "-o", str(cbin), str(csrc)],
                          capture_output=True).returncode != 0:
            fails.append("%s (C derlenmedi)" % name)
            continue
        cms, ctot = best_of([str(cbin)])

        tsrc = tmp / ("s%d.tpr" % idx)
        tsrc.write_text(TPR_TMPL % {
            "n": N, "body": tbody, "pre": pre,
            "bdecl": ("int[] b = array_fill(n, 1);\n" if needs_b else ""),
        }, encoding="utf-8")
        tbin = tmp / ("s%d_t" % idx)
        if subprocess.run([str(TULPAR), "build", str(tsrc), str(tbin)],
                          capture_output=True, cwd=str(ROOT)).returncode != 0:
            fails.append("%s (Tulpar derlenmedi)" % name)
            continue
        tms, ttot = best_of([str(tbin)])

        if cms is None or tms is None:
            fails.append("%s (kosmadi)" % name)
            continue
        # CIKTI MUTABAKATI: sayilar ancak ayni isi yaptiysa karsilastirilabilir.
        if ctot != ttot:
            print("  %-16s %9s %9s %8s  %sAYRISIYOR%s C=%s Tulpar=%s"
                  % (name, cms, tms, "-", RED, RESET, ctot, ttot))
            fails.append("%s (cikti ayrisiyor)" % name)
            continue
        oran = tms / cms if cms else 0.0
        rows.append((name, oran))
        print("  %-16s %9d %9d %7.2fx  %s" % (name, cms, tms, oran, ttot))

    if fails:
        print(RED + "Sekil olcumu GECERSIZ" + RESET + ": %s" % ", ".join(fails))
        return 1

    # IDDIA: eleman YAZMA en pahali sekil. Sayilar makineye gore kayar,
    # siralama kaymaz — olculen sey budur, mutlak ms degil.
    en_pahali = max(rows, key=lambda r: r[1])[0]
    if en_pahali != "oku-yaz":
        print(RED + "IDDIA CURUDU" + RESET +
              ": en pahali sekil 'oku-yaz' degil, '%s'. FINDINGS M-serisi ve "
              "S11 karari bu siralamaya dayaniyor — yeniden degerlendirin."
              % en_pahali)
        return 1
    print(GREEN + "Siralama korunuyor" + RESET +
          " — en pahali sekil 'oku-yaz' (%.2fx), indeks aritmetigi bedava (%.2fx)"
          % (dict(rows)["oku-yaz"], dict(rows)["hesapli indeks"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
