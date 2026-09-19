#!/usr/bin/env python3
"""Editor ikon kapisi: cizilemeyecek ikon kalmasin.

NEDEN: editorun ikonlari kaynak icinde ya ham UTF-8 ya da \\xNN kacis
dizileriyle yaziliyor. Yuklenen font o kod noktasini ICERMIYORSA ImGui
eksik-glif kutusu cizer ve HATA VERMEZ -- arayuz sessizce bozulur. Bu
oturumda olculdu: 16 kod noktasi, 34 cagri noktasi bos kutu ciziyordu
(panel baslikleri dahil: Kamera, Ses, Betik, Istatistik, Kaydet, Dunya).

Kapi, editorun GERCEKTEN yukledigi fontlarin cmap'ini okur ve kaynaktaki
her ikon kod noktasinin bir fontta bulundugunu dogrular.

Kapsam kararlari:
  * Yalniz DIZE SABITLERININ ICI taranir. Yorumlardaki tire/uc-nokta metindir,
    ikon degildir; onlari saymak kapiyi gurultuye bogardi.
  * U+2000 altindaki kod noktalari atlanir (ASCII + Latin-1 ek + Turkce
    harfler); bunlar metin fontunun asli isi ve zaten kapsaniyor.
  * ICON_MD_* makrolari IconsMaterialDesign.h'ten cozulur: kaynakta gorunen
    makro adi, baslikta tanimli UTF-8 diziye cevrilip ayni denetimden gecer.
    Boylece "makro var ama fontta glif yok" durumu da yakalanir.

Kullanim:  python tools/icon_check.py [kok]
Donus:     0 = temiz, 1 = eksik glif var (derlemeyi kir)
"""
import os
import re
import struct
import sys

BS = chr(92)

# Editorun yukledigi fontlar (app/editor_app.cpp'deki yollarla AYNI olmali).
FONTS = [
    "assets/fonts/DejaVuSans.ttf",
    "assets/fonts/MaterialIcons-Regular.ttf",
]
# Ikon makrolarinin tanimlandigi baslik.
ICON_HEADER = "third_party/iconfont/IconsMaterialDesign.h"
# Taranacak kaynaklar.
SCAN_DIRS = ["app"]
SCAN_EXT = (".cpp", ".hpp")

# Bu kod noktasinin altinda kalanlar metindir, ikon degil.
MIN_ICON_CP = 0x2000


# --------------------------------------------------------------------------
# TrueType cmap okuyucu (format 4 ve 12). Harici bagimlilik YOK: kapi
# fontTools kurulu olmayan bir makinede de kosmali.
# --------------------------------------------------------------------------
def font_ranges(path):
    with open(path, "rb") as f:
        d = f.read()
    if len(d) < 12:
        raise ValueError("font cok kisa")
    num_tables = struct.unpack(">H", d[4:6])[0]
    cmap_off = None
    for i in range(num_tables):
        o = 12 + 16 * i
        if d[o:o + 4] == b"cmap":
            cmap_off = struct.unpack(">I", d[o + 8:o + 12])[0]
    if cmap_off is None:
        raise ValueError("cmap tablosu yok")

    n_sub = struct.unpack(">H", d[cmap_off + 2:cmap_off + 4])[0]
    best = None  # format 12 varsa onu tercih et (SMP kapsar)
    for i in range(n_sub):
        o = cmap_off + 4 + 8 * i
        sub_off = struct.unpack(">I", d[o + 4:o + 8])[0]
        base = cmap_off + sub_off
        fmt = struct.unpack(">H", d[base:base + 2])[0]
        if fmt == 12:
            best = (12, base)
        elif fmt == 4 and (best is None or best[0] != 12):
            best = (4, base)
    if best is None:
        raise ValueError("desteklenen cmap alt tablosu yok (4/12 bekleniyor)")

    fmt, base = best
    out = []
    if fmt == 12:
        n_groups = struct.unpack(">I", d[base + 12:base + 16])[0]
        for g in range(n_groups):
            o = base + 16 + 12 * g
            s, e, _ = struct.unpack(">III", d[o:o + 12])
            out.append((s, e))
    else:
        seg_x2 = struct.unpack(">H", d[base + 6:base + 8])[0]
        seg = seg_x2 // 2
        for i in range(seg):
            end = struct.unpack(">H", d[base + 14 + 2 * i:base + 16 + 2 * i])[0]
            start = struct.unpack(">H", d[base + 16 + seg_x2 + 2 * i:base + 18 + seg_x2 + 2 * i])[0]
            if start <= end:
                out.append((start, end))
    return out


def covered(ranges, cp):
    return any(s <= cp <= e for s, e in ranges)


# --------------------------------------------------------------------------
# ICON_MD_* makrolari -> kod noktasi
# --------------------------------------------------------------------------
DEFINE_RE = re.compile(r'^#define\s+(ICON_MD_[A-Z0-9_]+)\s+"([^"]*)"')


def load_icon_macros(root):
    path = os.path.join(root, ICON_HEADER)
    table = {}
    if not os.path.exists(path):
        return table
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = DEFINE_RE.match(line)
            if not m:
                continue
            name, body = m.group(1), m.group(2)
            # Govde ya ham UTF-8 ya da \xNN dizisidir.
            raw = decode_escapes(body)
            cps = [ord(c) for c in raw if ord(c) >= MIN_ICON_CP]
            if cps:
                table[name] = cps[0]
    return table


