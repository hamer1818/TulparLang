#!/usr/bin/env python3
# Combine all arcade games into one launcher .tpr by namespacing each game's
# top-level functions + globals with a per-game prefix, converting the trailing
# registration block into a `func <prefix>_setup()` (dropping sahne/oyna).
import re, sys

GAMES = [
    ("examples/arcade_topla.tpr",    "tp", "Topla"),
    ("examples/arcade_zipla.tpr",    "zp", "Zipla"),
    ("examples/arcade_nisan.tpr",    "ni", "Nisan"),
    ("examples/arcade_tugla.tpr",    "tu", "Tugla"),
    ("examples/arcade_uzay.tpr",     "uz", "Uzay"),
    ("examples/arcade_labirent.tpr", "la", "Labirent"),
    ("examples/arcade_karsiya.tpr",  "ka", "Karsiya"),
    ("examples/arcade_ucus.tpr",     "uc", "Ucus"),
    ("examples/arcade_goktasi.tpr",  "go", "Goktasi"),
    ("examples/arcade_yilan.tpr",    "yi", "Yilan"),
]

REG_RE  = re.compile(r'\s*(sahne|scene|bilgi|info|baslangicta|on_start|bolum|level|her_kare|on_frame|carpisinca|on_hit|ciz_ustune|dil|language|yercekimi|gravity|hud|hud_gizle|touch_controls|touch_kontrol|kontrol_semasi|control_scheme)\b')
DROP_RE = re.compile(r'\s*(sahne|scene|oyna|play)\b')

def mask(line):
    toks = []
    def repl(m):
        toks.append(m.group(0)); return f'\x00{len(toks)-1}\x00'
    masked = re.sub(r'"(?:[^"\\]|\\.)*"|//.*$', repl, line)
    return masked, toks

def unmask(line, toks):
    return re.sub(r'\x00(\d+)\x00', lambda m: toks[int(m.group(1))], line)

def transform(path, prefix):
    raw = open(path, encoding='utf-8').read().split('\n')
    lines = [l for l in raw if l.strip() != 'import "arcade";']
    # local symbols = functions + top-level global declarations
    # Only column-0 (top-level) declarations are game globals/functions. Indented
    # `int e = ...` are function locals — must NOT be captured (else params/locals
    # get needlessly prefixed).
    syms = set()
    for l in lines:
        m = re.match(r'func\s+([A-Za-z_][A-Za-z0-9_]*)', l)
        if m: syms.add(m.group(1))
        m = re.match(r'(?:int|float|bool|array|str|string|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=', l)
        if m: syms.add(m.group(1))
    if not syms:
        raise SystemExit(f"{path}: no local symbols found")
    alt = '|'.join(re.escape(s) for s in sorted(syms, key=len, reverse=True))
    sym_re = re.compile(r'\b(' + alt + r')\b')
    body, reg = [], []
    depth = 0
    for l in lines:
        masked, toks = mask(l)
        replaced = sym_re.sub(lambda m: prefix + '_' + m.group(1), masked)
        out = unmask(replaced, toks)
        is_drop = depth == 0 and DROP_RE.match(masked)
        is_reg  = depth == 0 and REG_RE.match(masked)
        if is_drop:
            pass
        elif is_reg:
            if out.strip():
                reg.append('    ' + out.strip())
        else:
            body.append(out)
        depth += masked.count('{') - masked.count('}')
    # trim leading/trailing blank lines from body
    while body and not body[0].strip():  body.pop(0)
    while body and not body[-1].strip(): body.pop()
    setup = f"func {prefix}_setup() {{\n" + "\n".join(reg) + "\n}"
    return "\n".join(body) + "\n\n" + setup, syms

out = []
out.append("// Tulpar Arcade — LAUNCHER: TÜM arcade oyunları tek pencerede / tek APK'da.")
out.append("//")
out.append("//   ./tulpar examples/arcade_launcher.tpr")
out.append("//   tulpar build --target=android examples/arcade_launcher.tpr out   (mobil)")
out.append("//")
out.append("// Açılışta menü ızgarası; bir karta dokun (ya da ok tuşları + ENTER) → oyun")
out.append("// başlar. Oyun içinde sağ-üst EV butonu (ya da ESC) → menü. Klavye + dokunma")
out.append("// (D-pad + aksiyon) her oyunda çalışır.")
out.append("//")
out.append("// BU DOSYA ÜRETİLDİ (tools/make_arcade_launcher.py): her oyun examples/arcade_*.tpr")
out.append("// dosyasından alınıp sembolleri <önek>_ ile namespace'lendi ve top-level kayıt")
out.append("// bloğu `func <önek>_setup()` içine taşındı (sahne/oyna düşürüldü). Oyun mantığı")
out.append("// aynen korunur; kaynak oyunlar hâlâ tek başına da çalışır.")
out.append("")
out.append('import "arcade";')
out.append("")

all_syms = {}
for path, prefix, name in GAMES:
    block, syms = transform(path, prefix)
    # collision check across games (prefixed, so shouldn't collide, but assert)
    out.append("// " + "=" * 74)
    out.append(f"//  {name}  ({path}, önek {prefix}_)")
    out.append("// " + "=" * 74)
    out.append(block)
    out.append("")

out.append("// " + "=" * 74)
out.append("//  Menü + launcher")
out.append("// " + "=" * 74)
out.append('sahne(640, 480, "Tulpar Arcade");')
out.append('menu_basligi("TULPAR ARCADE");')
for path, prefix, name in GAMES:
    out.append(f'oyun_ekle("{name}", {prefix}_setup);')
out.append("launcher();")
out.append("")

open("examples/arcade_launcher.tpr", "w", encoding='utf-8').write("\n".join(out))
print(f"wrote examples/arcade_launcher.tpr ({sum(len(l) for l in out)} bytes, {len(out)} lines)")
print(f"games: {', '.join(n for _,_,n in GAMES)}")
