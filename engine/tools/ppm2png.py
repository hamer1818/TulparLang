#!/usr/bin/env python3
"""PPM (P6) -> PNG. Headless motor/editor ciktisini (write_ppm) GORMEK icin:
    python3 engine/tools/ppm2png.py x.ppm x.png
PIL varsa onu, yoksa saf zlib PNG yazicisini kullanir."""
import struct, sys, zlib

def read_ppm(path):
    data = open(path, "rb").read()
    tokens, pos = [], 0
    while len(tokens) < 4:
        while data[pos:pos+1].isspace(): pos += 1
        if data[pos:pos+1] == b"#":
            while data[pos:pos+1] not in (b"\n", b""): pos += 1
            continue
        start = pos
        while not data[pos:pos+1].isspace(): pos += 1
        tokens.append(data[start:pos])
    assert tokens[0] == b"P6", "yalniz P6"
    w, h = int(tokens[1]), int(tokens[2])
    pos += 1
    return w, h, data[pos:pos + w * h * 3]

def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)

if __name__ == "__main__":
    if len(sys.argv) != 3: sys.exit("kullanim: ppm2png.py giris.ppm cikis.png")
    w, h, rgb = read_ppm(sys.argv[1])
    try:
        from PIL import Image
        Image.frombytes("RGB", (w, h), rgb).save(sys.argv[2])
    except ImportError:
        write_png(sys.argv[2], w, h, rgb)
    print(f"{sys.argv[2]}: {w}x{h}")
