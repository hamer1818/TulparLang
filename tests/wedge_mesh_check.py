#!/usr/bin/env python3
"""Kama (wedge) mesh'inin geometrik denetimi.

Rampa eskiden 12 kademeli kutu olarak çiziliyordu; artık gerçek bir mesh
(`tame_gen_wedge`, runtime/tame_impl.c). Sorun şu ki bu mesh'i GÖZLE
doğrulamak, pencere açmayı gerektiriyor — depoda yasak — ve ters sarılmış
bir üçgen arkayüz ayıklamasıyla sessizce GÖRÜNMEZ oluyor, yani "hata yok"
gibi duruyor.

Bu betik üç şeyi ölçüyor:
  1. Her üçgenin gerçek normali, ona atanan normalle aynı yönde mi
     (sarma yönü doğru mu).
  2. Katı KAPALI mı — her kenar tam iki kez, ters yönlerde geçiyor mu.
  3. Eğim yüzeyi, fiziğin kullandığı analitik yükseklikle AYNI tanımda mı
     (`_ramp_h3`, lib/scene3d.tpr). Ayrışırlarsa görünen rampa ile basılan
     rampa farklı yerlerde olur.

Üçgen listesi C KAYNAĞINDAN OKUNUYOR, burada kopyası tutulmuyor: kopya
tutsaydı C tarafı değişince denetim eski hâli doğrulamaya devam ederdi.
"""
import os
import re
import sys
import math

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "runtime", "tame_impl.c")


def fail(msg):
    print("HATA: " + msg)
    sys.exit(1)


def sub(p, q):
    return (p[0] - q[0], p[1] - q[1], p[2] - q[2])


def cross(u, v):
    return (u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0])


def dot(u, v):
    return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]


def unit(u):
    l = math.sqrt(dot(u, u))
    return (u[0] / l, u[1] / l, u[2] / l) if l else u


