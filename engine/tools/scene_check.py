#!/usr/bin/env python3
"""Sahne metin bicimi kapisi: yazici ile ayristirici AYNI dili konussun.

NEDEN: `.sahne` yazicisi (write_entity) ve ayristiricisi (scene_parse) elle
tutulan iki ayri tablodur. Biri degisip oteki degismediginde derleyici hicbir
sey demez -- sahne yazilir, geri okunamaz ya da ALANLAR KAYAR. Bu oturumda
olculdu:

  * `su` ve `ruzgar` satirlarinda yon Vec2 ama yazici vec(Vec3) cagiriyordu:
    hem derleme hatasi hem de ayristiriciyla uyusmayan jeton sayisi.
  * `partikul` bloku TEK satira yaziliyordu; ayristirici omur/boyut/hiz/
    dagilim'i AYRI satirlarda bekliyor ve satir kMaxTok=8 tavanini asiyordu
    (split() *bad=true -> sahne geri okunamaz).

Kapi iki sey olcer:
  1. Yazicinin urettigi her anahtar kelimenin ayristiricida bir dali var mi
     (ve tersi).
  2. Yazicinin o satirda kac JETON urettigi, ayristiricinin bekledigi sayiyla
     uyusuyor mu.

Ayrica satir basina jeton sayisinin kMaxTok tavanini asmadigini dogrular.

Kullanim:  python tools/scene_check.py [kok]
Donus:     0 = temiz, 1 = uyusmazlik (derlemeyi kir)
"""
import os
import re
import sys

SRC = "content/scene.cpp"

# Jeton sayimi DALLANMAYI bilmez: bir yazici satiri if/else ile iki ayri sekil
# yaziyorsa iki dalin jetonlari TOPLANIR ve sayi sisirilir. Asagidaki anahtarlar
# bu yuzden muaf; her birinin GERCEK en buyuk jeton sayisi elle yazili ve
# kMaxTok tavani yine de bu sayiyla olculur.
#
# Muafiyet EKLEMEK, sayiyi degistirmekten farkli bir sey: bir satir gercekten
# ayristiriciyla uyusmuyorsa buraya yazilmaz, DUZELTILIR.
BRANCHY = {
    # "govde kutu x y z dinamik" (6) | "govde kure r sabit" (4).
    # Sayici iki dali toplayip 8 diyor; gercegi 6.
    "govde": 6,
}

# Yazici cagrilarinin urettigi JETON sayisi.
EMIT = {
    "num": 1,
    "vec": 3,
    "vec2": 2,
    "str": 1,
}


def read(root):
    with open(os.path.join(root, SRC), encoding="utf-8") as f:
        return f.read()


# --------------------------------------------------------------------------
# Yazici tarafi: write_entity govdesi
# --------------------------------------------------------------------------
# Anahtar COK KELIMELI olabilir: o.puts("  model ilkel ") -> anahtar "model",
# ama satira IKI jeton koyar. Tek kelimeye zorlamak "pbr"yi ayri bir anahtar
# sanmaya yol aciyordu (aslinda model satirinin devami).
KW_START = re.compile(r'o\.puts\("(\s*[a-z0-9_-]+(?:\s+[a-z0-9_-]+)*\s*)"\)')
CALL = re.compile(r'o\.(num|vec2|vec|str)\(')
NEWLINE = re.compile(r"o\.ch\('\\n'\)")
# o.puts("kutu ") gibi govde icindeki sabit kelimeler de birer jetondur.
PUTS_WORD = re.compile(r'o\.puts\("\s*([a-z0-9_-]+)\s*"\)')
# o.puts(cond ? " a\n" : " b\n") -> 1 jeton + satir sonu
PUTS_TERNARY = re.compile(r'o\.puts\([^)]*\?\s*"([^"]*)"\s*:\s*"([^"]*)"\)')


