#!/usr/bin/env python3
"""`tulpar fmt` bozuk kod uretiyor mu — ve idempotent mi.

NEDEN VAR (olculdu 2026-09-16): bicimlendirici BILINMEYEN bir isleci karakter
karakter bosluklayinca gecerli kaynagi BOZUYOR. Iki gercek vaka:
  a << 2   -> a < <  2      (bit isleci tanimli degilken)
  1.5e-8   -> 1.5e - 8      (us isareti ikili islec sanildi)
Ikincisi UZUN SUREDIR oradaydi: `tests/scientific_notation.test.tpr`
bicimlendirilince 20 ayristirma hatasi veriyordu ve kimse sormuyordu.

Sebep, denetimin YANLIS SEYI olcmesiydi: bicimlendirici yalnizca IDEMPOTANS
icin sinaniyordu. Idempotans BOZUK bir cikti icin de saglanir — bozuk metni
ikinci kez bicimlendirmek ayni bozuk metni verir. Yani "kararli" ile "dogru"
karistirilmisti.

Bu denetim iki seyi birden sorar:
  1. DERLENEBILIRLIK: bicimlendirilmis kaynagin ayristirma/tip hatasi sayisi,
     bicimlendirme ONCESINE gore ARTMAMALI. (Mutlak sifir istenmiyor: depoda
     zaten bilerek hatali fixture'lar var.)
  2. IDEMPOTANS: iki kez bicimlendirmek ayni metni vermeli.

Cikis: 0 temiz, 1 sorun.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Bilerek hatali/eksik kaynak barindiran dizinler: bunlarin "hata sayisi"
# anlamli bir taban degil, yine de ARTIS denetlenebilir oldugu icin dahil.
DIRS = ["examples", "lib", "tests", "packages"]
SKIP_PARTS = ("tulpar_modules", "/en/inputs", "third_party", "wasm/emsdk")


def sources():
    for d in DIRS:
        base = os.path.join(ROOT, d)
        for root, _sub, files in os.walk(base):
            if any(p in root.replace("\\", "/") for p in SKIP_PARTS):
                continue
            for f in sorted(files):
                if f.endswith(".tpr"):
                    yield os.path.join(root, f)


def fmt_capture(binpath, path, env):
    """`tulpar fmt` ciktisini dondurur (dosyayi degistirmez); reddederse None."""
    r = subprocess.run([binpath, "fmt", path], capture_output=True, env=env, timeout=120)
    if r.returncode != 0:
        return None
    return r.stdout.decode(errors="replace")


def hata_sayisi(binpath, path, env):
    r = subprocess.run([binpath, "typecheck", path], capture_output=True, env=env, timeout=120)
    t = (r.stdout + r.stderr).decode(errors="replace")
    return t.count("parse error") + t.count("Lexer Error") + t.count("Parser Error")


def main():
    binpath = os.path.join(ROOT, "tulpar")
    if not os.path.exists(binpath):
        print("ATLANDI: ./tulpar yok (once ./build.sh) — bu denetim TAM kosmadi")
        return 0
    env = dict(os.environ, LC_ALL="C")
    tmp = tempfile.mkdtemp(prefix="fmtaudit")
    bozuk, degisken, n = [], [], 0
    for src in sources():
        n += 1
        raw = open(src, encoding="utf-8", errors="replace").read()
        a = os.path.join(tmp, "a.tpr")
        with open(a, "w", encoding="utf-8") as fh:
            fh.write(raw)
        once = hata_sayisi(binpath, a, env)
        # DIKKAT: `tulpar fmt <dosya>` dosyayi YERINDE DEGISTIRMEZ —
        # bicimlenmis metni STDOUT'a basar. Bu denetimin ilk surumu bunu
        # bilmeden `fmt` cagirip AYNI dosyayi yeniden okuyordu: dosya hic
        # degismedigi icin "hata sayisi artmadi" ve "idempotent" iddialarinin
        # IKISI DE kendiliginden dogruydu ve denetim HICBIR SEY OLCMUYORDU.
        # (Pozitif kontrol yakaladi: bicimlendirici bilerek bozuldugunda
        # denetim yine yesil veriyordu.) Cikti boruyla alinip yaziliyor.
        r1 = fmt_capture(binpath, a, env)
        if r1 is None:
            continue  # fmt kendisi reddetti: bu denetimin konusu degil
        bir = r1
        with open(a, "w", encoding="utf-8") as fh:
            fh.write(bir)
        sonra = hata_sayisi(binpath, a, env)
        if sonra > once:
            bozuk.append((os.path.relpath(src, ROOT), once, sonra))
        r2 = fmt_capture(binpath, a, env)
        if r2 is not None and r2 != bir:
            degisken.append(os.path.relpath(src, ROOT))
    if bozuk:
        print("HATA: %d dosyada bicimlendirme HATA SAYISINI ARTIRDI "
              "(fmt gecerli kaynagi bozuyor):" % len(bozuk))
        for p, a_, b_ in bozuk[:10]:
            print("    %s: %d -> %d hata" % (p, a_, b_))
    if degisken:
        print("HATA: %d dosya IDEMPOTENT DEGIL (ikinci fmt metni degistiriyor):" % len(degisken))
        for p in degisken[:10]:
            print("    %s" % p)
    if bozuk or degisken:
        print("fmt denetimi: %d dosya tarandi, SORUN VAR" % n)
        return 1
    print("fmt denetimi temiz: %d dosya — bicimlendirme hata sayisini artirmiyor ve idempotent" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