def main():
    with open(SRC, encoding="utf-8", errors="replace") as f:
        src = f.read()

    start = src.find("static Mesh tame_gen_wedge(")
    if start < 0:
        fail("tame_gen_wedge bulunamadi (runtime/tame_impl.c)")
    body = src[start:src.find("\nstatic ", start + 10)]

    # Somut boyutlar: scene3d_arena'daki rampalardan biri.
    a, b, c = 4.0, 1.8, 7.0
    hx, hy, hz = a / 2, b / 2, c / 2
    env = {"hx": hx, "hy": hy, "hz": hz, "a": a, "b": b, "c": c}

    # Vector3 A = {-hx, -hy, -hz}, B = {...};  — köşeler
    pts = {}
    for name, comp in re.findall(
            r"(\b[A-F]\b)\s*=\s*\{([^}]*)\}", body):
        vals = []
        for e in comp.split(","):
            e = e.strip().rstrip("f")
            vals.append(float(eval(e, {"__builtins__": {}}, env)))
        if len(vals) == 3:
            pts[name] = tuple(vals)
    if len(pts) != 6:
        fail("kose sayisi 6 degil, %d bulundu: %s" % (len(pts), sorted(pts)))

    # Normaller — HEPSİ kaynaktan okunuyor, eğim normali dahil.
    #
    # İlk yazımda `ns` burada TÜRETİLİYORDU (kaynaktan okunmuyordu) ve o
    # yüzden eğim normalini düz yukarı (0,1,0) yapan bozma denetimden KAÇTI:
    # betik kendi doğru değerini kullanıp kaynağınkine hiç bakmıyordu.
    # Ölçüldü — üç bozmadan yalnız ikisi yakalanmıştı.
    sl = math.sqrt(b * b + c * c)
    env["sl"] = sl

    def c_expr(e):
        """C ifadesini Python'a çevir: `x ? y : z` -> `(y if x else z)`."""
        e = e.strip().replace("sqrtf", "math.sqrt")
        e = re.sub(r"(\d)f\b", r"\1", e)          # 1.0f -> 1.0
        m = re.match(r"^\((.*?)\?(.*?):(.*)\)$", e)
        if m:
            return "((%s) if (%s) else (%s))" % (m.group(2), m.group(1),
                                                 m.group(3))
        return e

    norms = {}
    for name, comp in re.findall(
            r"Vector3 (n[a-z])\s*=\s*\{(.*?)\};", body, re.S):
        parts, depth, cur = [], 0, ""
        for ch in comp:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            if ch == "," and depth == 0:
                parts.append(cur)
                cur = ""
            else:
                cur += ch
        parts.append(cur)
        vals = []
        for e in parts:
            try:
                vals.append(float(eval(c_expr(e), {"math": math}, env)))
            except Exception as ex:
                fail("normal %s okunamadi: %r (%s)" % (name, e, ex))
        if len(vals) != 3:
            fail("normal %s uc bilesen degil: %s" % (name, vals))
        norms[name] = tuple(vals)
    for need in ("nd", "nb", "nl", "nr", "ns"):
        if need not in norms:
            fail("normal %s kaynakta bulunamadi" % need)

    tris = re.findall(
        r"tame_wedge_tri\([^,]+,[^,]+,[^,]+,\s*k,\s*"
        r"([A-F]),\s*([A-F]),\s*([A-F]),\s*(n[a-z])\)", body)
    if len(tris) != 8:
        fail("ucgen sayisi 8 degil, %d bulundu" % len(tris))

    # 1) Sarma yönü — tame_wedge_tri normale göre düzeltiyor, biz de öyle
    #    yapıp sonucun normalle uyuştuğunu doğruluyoruz.
    for (i0, i1, i2, nn) in tris:
        p0, p1, p2 = pts[i0], pts[i1], pts[i2]
        n = norms[nn]
        f = cross(sub(p1, p0), sub(p2, p0))
        if dot(f, n) < 0:
            p1, p2 = p2, p1
        g = unit(cross(sub(p1, p0), sub(p2, p0)))
        if dot(g, n) < 0.999:
            fail("ucgen %s%s%s normali %s ile uyusmuyor (%s)"
                 % (i0, i1, i2, nn, g))
        if abs(dot(g, g)) < 0.5:
            fail("ucgen %s%s%s dejenere (alan sifir)" % (i0, i1, i2))

    # 2) Kapalılık: düzeltilmiş sarmayla her kenar bir ileri bir geri.
    edges = {}
    for (i0, i1, i2, nn) in tris:
        p0, p1, p2 = pts[i0], pts[i1], pts[i2]
        n = norms[nn]
        if dot(cross(sub(p1, p0), sub(p2, p0)), n) < 0:
            p1, p2 = p2, p1
        for u, v in ((p0, p1), (p1, p2), (p2, p0)):
            edges[(u, v)] = edges.get((u, v), 0) + 1
    for e, cnt in edges.items():
        back = edges.get((e[1], e[0]), 0)
        if back != cnt:
            fail("kati KAPALI degil: kenar %s ileri %d, geri %d"
                 % (str(e), cnt, back))

    # 3) Eğim yüzeyi fiziğin analitik yüksekliğiyle aynı mı?
    #    _ramp_h3: k = (lz + hz) / sz, ust yuzey = -sy/2 + sy*k (merkeze göre)
    ns = norms["ns"]
    anchor = pts["A"]
    rhs = dot(ns, anchor)
    for k in (0.0, 0.25, 0.5, 0.75, 1.0):
        lz = -hz + k * c
        physics = -hy + b * k
        mesh_y = (rhs - ns[2] * lz) / ns[1]
        if abs(mesh_y - physics) > 1e-9:
            fail("egim yuzeyi fizikle ayrisiyor: k=%.2f mesh=%.9f fizik=%.9f "
                 "(gorunen rampa ile basilan rampa farkli yerde olur)"
                 % (k, mesh_y, physics))

    print("kama mesh'i saglam (8 ucgen, kapali, normaller dogru, "
          "egim fizikle ayni)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
