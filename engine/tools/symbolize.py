#!/usr/bin/env python3
"""Crash raporunu sembollere cevirir — "sembol sunucusu"nun Faz 0 hali.

Kullanim:
    symbolize.py <rapor.txt> [--symbols <dizin>] [--binary <stripsiz ikili>]

Rapor satirlari: "  #N 0x<pc> <modul>+0x<ofset> (sembol?)". Her modul icin
stripsiz ikili su sirayla aranir: --binary, <symbols>/<build>/<modul adi>,
raporun modul yolu (gelistirici makinesinde ayni yolda duruyorsa). Cozum
llvm-symbolizer ya da addr2line ile; ikisi de yoksa rapor oldugu gibi basilir
ve cikis kodu 2 (GORUNUR atlama, sessiz degil).
"""
import os
import re
import shutil
import subprocess
import sys

LINE = re.compile(r'^\s*#(\d+)\s+(0x[0-9a-f]+)\s+(\S+)\+(0x[0-9a-f]+)(?:\s+\((.*)\))?')


def find_tool():
    for t in ("llvm-symbolizer", "addr2line"):
        p = shutil.which(t)
        if p:
            return t, p
    return None, None


def symbolize(tool, path, binary, offsets):
    # -i (inline zinciri) YOK: cikti adres basina degisken satir sayisi
    # verir ve eslesme kayar. Faz 0: adres basina bir fonksiyon + konum.
    if tool == "llvm-symbolizer":
        out = subprocess.run([path, "-e", binary, "-f", "-C"] + offsets,
                             capture_output=True, text=True).stdout
        blocks = [b for b in out.strip().split("\n\n") if b.strip()]
        return [b.strip().replace("\n", " @ ") for b in blocks]
    out = subprocess.run([path, "-e", binary, "-f", "-C"] + offsets,
                         capture_output=True, text=True).stdout.splitlines()
    res, i = [], 0
    while i + 1 < len(out):
        res.append("%s @ %s" % (out[i], out[i + 1]))
        i += 2
    return res


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    report = sys.argv[1]
    symbols = None
    binary = None
    args = sys.argv[2:]
    while args:
        a = args.pop(0)
        if a == "--symbols":
            symbols = args.pop(0)
        elif a == "--binary":
            binary = args.pop(0)
    lines = open(report, encoding="utf-8", errors="replace").read().splitlines()
    build = "dev"
    for l in lines:
        if l.startswith("build: "):
            build = l[7:].strip()
    tool, tool_path = find_tool()
    if not tool:
        print("\n".join(lines))
        print("symbolize: ATLANDI — llvm-symbolizer/addr2line yok", file=sys.stderr)
        return 2
    per_module = {}
    frames = []
    for l in lines:
        m = LINE.match(l)
        if not m:
            continue
        idx, pc, mod, off, sym = m.groups()
        frames.append((int(idx), pc, mod, off, sym))
        per_module.setdefault(mod, []).append(off)
    resolved = {}
    for mod, offs in per_module.items():
        cands = []
        if binary:
            cands.append(binary)
        if symbols:
            cands.append(os.path.join(symbols, build, os.path.basename(mod)))
        cands.append(mod)
        bin_path = next((c for c in cands if os.path.isfile(c)), None)
        if not bin_path:
            continue
        try:
            names = symbolize(tool, tool_path, bin_path, offs)
        except OSError:
            continue
        for off, name in zip(offs, names):
            resolved[(mod, off)] = name
    out_lines = []
    for l in lines:
        m = LINE.match(l)
        if m:
            idx, pc, mod, off, sym = m.groups()
            name = resolved.get((mod, off))
            out_lines.append("  #%s %s %s+%s  %s" % (idx, pc, os.path.basename(mod), off, name or (sym or "?")))
        else:
            out_lines.append(l)
    print("\n".join(out_lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
