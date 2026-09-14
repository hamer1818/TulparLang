#!/usr/bin/env python3
"""Test varligi uretici: dama dokulu kup -> engine/tests/assets/checker_cube.gltf
                       UV kure (LOD/meshopt testi) -> engine/tests/assets/lod_sphere.gltf

Tek dosya: tampon ve PNG data URI olarak gomulu (cgltf ikisini de acar). Depoya
girer; belirlenimli (ayni girdi, ayni bayt). Yeniden uretmek:
  python3 engine/tools/make_test_gltf.py
"""
import base64, json, os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "tests", "assets", "checker_cube.gltf")
OUT_SPHERE = os.path.join(os.path.dirname(HERE), "tests", "assets", "lod_sphere.gltf")
OUT_SKIN = os.path.join(os.path.dirname(HERE), "tests", "assets", "skin_tube.gltf")


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


def sphere():
    """UV kure: 48 dilim x 24 halka (~2300 ucgen). Dikis vertex'leri cift (uv icin);
    meshopt tekillestirmesi bunlari BIRLESTIRMEZ (uv farkli) — normal. Disaridan CCW."""
    import math
    S, R = 48, 24
    pos, nrm, uv, idx = [], [], [], []
    for r in range(R + 1):
        th = math.pi * r / R
        for s_ in range(S + 1):
            ph = 2 * math.pi * s_ / S
            p = [math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph)]
            pos.append([round(c, 6) for c in p]); nrm.append([round(c, 6) for c in p]); uv.append([s_ / S, r / R])
    for r in range(R):
        for s_ in range(S):
            a = r * (S + 1) + s_; b = a + S + 1; c = a + 1; d = b + 1
            for tri in ([a, b, c], [c, b, d]):
                p0, p1, p2 = pos[tri[0]], pos[tri[1]], pos[tri[2]]
                if p0 == p1 or p1 == p2 or p0 == p2:
                    continue  # kutup: yoz ucgen
                e1 = [p1[i] - p0[i] for i in range(3)]; e2 = [p2[i] - p0[i] for i in range(3)]
                n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                cen = [(p0[i] + p1[i] + p2[i]) / 3 for i in range(3)]
                if sum(n[i] * cen[i] for i in range(3)) < 0:
                    tri = [tri[0], tri[2], tri[1]]  # icten CCW ise cevir
                idx += tri
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
        "nodes": [{"mesh": 0, "name": "lod_sphere"}],
        "meshes": [{"name": "sphere", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                       "indices": 3, "material": 0}]}],
        "materials": [{"name": "flat", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.9, 0.9, 1], "metallicFactor": 0}}],
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
    with open(OUT_SPHERE, "w") as f:
        json.dump(g, f, separators=(",", ":"), sort_keys=True)
    print("%s (%d bayt): %d vertex, %d indeks (%d ucgen)" % (OUT_SPHERE, os.path.getsize(OUT_SPHERE), len(pos), len(idx), len(idx) // 3))


def skin_tube():
    """Iskeletli boru (2 eklem): kok (0,0,0) ve uc (0,1,0). Halkalar y=0..2; y<1 koke,
    y>1 uca, y=1 yari yariya bagli. Animasyon "bend": uc eklem 1 s'de Z etrafinda 0->90
    derece doner. Beklenen: t=1'de tepe (0,2,0) -> (-1,1,0). Iskelet/animasyon ice
    aktarma + GPU skinning testi (content_skinned_gltf_bends)."""
    import math
    S, R, rad = 12, 4, 0.15
    ys = [0.0, 0.5, 1.0, 1.5, 2.0]
    pos, nrm, uv, joints, weights, idx = [], [], [], [], [], []
    for ri, y in enumerate(ys):
        for s_ in range(S + 1):
            ph = 2 * math.pi * s_ / S
            n = [math.cos(ph), 0.0, math.sin(ph)]
            pos.append([round(rad * n[0], 6), y, round(rad * n[2], 6)])
            nrm.append([round(n[0], 6), 0.0, round(n[2], 6)])
            uv.append([s_ / S, y / 2.0])
            if y < 1.0:
                joints.append([0, 0, 0, 0]); weights.append([1.0, 0.0, 0.0, 0.0])
            elif y > 1.0:
                joints.append([1, 0, 0, 0]); weights.append([1.0, 0.0, 0.0, 0.0])
            else:
                joints.append([0, 1, 0, 0]); weights.append([0.5, 0.5, 0.0, 0.0])
    for r in range(R):
        for s_ in range(S):
            a = r * (S + 1) + s_; b = a + S + 1; c = a + 1; d = b + 1
            for tri in ([a, b, c], [c, b, d]):
                p0, p1, p2 = pos[tri[0]], pos[tri[1]], pos[tri[2]]
                e1 = [p1[i] - p0[i] for i in range(3)]; e2 = [p2[i] - p0[i] for i in range(3)]
                n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                cen = [(p0[0] + p1[0] + p2[0]) / 3, 0.0, (p0[2] + p1[2] + p2[2]) / 3]  # eksenden disari
                if sum(n[i] * cen[i] for i in range(3)) < 0:
                    tri = [tri[0], tri[2], tri[1]]
                idx += tri
    def f32s(rows):
        return b"".join(struct.pack("<%df" % len(r), *r) for r in rows)
    def pad4(b):
        return b + b"\x00" * ((4 - len(b) % 4) % 4)
    ibm = [[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
           [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]]  # sutun-major: T(0,-1,0)
    times = [[0.0], [0.5], [1.0]]
    rots = [[0.0, 0.0, math.sin(a / 2), math.cos(a / 2)] for a in (0.0, math.pi / 4, math.pi / 2)]
    parts = [pad4(f32s(pos)), pad4(f32s(nrm)), pad4(f32s(uv)),
             pad4(b"".join(struct.pack("<4B", *j) for j in joints)), pad4(f32s(weights)),
             pad4(struct.pack("<%dH" % len(idx), *idx)), pad4(f32s(ibm)), pad4(f32s(times)), pad4(f32s(rots))]
    buf = b"".join(parts)
    views, off = [], 0
    for i, part in enumerate(parts):
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(part)}
        if i < 5: v["target"] = 34962
        elif i == 5: v["target"] = 34963
        views.append(v); off += len(part)
    mn = [min(p[i] for p in pos) for i in range(3)]
    mx = [max(p[i] for p in pos) for i in range(3)]
    g = {
        "asset": {"version": "2.0", "generator": "tulpar make_test_gltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 2]}],
        "nodes": [{"name": "root", "translation": [0, 0, 0], "children": [1]},
                  {"name": "tip", "translation": [0, 1, 0]},
                  {"name": "tube", "mesh": 0, "skin": 0}],
        "skins": [{"name": "tube_skin", "joints": [0, 1], "inverseBindMatrices": 6, "skeleton": 0}],
        "meshes": [{"name": "tube", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2,
                                                                    "JOINTS_0": 3, "WEIGHTS_0": 4},
                                                     "indices": 5, "material": 0}]}],
        "materials": [{"name": "flat", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.9, 0.9, 1], "metallicFactor": 0}}],
        "animations": [{"name": "bend",
                        "samplers": [{"input": 7, "output": 8, "interpolation": "LINEAR"}],
                        "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}]}],
        "buffers": [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode()}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mn, "max": mx},
            {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uv), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5121, "count": len(joints), "type": "VEC4"},
            {"bufferView": 4, "componentType": 5126, "count": len(weights), "type": "VEC4"},
            {"bufferView": 5, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
            {"bufferView": 6, "componentType": 5126, "count": 2, "type": "MAT4"},
            {"bufferView": 7, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [1.0]},
            {"bufferView": 8, "componentType": 5126, "count": 3, "type": "VEC4"},
        ],
    }
    with open(OUT_SKIN, "w") as f:
        json.dump(g, f, separators=(",", ":"), sort_keys=True)
    print("%s (%d bayt): %d vertex, %d ucgen, 2 eklem, 1 klip" % (OUT_SKIN, os.path.getsize(OUT_SKIN), len(pos), len(idx) // 3))


def checker_png():
    """64x64 dama PNG (KTX2/ASTC testi: engine_texpack ile checker_64.ktx2 uretilir)."""
    w = h = 64
    px = []
    for y in range(h):
        for x in range(w):
            on = ((x // 8) + (y // 8)) % 2 == 0
            px += [235, 120, 30, 255] if on else [25, 40, 110, 255]
    out = os.path.join(os.path.dirname(HERE), "tests", "assets", "checker_64.png")
    with open(out, "wb") as f:
        f.write(png_rgba(w, h, px))
    print("%s (%d bayt)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
    sphere()
    skin_tube()
    checker_png()
