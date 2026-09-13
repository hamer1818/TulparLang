#!/usr/bin/env python3
"""GLSL -> SPIR-V -> C dizisi (engine/rhi/shaders/*_spv.h).

Faz 1: shader'lar GLSL (glslc). Plan Faz 8'e kadar Slang diyordu; bu makinede
ve CI'da slangc yok (Arch'taki `slang` paketi S-Lang kutuphanesi, shader
Slang degil). Uretilen .h dosyalari DEPOYA GIRER: CI'da glslc gerekmez ve
byte'lar deterministiktir. Yeniden uretmek: python3 engine/tools/compile_shaders.py
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHADERS = os.path.join(os.path.dirname(HERE), "rhi", "shaders")


def main():
    glslc = shutil.which("glslc")
    if not glslc:
        print("glslc yok (shaderc paketi)", file=sys.stderr)
        return 2
    for name in sorted(os.listdir(SHADERS)):
        if not name.endswith((".vert", ".frag", ".comp")):
            continue
        src = os.path.join(SHADERS, name)
        spv = subprocess.run([glslc, "-O", "--target-env=vulkan1.1", "-o", "-", src],
                             capture_output=True)
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
