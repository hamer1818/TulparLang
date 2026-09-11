#!/usr/bin/env python3
"""Korpus tani taramasi — TABAN OLCUMU ICIN, ve tabani kendi sinayan.

Neden bu dosya var (#26): P23'un taban olcumunun ILK UC SURUMU "195 dosyada
0 tani" dedi ve UCU DE YESIL gorunuyordu:

  1. tanilar stdout'ta saniliyordu — stderr'e gidiyorlar.
  2. LC_ALL=C mesaji Ingilizceye ceviriyor — desen Turkce araniyordu.
  3. Ingilizce mesaj "Type Error" (buyuk E) — desen "Type error" ariyordu.

Ucu de #9b'nin ("olcum aleti de bir programdir") varyanti. O taban, P23'un
butun kapilarinin referansi olacakti; az kalsin "korpus tertemiz" diye
yayinlanacakti.

Bu yuzden tarama, is yapmadan ONCE kendini siniyor: tani URETMESI KESIN olan
bir fikstur (tests/typeinfer/fail/01_len_on_int.tpr) uzerinde calisiyor ve
tani goremezse HIC TARAMADAN hata veriyor. Taban olcum bir tablo degil, bir
TESPITTIR; once kendisi sinanir, sonra referans olur.

Kullanim:
    python3 tests/typecheck_corpus_scan.py            # ozet
    python3 tests/typecheck_corpus_scan.py --full     # normalize tam metin
    python3 tests/typecheck_corpus_scan.py --check    # TABANLA KARSILASTIR
    python3 tests/typecheck_corpus_scan.py --update   # tabani yeniden yaz

`--check` build.sh suites'te kosuyor. Taban SAYI degil METIN tutuyor: P23'un
kapisi ilk turda "6 dosya / 17 tani degismedi" diye sayiyla dogrulanmisti ve
sayinin ayni kalmasi ICERIGIN ayni kaldigini SOYLEMEZ (bir tani kaybolup
yerine baskasi gelebilirdi). Icerik-duzeyinde diff sonradan alindi ve 17
satirin metni de birebir ayni cikti — ama once kanit yoktu, sadece sayi
vardi. Taban artik metin.

Tani sayisi degistiginde bu kapi KIRMIZI verir ve tabani elle guncellemeni
ister. Amac tanilari yasaklamak degil; bir tanini SESSIZCE dogmasini ya da
kaybolmasini engellemek.
"""
import glob, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TULPAR = os.path.join(ROOT, "tulpar")
# Buyuk/kucuk harf duyarsiz VE iki dilde: ucuncu yanilsama tam burada dogdu.
PAT = re.compile(r"^\s*(Type\s+Error|Tip\s+Hatasi)\s*:", re.I)
SELFTEST = os.path.join("tests", "typeinfer", "fail", "01_len_on_int.tpr")


def run(path, env):
    r = subprocess.run([TULPAR, "typecheck", path], capture_output=True,
                       text=True, env=env, cwd=ROOT)
    # stdout VE stderr: ilk yanilsama yalniz stdout'a bakmakti.
    return [l.rstrip() for l in (r.stdout + r.stderr).splitlines() if PAT.match(l)]


def normalise(path, line):
    """Dosya yolunu basa koy, satir numarasini KORU, mutlak yollari at.
    Icerik-duzeyinde diff icin: sayi degil METIN karsilastirilacak."""
    line = line.replace(ROOT + "/", "")
    return "%s :: %s" % (path, line.strip())


def main():
    env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="", LC_ALL="C")
    # --- KENDINI SINAMA: tani gormeyen bir tarayici hicbir sey olcmez.
    probe = run(SELFTEST, env)
    if not probe:
        print("HATA: tarayici bilinen-pozitif fiksturde (%s) TANI GOREMEDI." % SELFTEST)
        print("      Tarama yapilmadi — bu bir 'temiz korpus' sonucu DEGIL,")
        print("      olcum aletinin bozuk oldugunun kanitidir (#26).")
        return 2

    files = []
    for pat in ("lib/*.tpr", "examples/*.tpr", "tests/*.tpr", "packages/*/*.tpr"):
        files += sorted(glob.glob(os.path.join(ROOT, pat)))
    rows = []
    for f in files:
        rel = os.path.relpath(f, ROOT)
        for l in run(f, env):
            rows.append(normalise(rel, l))

    if "--full" in sys.argv:
        for r in rows:
            print(r)
        return 0

    base_path = os.path.join(ROOT, "tests", "typecheck_corpus_baseline.txt")
    if "--update" in sys.argv:
        with open(base_path, "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")
        print("taban guncellendi: %d satir" % len(rows))
        return 0
    if "--check" in sys.argv:
        if not os.path.exists(base_path):
            print("HATA: taban dosyasi yok (%s) — --update ile olustur" % base_path)
            return 2
        want = [l.rstrip() for l in open(base_path, encoding="utf-8") if l.strip()]
        if want == rows:
            print("korpus tani tabani: %d tani, degismedi" % len(rows))
            return 0
        print("KORPUS TANI TABANI DEGISTI (sayi degil METIN karsilastirildi):")
        for l in sorted(set(rows) - set(want)):
            print("   + %s" % l)
        for l in sorted(set(want) - set(rows)):
            print("   - %s" % l)
        print("Kasitliysa: python3 tests/typecheck_corpus_scan.py --update")
        return 1

    per = {}
    for r in rows:
        per[r.split(" :: ")[0]] = per.get(r.split(" :: ")[0], 0) + 1
    print("kendini sinama: OK (%d tani gorundu)" % len(probe))
    print("dosya: %d · tanili dosya: %d · toplam tani: %d" % (len(files), len(per), len(rows)))
    for f in sorted(per):
        print("   %-46s %d" % (f, per[f]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
