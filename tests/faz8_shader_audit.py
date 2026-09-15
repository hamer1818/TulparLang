#!/usr/bin/env python3
"""faz8_shader_audit.py — Faz 8 shader alt kumesi prototipinin kapisi.

Ne olcer
--------
1. GERCEK VARLIKLAR: engine/tests/assets/shader/*.tprs cevrilir, glslc ile
   SPIR-V'ye derlenir ve depodaki engine/rhi/shaders/*_spv.h ile BAYT
   karsilastirilir. Tek bir fark bile kirmizi.
2. POZITIF KONTROL: bilerek bozulmus girdiler verilir. Her biri KIRMIZI
   olmali; biri bile gecerse kapi hicbir sey olcmuyor demektir ve denetim
   yine kirmizi olur. Dort kontrol var, uc ayri katmani ayri ayri yokluyor:
     - sozdizimi hatasi          -> ceviricinin ayristiricisi yakalamali
     - bilinmeyen tip / fonksiyon -> ceviricinin anlamsal denetimi yakalamali
     - tip uyusmazligi            -> cevirici YAKALAMAZ, glslc yakalamali
       (prototip tip denetimi yapmiyor; bu kontrol o siniri gorunur tutar)
     - degistirilmis sabit        -> bayt karsilastirmasi yakalamali
       (yoksa "bayt ayni" satiri bos bir iddia olurdu)

glslc yoksa SPIR-V'ye bagli adimlar ACIKCA "ATLANDI" yazar ve sayilir;
sessizce gecmez.

Kosum:  python3 tests/faz8_shader_audit.py
build.sh'e BAGLI DEGILDIR; elle calistirilir.
"""

import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TOOL = os.path.join(REPO, "engine", "tools", "tpr_shader.py")
ASSETS = os.path.join(REPO, "engine", "tests", "assets", "shader")


