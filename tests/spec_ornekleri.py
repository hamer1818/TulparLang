#!/usr/bin/env python3
"""DIL TANIMI ORNEKLERI KAPISI (2026-11-26) — docs/SPEC.md calistirilabilir.

SPEC.md'deki her ```tpr blogu derlenir:
  * `// cikti:` satirlari varsa: ardindaki `// <satir>`lar beklenen stdout
    (bayt bayt; bos satir `//`), program 0 ile cikmali.
  * `// hata: <alt dizgi>` varsa: derleme BASARISIZ olmali ve tani o alt
    dizgiyi icermeli.
  * Ikisi de yoksa blok yalniz DERLENMELI.
Belge ile derleyici ayrisirsa kirmizi — belge eskiyemez.

KONTROL: bir ornegin beklenen ciktisi bozulmus kopyasiyla kapi kirmizi
olmali (karsilastirici gercekten karsilastiriyor mu) — her kosuda yapilir.
"""
import os
import re
import subprocess
import sys
import tempfile

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(KOK, "docs", "SPEC.md")
TUL = os.path.join(KOK, "tulpar")


def bloklar(metin):
    out = []
    for m in re.finditer(r"```tpr\n(.*?)```", metin, re.S):
        satir = metin[:m.start()].count("\n") + 1
        out.append((satir, m.group(1)))
    return out


def ayrik(kod):
    """(program, beklenen_cikti|None, beklenen_hata|None)"""
    lines = kod.split("\n")
    cikti, hata = None, None
    prog = []
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.strip() == "// cikti:":
            cikti = []
            for k in lines[i + 1:]:
                if k.startswith("// "):
                    cikti.append(k[3:])
                elif k.strip() == "//":
                    cikti.append("")
                elif k.strip() == "":
                    continue
                else:
                    break
            break
        mh = re.match(r"\s*// hata: (.*)$", l)
        if mh:
            hata = mh.group(1).strip()
            i += 1
            continue
        prog.append(l)
        i += 1
    return "\n".join(prog) + "\n", cikti, hata


def kos(prog, cikti, hata, d, ad):
    yol = os.path.join(d, ad + ".tpr")
    open(yol, "w", encoding="utf-8").write(prog)
    env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="", LC_ALL="C")
    b = subprocess.run([TUL, "build", yol, os.path.join(d, ad)], cwd=d, capture_output=True,
                       text=True, env=env, timeout=300)
    if hata is not None:
        if b.returncode == 0:
            return "derleme BASARILI, hata bekleniyordu: " + hata
        tani = b.stdout + b.stderr
        env_tr = dict(env, LC_ALL="tr_TR.UTF-8", LANG="tr_TR.UTF-8")
        b2 = subprocess.run([TUL, "build", yol, os.path.join(d, ad)], cwd=d, capture_output=True,
                            text=True, env=env_tr, timeout=300)
        tani += b2.stdout + b2.stderr
        if hata not in tani:
            return "beklenen tani yok: '%s'\n%s" % (hata, tani[-600:])
        return None
    if b.returncode != 0:
        return "derlenmedi:\n" + (b.stdout + b.stderr)[-800:]
    if cikti is None:
        return None
    r = subprocess.run([os.path.join(d, ad)], cwd=d, capture_output=True, text=True, env=env,
                       timeout=60)
    gercek = r.stdout.rstrip("\n").split("\n") if r.stdout else []
    if r.returncode != 0:
        return "cikis kodu %d\n%s" % (r.returncode, r.stderr[-400:])
    if gercek != cikti:
        return "cikti farkli:\n  beklenen %r\n  gercek   %r" % (cikti, gercek)
    return None


def main():
    metin = open(SPEC, encoding="utf-8").read()
    bl = bloklar(metin)
    hatalar = []
    ornek_cikti = None
    with tempfile.TemporaryDirectory(prefix="spec_") as d:
        for n, (satir, kod) in enumerate(bl):
            prog, cikti, hata = ayrik(kod)
            if cikti is not None and ornek_cikti is None:
                ornek_cikti = (prog, cikti)
            e = kos(prog, cikti, hata, d, "o%d" % n)
            if e:
                hatalar.append("SPEC.md:%d: %s" % (satir, e))
        # KONTROL: bozulmus beklenen cikti kirmizi olmali.
        if ornek_cikti:
            prog, cikti = ornek_cikti
            if kos(prog, cikti + ["FAZLA SATIR"], None, d, "kontrol") is None:
                hatalar.append("KONTROL: bozulmus beklenen cikti GECTI — karsilastirici olcmuyor")
    for h in hatalar:
        print("FAIL:", h)
    if hatalar:
        print("FAIL: spec_ornekleri — %d/%d ornek" % (len(hatalar), len(bl)))
        return 1
    print("PASS: spec_ornekleri — docs/SPEC.md'deki %d ornegin hepsi derleyiciyle ayni "
          "(kontrol: bozuk beklenti kirmizi)" % len(bl))
    return 0


if __name__ == "__main__":
    sys.exit(main())
