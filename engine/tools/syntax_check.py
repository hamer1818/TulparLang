#!/usr/bin/env python3
"""Hizli sozdizimi on-denetimi: CI'i derleyici olarak kullanmayi birak.

NEDEN: bu depoda bir tur, once hic derlenmemis kodun uzerine calisildigi icin
yedi CI turuna mal oldu (her tur ~5 dakika bekleme). Sebep yapisaldi:
build.yml yalniz main/master'a acilan PR'larda kosuyor, calisma dallarinda
kosmuyor -- yani birlesen kod derlenmeden birlesiyor.

Bu betik `g++ -fsyntax-only` ile TEK TEK ceviri birimlerini denetler. Baglama
(link) yapmaz, kod uretmez; amaci "bu dosya derleyiciden gecer mi" sorusunu
saniyeler icinde yanitlamak.

KAPSAM VE SINIRLARI
  * Baglayici hatalarini GORMEZ (tanimsiz sembol, cift tanim). Onlar icin
    gercek derleme gerekir.
  * Windows'ta kosarken POSIX-ozgu farklar gurultu uretir (ornek: mkdir()
    POSIX'te iki, Windows'ta tek argumanli). Bunlar PLATFORM_NOISE ile
    eleniyor -- CI Linux/macOS'ta kosuyor ve orada dogru derleniyorlar.
  * third_party taranmaz.

Kullanim:
    python tools/syntax_check.py [kok] [--cxx /yol/g++] [--only content,renderer]

Donus: 0 = temiz, 1 = en az bir gercek hata.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

INCLUDE_DIRS = [
    ".",
    "third_party/vulkan",
    "third_party/cgltf",
    "third_party/stb",
    "third_party/meshoptimizer",
    "third_party/astcenc",
    "third_party/recast/Detour/Include",
    "third_party/recast/Recast/Include",
    "third_party/recast/DetourTileCache/Include",
    "third_party/jolt",
    "third_party/imgui",
    "third_party/iconfont",
    "third_party/tracy",
    "third_party/debug_draw",
    "third_party/miniaudio",
    "third_party/mdt",
    "third_party/glfw",
    "third_party",  # ratas/timer-wheel.h gibi onekli include'lar icin
]

SCAN_DIRS = ["core", "platform", "rhi", "renderer", "sim", "content", "audio", "bridge", "app"]

# CMake'te BILEREK olmayan dosyalar (gerekli kutuphane vendor'lanmamis).
SKIP_FILES = {
    "renderer/nextgen_render.cpp",  # Intel MaskedOcclusionCulling vendor'lanmadi (CMakeLists.txt:229)
}

# Windows'ta kosarken cikan, Linux/macOS'ta OLMAYAN farklar.
PLATFORM_NOISE = [
    re.compile(r"too many arguments to function 'int mkdir"),
    re.compile(r"dlfcn\.h: No such file"),
    re.compile(r"sys/mman\.h: No such file"),
    re.compile(r"pthread\.h: No such file"),
    re.compile(r"unistd\.h: No such file"),
    # POSIX-ozgu API'ler: Windows'ta yok, CI'in kostugu Linux/macOS'ta var.
    re.compile(r"'sysconf' was not declared"),
    re.compile(r"'_SC_[A-Z_]+' was not declared"),
    re.compile(r"'::pipe' has not been declared"),
    re.compile(r"'::fcntl' has not been declared"),
    re.compile(r"'F_[A-Z]+' was not declared"),
    re.compile(r"'O_NONBLOCK' was not declared"),
    re.compile(r"has no member named 'st_mtim'"),
]


def is_noise(line):
    return any(p.search(line) for p in PLATFORM_NOISE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default=".")
    ap.add_argument("--cxx", default=None, help="derleyici (varsayilan: PATH'teki g++/clang++)")
    ap.add_argument("--only", default=None, help="virgulle ayrilmis dizinler")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    cxx = a.cxx or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        print("sozdizimi kapisi: derleyici bulunamadi (g++/clang++ PATH'te yok)")
        print("  Windows'ta:  winget install BrechtSanders.WinLibs.POSIX.UCRT")
        return 1

    dirs = a.only.split(",") if a.only else SCAN_DIRS
    inc = []
    for d in INCLUDE_DIRS:
        p = os.path.join(a.root, d)
        if os.path.isdir(p):
            inc += ["-I", p]

    files = []
    for d in dirs:
        base = os.path.join(a.root, d)
        for cur, _, names in os.walk(base):
            for n in sorted(names):
                if not n.endswith(".cpp"):
                    continue
                rel = os.path.relpath(os.path.join(cur, n), a.root).replace(os.sep, "/")
                if rel in SKIP_FILES:
                    continue
                files.append(rel)

    bad = []
    for rel in files:
        cmd = [cxx, "-fsyntax-only", "-std=c++17", "-fno-exceptions", "-fno-rtti",
               "-DENGINE_SOURCE_DIR=\".\""] + inc + [os.path.join(a.root, rel)]
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        if r.returncode == 0:
            continue
        errs = [l for l in r.stderr.split("\n") if ": error:" in l or ": fatal error:" in l]
        real = [l for l in errs if not is_noise(l)]
        if real:
            bad.append((rel, real))
        elif a.verbose:
            print("  (yalniz platform gurultusu) %s" % rel)

    print("sozdizimi kapisi: %s | %d ceviri birimi, %d hatali"
          % (os.path.basename(cxx), len(files), len(bad)))
    if not bad:
        print("sozdizimi kapisi: temiz")
        print("  NOT: baglayici hatalari bu denetimde GORUNMEZ (tanimsiz sembol vb.).")
        return 0
    for rel, errs in bad:
        print("  %s" % rel)
        for e in errs[:4]:
            print("      %s" % e.strip())
        if len(errs) > 4:
            print("      ... +%d" % (len(errs) - 4))
    return 1


if __name__ == "__main__":
    sys.exit(main())