def load_tool():
    spec = importlib.util.spec_from_file_location("tpr_shader", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Audit:
    def __init__(self):
        self.fail = 0
        self.skip = 0
        self.ok = 0

    def yesil(self, msg):
        self.ok += 1
        print("  YESIL   %s" % msg)

    def kirmizi(self, msg):
        self.fail += 1
        print("  KIRMIZI %s" % msg)

    def atlandi(self, msg):
        self.skip += 1
        print("  ATLANDI %s" % msg)


BOZUK = [
    ("sozdizimi", "cevirici-parse", '''str asama = "frag";
vec4 o_color = cikti(0);
func main() { o_color = vec4(1.0, 0.0 0.0, 1.0); }
'''),
    ("bilinmeyen tip", "cevirici-anlam", '''str asama = "frag";
vec4 o_color = cikti(0);
func main() { qvec9 x = qvec9(1.0); o_color = vec4(1.0); }
'''),
    ("bilinmeyen fonksiyon", "cevirici-anlam", '''str asama = "frag";
vec4 o_color = cikti(0);
func main() { o_color = vec4(kokusuz_fonksiyon(1.0)); }
'''),
    ("tip uyusmazligi", "glslc", '''str asama = "frag";
vec2 v_uv = girdi(0);
sampler2D u_src = ornek(0, 0);
vec4 o_color = cikti(0);
func main() { vec3 c = texture(u_src, v_uv); o_color = vec4(c, 1.0); }
'''),
]


def main():
    a = Audit()
    if not os.path.exists(TOOL):
        print("KIRMIZI: %s yok" % TOOL)
        return 1
    tool = load_tool()
    have_glslc = shutil.which("glslc") is not None
    tmp = tempfile.mkdtemp(prefix="faz8audit")

    def derle(glsl, stage):
        """(bayt, hata) — glslc yoksa (None, 'glslc yok')."""
        return tool.compile_spv(glsl, stage, None, tmp)

    # ---------------------------------------------------------------- 1
    print("\n[1] Gercek varliklar: .tprs -> GLSL -> SPIR-V -> depodaki *_spv.h")
    files = sorted(f for f in os.listdir(ASSETS) if f.endswith(".tprs")) \
        if os.path.isdir(ASSETS) else []
    if not files:
        a.kirmizi("%s altinda .tprs yok — kapi olcecek sey bulamadi" % ASSETS)
    ayni = 0
    for f in files:
        path = os.path.join(ASSETS, f)
        base = f[:-5]
        try:
            glsl = tool.translate(path)
        except tool.ShaderError as e:
            a.kirmizi("%-24s cevrilemedi: %s" % (base, e))
            continue
        if not have_glslc:
            a.atlandi("%-24s cevrildi, glslc yok — SPIR-V dogrulanmadi" % base)
            continue
        data, err = derle(glsl, tool.tprs_stage(path))
        if data is None:
            first = (err or "").strip().splitlines()
            a.kirmizi("%-24s glslc: %s" % (base, first[0] if first else "?"))
            continue
        ref = tool.header_bytes(base)
        pin = tool.pinned_digest(open(path).read())
        now = tool.source_digest(base)
        if ref is None:
            a.yesil("%-24s %5d bayt (depoda karsiligi yok)" % (base, len(data)))
        elif pin and now and pin != now:
            # Referans shader BASKA biri tarafindan degistirilmis. Sessizce
            # kirmizi (yanlis suclama) ya da sessizce yesil (olcum yok)
            # olmasin: GORUNUR atlama.
            a.atlandi("%-24s kaynak GLSL degismis (%s -> %s); .tprs yeniden "
                      "ported edilmeli" % (base, pin, now))
        elif ref == data:
            a.yesil("%-24s %5d bayt, *_spv.h ile BAYT AYNI" % (base, len(data)))
            ayni += 1
        else:
            a.kirmizi("%-24s SPIR-V farkli (%d vs referans %d bayt)"
                      % (base, len(data), len(ref)))
    if files and have_glslc:
        print("  [bilgi] %d/%d dosya depodaki SPIR-V ile bayt ayni "
              "(%d atlandi: kaynak degismis)"
              % (ayni, len(files), a.skip))

    # ---------------------------------------------------------------- 2
    print("\n[2] Pozitif kontrol: bozuk girdi KIRMIZI yapmali")
    for ad, katman, src in BOZUK:
        p = os.path.join(tmp, "bozuk.tprs")
        with open(p, "w") as fh:
            fh.write(src)
        yakalandi, nerede, detay = False, "", ""
        try:
            glsl = tool.translate(p)
        except tool.ShaderError as e:
            yakalandi, nerede, detay = True, "cevirici", str(e)
        else:
            if not have_glslc:
                if katman == "glslc":
                    a.atlandi("%-22s glslc yok — bu kontrol kosmadi" % ad)
                    continue
            else:
                data, err = derle(glsl, tool.tprs_stage(p))
                if data is None:
                    lines = (err or "").strip().splitlines()
                    yakalandi, nerede = True, "glslc"
                    detay = lines[0] if lines else "?"
        if yakalandi:
            a.yesil("%-22s %s yakaladi: %s"
                    % (ad, nerede, detay[:58]))
        else:
            a.kirmizi("%-22s BOZUK GIRDI GECTI — kapi bu katmani (%s) "
                      "olcmuyor" % (ad, katman))

    # bayt karsilastirmasinin kendisi canli mi?
    if have_glslc and files:
        ornek = os.path.join(ASSETS, "bloom_down.frag.tprs")
        if os.path.exists(ornek):
            src = open(ornek).read()
            assert "c * 0.25" in src, "kontrolun dayandigi sabit degismis"
            p = os.path.join(tmp, "bloom_down.frag.tprs")
            with open(p, "w") as fh:
                fh.write(src.replace("c * 0.25", "c * 0.26"))
            data, err = derle(tool.translate(p), "frag")
            ref = tool.header_bytes("bloom_down.frag")
            if data is None:
                a.kirmizi("degistirilmis sabit   glslc derleyemedi: %s" % err)
            elif data == ref:
                a.kirmizi("degistirilmis sabit   SPIR-V hala AYNI — bayt "
                          "karsilastirmasi hicbir sey olcmuyor")
            else:
                a.yesil("degistirilmis sabit    bayt karsilastirmasi farki "
                        "gordu (%d vs %d)" % (len(data), len(ref)))
    else:
        a.atlandi("degistirilmis sabit    glslc yok — bayt kontrolu kosmadi")

    shutil.rmtree(tmp, ignore_errors=True)
    print("\nFaz 8 shader denetimi: %d yesil, %d kirmizi, %d atlandi"
          % (a.ok, a.fail, a.skip))
    if a.skip and not a.fail:
        neden = "glslc kurulu degil" if not have_glslc \
            else "referans GLSL degismis (--pin ile yeniden ported edilmeli)"
        print("UYARI: %d adim ATLANDI (%s) — bu denetim TAM kosmadi."
              % (a.skip, neden))
    return 1 if a.fail else 0


if __name__ == "__main__":
    sys.exit(main())