HEX_ESC = re.compile(BS + BS + "x([0-9A-Fa-f]{2})")


def decode_escapes(text):
    """\\xNN dizilerini UTF-8 cozer; kalan karakterleri oldugu gibi birakir."""
    out = []
    i = 0
    while i < len(text):
        m = HEX_ESC.match(text, i)
        if m:
            run = bytearray()
            while m:
                run.append(int(m.group(1), 16))
                i = m.end()
                m = HEX_ESC.match(text, i)
            out.append(run.decode("utf-8", "replace"))
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


# --------------------------------------------------------------------------
# Kaynak tarayici: YALNIZ dize sabitlerinin ici.
# --------------------------------------------------------------------------
def string_literals(line):
    """Satirdaki cift tirnakli dizelerin govdelerini dondurur.

    Kaba ama bu kod tabani icin yeterli: tek tirnakli karakter sabitlerinde
    ikon yok, ham dize (R"(...)") kullanilmiyor. Yorum ICINDE kalan tirnaklar
    elenir ki aciklama metinleri kapiyi tetiklemesin.
    """
    # // yorumunu at (dize icindeki // durumunu ayirt etmek icin tirnak say)
    in_str = False
    esc = False
    cut = len(line)
    for i, ch in enumerate(line):
        if esc:
            esc = False
            continue
        if ch == BS:
            esc = True
        elif ch == '"':
            in_str = not in_str
        elif ch == "/" and not in_str and i + 1 < len(line) and line[i + 1] == "/":
            cut = i
            break
    line = line[:cut]

    out = []
    buf = None
    esc = False
    for ch in line:
        if buf is None:
            if ch == '"':
                buf = []
            continue
        if esc:
            buf.append(ch)
            esc = False
            continue
        if ch == BS:
            buf.append(ch)
            esc = True
        elif ch == '"':
            out.append("".join(buf))
            buf = None
        else:
            buf.append(ch)
    return out


MACRO_RE = re.compile(r"\b(ICON_MD_[A-Z0-9_]+)\b")


def scan_sources(root, macros):
    """[(kod_noktasi, dosya, satir, kaynak_turu)] dondurur."""
    found = []
    for d in SCAN_DIRS:
        base = os.path.join(root, d)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            if not name.endswith(SCAN_EXT):
                continue
            path = os.path.join(base, name)
            rel = os.path.join(d, name).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as f:
                for ln, line in enumerate(f, 1):
                    for lit in string_literals(line):
                        for ch in decode_escapes(lit):
                            if ord(ch) >= MIN_ICON_CP:
                                found.append((ord(ch), rel, ln, "dize"))
                    # Makro kullanimlari (yorum disi kismi zaten kirpildi)
                    code = line.split("//", 1)[0] if '"' not in line.split("//", 1)[0] else line
                    for m in MACRO_RE.finditer(code):
                        cp = macros.get(m.group(1))
                        if cp is None:
                            found.append((-1, rel, ln, m.group(1)))
                        else:
                            found.append((cp, rel, ln, m.group(1)))
    return found


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."

    ranges = []
    loaded = []
    for rel in FONTS:
        path = os.path.join(root, rel)
        if not os.path.exists(path):
            print("ikon kapisi: FONT YOK -> %s" % rel)
            return 1
        try:
            r = font_ranges(path)
        except Exception as exc:  # bozuk font da kapiyi kirmali
            print("ikon kapisi: %s okunamadi: %s" % (rel, exc))
            return 1
        ranges.extend(r)
        loaded.append("%s (%d aralik)" % (os.path.basename(rel), len(r)))

    macros = load_icon_macros(root)
    if not macros:
        print("ikon kapisi: %s bulunamadi ya da bos" % ICON_HEADER)
        return 1

    found = scan_sources(root, macros)
    bad = {}
    for cp, rel, ln, kind in found:
        if cp < 0:
            bad.setdefault(kind, []).append("%s:%d (baslikta TANIMSIZ makro)" % (rel, ln))
        elif not covered(ranges, cp):
            bad.setdefault("U+%05X" % cp, []).append("%s:%d [%s]" % (rel, ln, kind))

    uniq = len({cp for cp, _, _, _ in found if cp >= 0})
    print("ikon kapisi: %s | %d ikon kullanimi, %d ayri kod noktasi, %d makro tanimli"
          % (", ".join(loaded), len(found), uniq, len(macros)))
    if not bad:
        print("ikon kapisi: 0 eksik glif")
        return 0

    n = sum(len(v) for v in bad.values())
    print("ikon kapisi: %d EKSIK GLIF (%d cagri noktasi)" % (len(bad), n))
    for key in sorted(bad):
        locs = bad[key]
        print("  %-12s %2d yer" % (key, len(locs)))
        for loc in locs[:4]:
            print("      %s" % loc)
        if len(locs) > 4:
            print("      ... +%d" % (len(locs) - 4))
    print()
    print("Cozum: ikonu fontta VAR OLAN bir glifle degistir (ICON_MD_* makrolari,")
    print("third_party/iconfont/IconsMaterialDesign.h) ya da gerekli fontu FONTS'a ekle.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
