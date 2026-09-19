#!/usr/bin/env python3
"""GLSL -> SPIR-V -> C dizisi (engine/rhi/shaders/*_spv.h).

Faz 1: shader'lar GLSL (glslc). Plan Faz 8'e kadar Slang diyordu; bu makinede
ve CI'da slangc yok (Arch'taki `slang` paketi S-Lang kutuphanesi, shader
Slang degil). Uretilen .h dosyalari DEPOYA GIRER: CI'da glslc gerekmez ve
byte'lar deterministiktir. Yeniden uretmek: python3 engine/tools/compile_shaders.py
"""
import os
import struct
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHADERS = os.path.join(os.path.dirname(HERE), "rhi", "shaders")


# Iki derleyici de kabul edilir. glslc (shaderc) TERCIH EDILIR cunku
# depodaki mevcut basliklar onunla uretildi; glslang ayni kaynagi GECERLI
# ama BAYT OLARAK FARKLI SPIR-V'ye cevirir (farkli optimizasyon gecisleri).
# Ikisi de yoksa betik, once oldugu gibi, calismaz -- ama artik SEBEBI
# soyluyor ve glslang'in da aranmis oldugunu belirtiyor.

# --- glslang: hata ayiklama ISIMLERINI siyir --------------------------------
# Depodaki *_spv.h dosyalari glslc -O ile uretildi ve glslc OpName/OpMemberName
# komutlarini ATAR. tools/layout_check.py bu varsayim uzerine kurulu: isimler
# DURUYORSA blok yollarini GLSL kaynagiyla eslestiremiyor, "yolu
# adlandirilamadi" deyip shader'i denetim DISINDA birakiyor -- yani yerlesim
# kapisi sessizce kapsam kaybediyor (olculdu: 39 blok/16 shader -> 34/15).
# glslang isimleri KORUDUGU icin burada elle siyriliyor.
#
# OpName(5) ve OpMemberName(6) yalnizca hata ayiklama bilgisidir; kaldirmak
# modulun ANLAMINI degistirmez (SPIR-V spec, Debug Instructions).
_OP_NAME, _OP_MEMBER_NAME = 5, 6


def strip_debug_names(data):
    if len(data) % 4 or data[:4] != b"\x03\x02\x23\x07":
        return data  # SPIR-V degil: dokunma
    w = list(struct.unpack("<%dI" % (len(data) // 4), data))
    out = w[:5]  # baslik: magic, surum, uretec, id siniri, sema
    i = 5
    while i < len(w):
        count = w[i] >> 16
        if count == 0:
            return data  # bozuk akis: dokunma
        if (w[i] & 0xFFFF) not in (_OP_NAME, _OP_MEMBER_NAME):
            out.extend(w[i:i + count])
        i += count
    return struct.pack("<%dI" % len(out), *out)


def find_compiler():
    glslc = shutil.which("glslc")
    if glslc:
        return ("glslc", glslc)
    glslang = shutil.which("glslang") or shutil.which("glslangValidator")
    if glslang:
        return ("glslang", glslang)
    return (None, None)


STAGE = {".vert": "vert", ".frag": "frag", ".comp": "comp"}


def compile_one(kind, exe, src, ext):
    if kind == "glslc":
        return subprocess.run([exe, "-O", "--target-env=vulkan1.1", "-o", "-", src],
                              capture_output=True)
    # glslang stdout'a SPIR-V yazmaz; gecici dosya uzerinden gider.
    tmp = src + ".tmp.spv"
    r = subprocess.run([exe, "-V", "-O", "--target-env", "vulkan1.1",
                        "-S", STAGE[ext], "-o", tmp, src], capture_output=True)
    if r.returncode == 0:
        with open(tmp, "rb") as f:
            r.stdout = strip_debug_names(f.read())
        os.remove(tmp)
    return r


def main():
    kind, exe = find_compiler()
    if not exe:
        print("shader derleyicisi yok: glslc (shaderc) ya da glslang gerekli", file=sys.stderr)
        return 2
    print("derleyici: %s (%s)" % (kind, exe))
    for name in sorted(os.listdir(SHADERS)):
        if not name.endswith((".vert", ".frag", ".comp")):
            continue
        src = os.path.join(SHADERS, name)
        spv = compile_one(kind, exe, src, os.path.splitext(name)[1])
        if spv.returncode != 0:
            print(spv.stderr.decode(), file=sys.stderr)
            return 1
        data = spv.stdout
        assert len(data) % 4 == 0 and data[:4] == b"\x03\x02\x23\x07", "SPIR-V basligi"
        ident = name.replace(".", "_")
        words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
        out = os.path.join(SHADERS, ident + "_spv.h")
        with open(out, "w") as f:
            f.write("// URETILMIS DOSYA — %s'den compile_shaders.py ile. Elle duzenleme.\n" % name)
            f.write("#pragma once\n#include <cstdint>\n")
            f.write("static const uint32_t %s_spv[] = {\n" % ident)
            for i in range(0, len(words), 8):
                f.write("  " + ", ".join("0x%08x" % w for w in words[i:i + 8]) + ",\n")
            f.write("};\nstatic const uint32_t %s_spv_size = %d; // bayt\n" % (ident, len(data)))
        print("%s -> %s (%d bayt)" % (name, os.path.basename(out), len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
