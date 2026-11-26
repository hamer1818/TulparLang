#!/usr/bin/env python3
"""S3 — arena sozlesmesi (2026-11-26'da DUZELTILDI).

Sozlesme:
    arena_restore ve arena_drop AYNI geri sarmayi yapar (aot_arena_rewind_to);
    fark yalniz checkpoint yiginin tepesi (restore korur, drop birakir).
    -> drop kolu ve tek save + cok restore kolu: RSS DUZ
    -> drop'suz TEKRARLANAN arena_save: 33. kayit yakalanabilir hata (RFC C3);
       eskiden sessizce -1 donup sizdiriyordu
    -> arena yonetimi yok: RSS tirmanir (kontrol)

Bu kapinin ilk hali (FINDINGS M3) "restore SERBEST BIRAKMAZ" diyordu ve onu
"restore kolu none'dan iyi degil" diye olcuyordu. Olcum dogruydu, okuma
yanlisti: o kol her iterasyonda arena_save cagiriyordu; 33. kayit -1 donunce
restore(-1) no-op oluyordu. Sizan restore degil, drop'suz tekrarlanan save'di.

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
ARMS = ("idle", "drop", "restore_tek", "none")
LOUD_ARM = "dropsuz_tekrar"   # RSS'i degil, cikis kodunu + mesaji olculur
RUNS = 3
# Kapilar olculen degerlerden (drop 11 MB · restore 161 MB · none 143 MB,
# 2026-09-11) DEGIL, aralarindaki BUYUKLUK FARKINDAN turetildi; makineye
# gore RSS kayar, oran kaymaz.
FLAT_RATIO = 4.0          # none / (drop | restore_tek) en az bu kadar olmali

RED, GREEN, RESET = "\033[0;31m", "\033[0;32m", "\033[0m"
fails = []


def check(name, cond, detail):
    print("  %s %-44s %s" % (GREEN + "PASS" + RESET if cond else RED + "FAIL" + RESET, name, detail))
    if not cond:
        fails.append(name)


def peak_rss_kb(binary, env):
    """Cocugun VmHWM zirvesi — /proc'tan yoklanarak. VmHWM monoton, yani
    son basarili okuma zirvedir."""
    p = subprocess.Popen([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
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
    out, err = p.communicate()
    peak_rss_kb.last_err = err.decode("utf-8", "replace")
    return hi, out.decode("utf-8", "replace").strip(), p.returncode


def main():
    if not sys.platform.startswith("linux"):
        print("S3: /proc yok, atlandi (%s)" % sys.platform)
        return 0
    tmp = tempfile.mkdtemp(prefix="s3_")
    env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="")
    peak, outs, rcs = {}, {}, {}
    print("S3 — arena sozlesmesi (restore = drop geri sarmasi; drop'suz tekrar gurultulu)")
    for arm in ARMS + (LOUD_ARM,):
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
        if arm == LOUD_ARM:
            outs["_loud_err"] = peak_rss_kb.last_err
    print("  zirve RSS: " + "  ".join("%s=%d KB" % (a, peak[a]) for a in ARMS))

    base = peak["idle"]
    flat = ("drop", "restore_tek", "none")
    grow = {a: peak[a] - base for a in flat}
    print("  acilis tabani %d KB cikarilinca: " % base +
          "  ".join("%s=+%d KB" % (a, grow[a]) for a in grow))

    # SAGLIK ONCE. Bu iki kontrol sus payi degil: ilk yazilislarinda N=200000
    # secilmisti ve UC KOL DA SIGSEGV veriyordu — RSS tablosu uc farkli COKUS
    # NOKTASININ orani cikti, "15x" gibi ikna edici gorunerek. Cikis kodunu ve
    # ciktiyi denetlemeyen bir bellek olcumu, olcemedigi bir programi olcuyor
    # olabilir.
    check("uc kol da temiz bitti (cokme yok)",
          all(all(c == 0 for c in rcs[a]) for a in flat),
          " ".join("%s=%s" % (a, rcs[a]) for a in flat))
    check("uc kol ayni sonucu uretiyor",
          outs["drop"] == outs["restore_tek"] == outs["none"] and outs["drop"] != "",
          "cikti=%r" % outs["drop"])

    # (1) drop DUZ: none ile arasinda buyuk fark OLMALI.
    r_drop = grow["none"] / max(grow["drop"], 1)
    check("drop geri kazaniyor (none/drop >= %.1fx)" % FLAT_RATIO,
          r_drop >= FLAT_RATIO, "oran=%.1fx" % r_drop)
    # (2) tek save + cok restore de DUZ — restore serbest BIRAKIR.
    r_rest = grow["none"] / max(grow["restore_tek"], 1)
    check("restore geri kazaniyor (none/restore_tek >= %.1fx)" % FLAT_RATIO,
          r_rest >= FLAT_RATIO, "oran=%.1fx" % r_rest)
    # (3) drop'suz tekrar SESSIZ SIZINTI DEGIL: hata ile erken biter.
    loud_ok = (all(c == 1 for c in rcs[LOUD_ARM]) and outs[LOUD_ARM] == "" and
               "kontrol noktasi yigini dolu" in outs.get("_loud_err", ""))
    check("drop'suz tekrar: hata + cikis 1 (sessiz -1 yok)", loud_ok,
          "rc=%s cikti=%r" % (rcs[LOUD_ARM], outs[LOUD_ARM][:40]))

    if fails:
        print(RED + "S3 sozlesmesi IHLAL: %d kontrol dustu" % len(fails) + RESET +
              " (%s)" % ", ".join(fails))
        return 1
    print(GREEN + "S3 kilitli" + RESET + " — drop ve tek-save/restore duz, none tirmaniyor, drop'suz tekrar gurultulu")
    return 0


if __name__ == "__main__":
    sys.exit(main())
