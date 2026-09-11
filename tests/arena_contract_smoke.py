#!/usr/bin/env python3
"""S3 — arena sozlesmesi: `arena_restore` SERBEST BIRAKMAZ, `arena_drop` BIRAKIR.

Sozlesme (FINDINGS M2/M3):
    arena_save/arena_restore  -> checkpoint geri sarar ama BELLEGI IADE ETMEZ
    arena_drop                -> iade eder; dongu icinde RSS duz kalir

Neden fikstur (#21): cumle defterde vardi, hicbir test onu sinamiyordu.
`arena_restore` uc suite'te GECIYOR ama hepsi "kalici deger hayatta kalir"
yonunu sinaniyor — sozlesmenin OBUR YARISI ("serbest birakmaz") hic
sinanmamisti. S4'le ayni hata sinifi: sozlesmenin yarisi testte, yarisi
satirda.

Uc kol AYNI govdeyi kosar; tek fark kapanis cagrisidir (kural: FARKI olc,
sonucu degil). `none` kolu — hic arena yonetimi yok — kontroldur: restore
ondan IYI DEGILSE, restore hicbir sey yapmiyor demektir.

KIRMIZIYA DONEBILIRLIK (#10): iki iddia ZIT yonlerde fark talep ediyor —
(1) drop ile restore ARASINDA buyuk fark olmali, (2) restore ile none
ARASINDA fark olMAmali. Sabit sayi donduren kor bir olcum (1)'i, her kola
rastgele sayi donduren biri (2)'yi dusurur. Olcum sabit de olamaz, gurultu
de.
"""
import os, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "tests", "fixtures")
ARMS = ("idle", "drop", "restore", "none")
RUNS = 3
# Kapilar olculen degerlerden (drop 11 MB · restore 161 MB · none 143 MB,
# 2026-09-11) DEGIL, aralarindaki BUYUKLUK FARKINDAN turetildi; makineye
# gore RSS kayar, oran kaymaz.
DROP_FLAT_RATIO = 4.0     # restore/drop en az bu kadar olmali
RESTORE_NO_HELP = 0.7     # restore >= none * bu  (yani restore yardim etmiyor)

RED, GREEN, RESET = "\033[0;31m", "\033[0;32m", "\033[0m"
fails = []


def check(name, cond, detail):
    print("  %s %-44s %s" % (GREEN + "PASS" + RESET if cond else RED + "FAIL" + RESET, name, detail))
    if not cond:
        fails.append(name)


def peak_rss_kb(binary, env):
    """Cocugun VmHWM zirvesi — /proc'tan yoklanarak. VmHWM monoton, yani
    son basarili okuma zirvedir."""
    p = subprocess.Popen([binary], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    hi = 0
    while p.poll() is None:
        try:
            with open("/proc/%d/status" % p.pid) as f:
                for ln in f:
                    if ln.startswith("VmHWM:"):
                        hi = max(hi, int(ln.split()[1]))
                        break
        except (IOError, OSError):
            break
        time.sleep(0.005)
    out, _ = p.communicate()
    return hi, out.decode("utf-8", "replace").strip(), p.returncode


def main():
    if not sys.platform.startswith("linux"):
        print("S3: /proc yok, atlandi (%s)" % sys.platform)
        return 0
    tmp = tempfile.mkdtemp(prefix="s3_")
    env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="")
    peak, outs, rcs = {}, {}, {}
    print("S3 — arena sozlesmesi (restore birakmaz / drop birakir)")
    for arm in ARMS:
        src = os.path.join(FIX, "arena_%s.tpr" % arm)
        binary = os.path.join(tmp, arm)
        b = subprocess.run([os.path.join(ROOT, "tulpar"), "build", src, binary],
                           capture_output=True, env=env, cwd=ROOT)
        if b.returncode != 0:
            print(RED + "FAIL" + RESET + " %s derlenmedi" % arm)
            print(b.stdout.decode("utf-8", "replace")[-1500:])
            return 1
        vals = [peak_rss_kb(binary, env) for _ in range(RUNS)]
        peak[arm] = max(v[0] for v in vals)
        outs[arm] = vals[0][1]
        rcs[arm] = [v[2] for v in vals]
    print("  zirve RSS: " + "  ".join("%s=%d KB" % (a, peak[a]) for a in ARMS))

    base = peak["idle"]
    grow = {a: peak[a] - base for a in ("drop", "restore", "none")}
    print("  acilis tabani %d KB cikarilinca: " % base +
          "  ".join("%s=+%d KB" % (a, grow[a]) for a in grow))

    # SAGLIK ONCE. Bu iki kontrol sus payi degil: ilk yazilislarinda N=200000
    # secilmisti ve UC KOL DA SIGSEGV veriyordu — RSS tablosu (drop 11 MB /
    # restore 161 MB / none 143 MB) uc farkli COKUS NOKTASININ orani cikti,
    # "15x" gibi ikna edici gorunerek. Cikis kodunu ve ciktiyi denetlemeyen
    # bir bellek olcumu, olcemedigi bir programi olcuyor olabilir.
    check("uc kol da temiz bitti (cokme yok)",
          all(all(c == 0 for c in rcs[a]) for a in ("drop", "restore", "none")),
          " ".join("%s=%s" % (a, rcs[a]) for a in ("drop", "restore", "none")))

    # Dogruluk once: uc kol AYNI isi yapmali, yoksa RSS karsilastirmasi anlamsiz.
    check("uc kol ayni sonucu uretiyor",
          outs["drop"] == outs["restore"] == outs["none"] and outs["drop"] != "",
          "cikti=%r" % outs["drop"])

    # (1) drop DUZ: restore ile arasinda buyuk fark OLMALI.
    ratio = grow["restore"] / max(grow["drop"], 1)
    check("drop bellegi IADE EDIYOR (restore/drop >= %.1fx)" % DROP_FLAT_RATIO,
          ratio >= DROP_FLAT_RATIO, "oran=%.1fx" % ratio)

    # (2) restore YARDIM ETMIYOR: none ile arasinda fark OLMAMALI.
    share = grow["restore"] / max(grow["none"], 1)
    check("restore SERBEST BIRAKMIYOR (restore >= none * %.2f)" % RESTORE_NO_HELP,
          share >= RESTORE_NO_HELP, "restore/none=%.2f" % share)

    if fails:
        print(RED + "S3 sozlesmesi IHLAL: %d kontrol dustu" % len(fails) + RESET +
              " (%s)" % ", ".join(fails))
        return 1
    print(GREEN + "S3 kilitli" + RESET + " — iki zit yonlu iddia, kor olcum ikisini birden gecemez")
    return 0


if __name__ == "__main__":
    sys.exit(main())