def writer_lines(src):
    """[(anahtar, jeton_sayisi, kaynak_satir)] dondurur."""
    # Govdeyi PARANTEZ DENGESIYLE kes. "sonraki void'e kadar" demek dunya
    # yazicisina tasiyor ve oradaki "kamera" satirini varlik dalina sayiyordu.
    start = src.index("void write_entity(")
    depth, j = 0, start
    for j in range(src.index("{", start), len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                break
    body = src[start:j + 1]
    out = []
    cur_kw = None
    cur_tok = 0
    cur_line = 0
    for ln, line in enumerate(body.split("\n"), 1):
        code = line.split("//")[0]
        if not code.strip():
            continue
        pos = 0
        while pos < len(code):
            m_kw = KW_START.search(code, pos)
            m_call = CALL.search(code, pos)
            m_tern = PUTS_TERNARY.search(code, pos)
            m_nl = NEWLINE.search(code, pos)
            cands = [(m.start(), k, m) for k, m in
                     (("kw", m_kw), ("call", m_call), ("tern", m_tern), ("nl", m_nl)) if m]
            if not cands:
                break
            cands.sort()
            _, kind, m = cands[0]
            pos = m.end()
            if kind == "kw":
                words = m.group(1).split()
                if cur_kw is None:
                    cur_kw, cur_tok, cur_line = words[0], len(words), ln
                else:
                    cur_tok += len(words)  # govde icinde sabit kelime ("kutu", "omur"...)
            elif kind == "call":
                if cur_kw is not None:
                    cur_tok += EMIT[m.group(1)]
            elif kind == "tern":
                if cur_kw is not None:
                    cur_tok += 1
                    if "\\n" in m.group(1) or "\\n" in m.group(2):
                        out.append((cur_kw, cur_tok, cur_line))
                        cur_kw = None
            elif kind == "nl":
                if cur_kw is not None:
                    out.append((cur_kw, cur_tok, cur_line))
                    cur_kw = None
    return out


# --------------------------------------------------------------------------
# Ayristirici tarafi: tok_is(t[0], "kw") dallari
# --------------------------------------------------------------------------
BRANCH = re.compile(r'tok_is\(t\[0\],\s*"([a-z0-9_-]+)"\)')
NCHECK = re.compile(r"\bn\s*(!=|<|>|<=|>=)\s*(\d+)")


def parser_expect(src):
    """{anahtar: (op, sayi, satir)} dondurur."""
    out = {}
    lines = src.split("\n")
    for i, line in enumerate(lines):
        m = BRANCH.search(line)
        if not m:
            continue
        kw = m.group(1)
        # Jeton sayisi kontrolu ayni satirda ya da sonraki birkac satirda.
        # YALNIZ p.fail'e giden kontrol sayilir: "if (n >= 2 && tok_is(...))"
        # gibi satirlar bir dal secimidir, jeton SOZLESMESI degil -- onlari
        # sozlesme sanmak govde satirini yanlis yere kirmizi yapiyordu.
        for j in range(i, min(i + 4, len(lines))):
            code = lines[j].split("//")[0]
            if "p.fail" not in code:
                continue
            nm = NCHECK.search(code)
            if nm:
                out.setdefault(kw, (nm.group(1), int(nm.group(2)), j + 1))
                break
        else:
            out.setdefault(kw, (None, None, i + 1))
    return out


def max_tok(src):
    m = re.search(r"kMaxTok\s*=\s*(\d+)", src)
    return int(m.group(1)) if m else None


# --------------------------------------------------------------------------
# Bilesen canlilik: her kSceneXxx biti icin YAZ + OKU + ESITLIK var mi.
#
# Bu kapi olmadan bir bilesen "panelde var ama diske hic yazilmiyor" halinde
# kalabiliyor ve kimse fark etmiyor. Olculdu: NavAgent, Joint, Skybox,
# RefProbe, Reverb -- besinin de tam calisan paneli vardi, content/scene.cpp
# icinde SIFIR kez geciyorlardi. Kullanici degerleri giriyor, kaydediyor,
# aciyor; hepsi gitmis, hata da yok.
# --------------------------------------------------------------------------
BIT_RE = re.compile(r"(kScene[A-Za-z]+)\s*=\s*1u\s*<<\s*\d+")
# Bayraklar (kSceneHidden/kSceneLocked) bilesen DEGIL: ayri enum, serilestirmesi
# "bayrak" satiriyla toplu yapilir.
NOT_COMPONENT = {"kSceneHidden", "kSceneLocked"}


def component_liveness(root, src):
    with open(os.path.join(root, "content/scene.hpp"), encoding="utf-8") as f:
        hdr = f.read()
    bits = [b for b in BIT_RE.findall(hdr) if b not in NOT_COMPONENT]

    write_body = src[src.index("void write_entity("):src.index("bool scene_entity_equal")]
    eq_body = src[src.index("bool scene_entity_equal"):]
    eq_body = eq_body[:eq_body.index("\n}")]
    parse_body = src[src.index("scene_parse"):]

    out = []
    for b in bits:
        has_write = b in write_body
        has_eq = b in eq_body
        has_parse = ("seen_comp |= " + b) in parse_body
        if not (has_write and has_eq and has_parse):
            missing = []
            if not has_write:
                missing.append("YAZ")
            if not has_eq:
                missing.append("ESITLIK")
            if not has_parse:
                missing.append("OKU")
            out.append("%s: %s yok -> bu bilesen .sahne'ye tam gitmiyor" % (b, "/".join(missing)))
    return bits, out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    src = read(root)
    cap = max_tok(src)
    w = writer_lines(src)
    # Dallanan satirlarin sayisini elle bilinen GERCEK en buyukle degistir.
    w = [(kw, BRANCHY.get(kw, ntok), ln) for kw, ntok, ln in w]
    p = parser_expect(src)

    problems = []

    # 1) Yazicinin ürettigi anahtarin ayristiricida dali var mi
    for kw, ntok, ln in w:
        if kw not in p:
            problems.append("yazici '%s' uretiyor, ayristiricida DAL YOK (scene.cpp:%d)" % (kw, ln))

    # 2) Jeton sayisi uyusuyor mu
    for kw, ntok, ln in w:
        if kw not in p:
            continue
        op, want, pln = p[kw]
        if want is None:
            continue
        ok = {"!=": ntok == want, "<": ntok >= want, "<=": ntok > want,
              ">": ntok <= want, ">=": ntok <= want}.get(op, True)
        if not ok:
            problems.append(
                "'%s': yazici %d jeton uretiyor, ayristirici 'n %s %d' istiyor "
                "(yazici scene.cpp:%d, ayristirici scene.cpp:%d)" % (kw, ntok, op, want, ln, pln))

    # 3) kMaxTok tavani
    if cap:
        for kw, ntok, ln in w:
            if ntok > cap:
                problems.append(
                    "'%s': %d jeton, kMaxTok=%d tavanini ASIYOR -> split() *bad=true, "
                    "sahne GERI OKUNAMAZ (scene.cpp:%d)" % (kw, ntok, cap, ln))

    # 4) Bilesen canliligi
    bits, dead = component_liveness(root, src)
    problems.extend(dead)

    print("sahne bicim kapisi: %d yazici satiri, %d ayristirici dali, %d bilesen biti, kMaxTok=%s"
          % (len(w), len(p), len(bits), cap))
    if not problems:
        print("sahne bicim kapisi: 0 uyusmazlik")
        return 0
    print("sahne bicim kapisi: %d UYUSMAZLIK" % len(problems))
    for x in problems:
        print("  " + x)
    return 1


if __name__ == "__main__":
    sys.exit(main())
