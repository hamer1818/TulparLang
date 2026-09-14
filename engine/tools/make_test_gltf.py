#!/usr/bin/env python3
"""Test varligi uretici: dama dokulu kup -> engine/tests/assets/checker_cube.gltf

Tek dosya: tampon ve PNG data URI olarak gomulu (cgltf ikisini de acar). Depoya
girer; belirlenimli (ayni girdi, ayni bayt). Yeniden uretmek:
  python3 engine/tools/make_test_gltf.py
"""
import base64, json, os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "tests", "assets", "checker_cube.gltf")


def png_rgba(w, h, px):
    raw = b"".join(b"\x00" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    # 64x64 dama: 8 piksellik kareler, turuncu / lacivert (parlaklik farki buyuk: test sayar)
    w = h = 64
    px = []
    for y in range(h):
        for x in range(w):
            on = ((x // 8) + (y // 8)) % 2 == 0
            px += [235, 120, 30, 255] if on else [25, 40, 110, 255]
    png = png_rgba(w, h, px)

    # Kup: yuz basina 4 vertex (pos, nrm, uv), 36 indeks. Sarim: disaridan CCW.
    faces = [((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
             ((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1))]
    pos, nrm, uv, idx = [], [], [], []
    for n, u, v in faces:
        c = [n[i] * 0.5 for i in range(3)]
        base = len(pos)
        for (su, sv, tu, tv) in [(-1, -1, 0, 0), (1, -1, 1, 0), (1, 1, 1, 1), (-1, 1, 0, 1)]:
            pos.append([c[i] + u[i] * 0.5 * su + v[i] * 0.5 * sv for i in range(3)])
            nrm.append(list(n))
            uv.append([tu, tv])
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def f32s(rows):
        return b"".join(struct.pack("<%df" % len(r), *r) for r in rows)
    bpos, bnrm, buv = f32s(pos), f32s(nrm), f32s(uv)
    bidx = struct.pack("<%dH" % len(idx), *idx)
    buf = bpos + bnrm + buv + bidx + (b"\x00" * ((4 - len(bidx) % 4) % 4))
    mn = [min(p[i] for p in pos) for i in range(3)]
    mx = [max(p[i] for p in pos) for i in range(3)]
    g = {
        "asset": {"version": "2.0", "generator": "tulpar make_test_gltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "checker_cube", "translation": [0, 0.5, 0]}],
        "meshes": [{"name": "cube", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                     "indices": 3, "material": 0}]}],
        "materials": [{"name": "checker", "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0}, "baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"uri": "data:image/png;base64," + base64.b64encode(png).decode(), "mimeType": "image/png"}],
        "buffers": [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode()}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(bpos), "target": 34962},
            {"buffer": 0, "byteOffset": len(bpos), "byteLength": len(bnrm), "target": 34962},
            {"buffer": 0, "byteOffset": len(bpos) + len(bnrm), "byteLength": len(buv), "target": 34962},
            {"buffer": 0, "byteOffset": len(bpos) + len(bnrm) + len(buv), "byteLength": len(bidx), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mn, "max": mx},
            {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uv), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
        ],
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(g, f, separators=(",", ":"), sort_keys=True)
    print("%s (%d bayt): %d vertex, %d indeks, %dx%d PNG" % (OUT, os.path.getsize(OUT), len(pos), len(idx), w, h))


if __name__ == "__main__":
    main()
