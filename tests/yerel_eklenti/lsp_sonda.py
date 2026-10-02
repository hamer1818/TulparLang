#!/usr/bin/env python3
"""LSP yerel eklentiyi goruyor mu (K303)? tests/yerel_eklenti.sh cagirir.

  lsp_sonda.py <tulpar> <proje_dizini>

<proje_dizini>'nde tulpar.toml [ext] paths eklentiyi gosterir; sunucuya HICBIR
bayrak verilmez — eklentiyi belgenin dizininden yukari bulmasi gerekir.
Olculen: hover imzasi + "(yerel eklenti: ornek)" etiketi, tamamlama listesinde
eklenti fonksiyonu, imza yardimi. Cikis 0 = hepsi tuttu.
"""
import json
import os
import subprocess
import sys


def mesaj(obj):
    b = json.dumps(obj).encode("utf-8")
    return b"Content-Length: " + str(len(b)).encode() + b"\r\n\r\n" + b


def oku(p):
    uzunluk = None
    while True:
        satir = p.stdout.readline()
        if not satir:
            return None
        satir = satir.strip()
        if not satir:
            break
        if satir.lower().startswith(b"content-length:"):
            uzunluk = int(satir.split(b":")[1])
    return json.loads(p.stdout.read(uzunluk))


def yanit(p, istek_id):
    while True:
        m = oku(p)
        if m is None:
            return None
        if m.get("id") == istek_id:
            return m


def main():
    tulpar, proje = sys.argv[1], os.path.abspath(sys.argv[2])
    dosya = os.path.join(proje, "kullan.tpr")
    uri = "file://" + (dosya if dosya.startswith("/") else "/" + dosya.replace("\\", "/"))
    metin = open(dosya, encoding="utf-8").read()
    env = dict(os.environ)
    env.pop("TULPAR_EXT_PATH", None)
    env["LC_ALL"] = "C"
    p = subprocess.Popen([tulpar, "--lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, cwd="/" if os.name != "nt" else None, env=env)
    hata = 0

    def gonder(o):
        p.stdin.write(mesaj(o))
        p.stdin.flush()

    gonder({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
    yanit(p, 1)
    gonder({"jsonrpc": "2.0", "method": "initialized", "params": {}})
    gonder({"jsonrpc": "2.0", "method": "textDocument/didOpen",
            "params": {"textDocument": {"uri": uri, "languageId": "tulpar", "version": 1, "text": metin}}})
    # `ornek_topla` gecen ilk satir
    satirlar = metin.split("\n")
    sn = next(i for i, s in enumerate(satirlar) if "ornek_topla(" in s and "print" in s)
    sk = satirlar[sn].index("ornek_topla") + 3
    gonder({"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": sn, "character": sk}}})
    h = yanit(p, 2)
    deger = (((h or {}).get("result") or {}).get("contents") or {}).get("value", "")
    if "ornek_topla(a: i64, b: i64): i64" in deger and "native extension: ornek" in deger:
        print("lsp: TAMAM hover imzasi + eklenti etiketi")
    else:
        print("lsp: HATA hover: %r" % deger)
        hata += 1
    gonder({"jsonrpc": "2.0", "id": 3, "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": sn, "character": 0}}})
    c = yanit(p, 3)
    ogeler = (c or {}).get("result") or []
    if isinstance(ogeler, dict):
        ogeler = ogeler.get("items", [])
    bul = [o for o in ogeler if o.get("label") == "ornek_selam"]
    if bul and bul[0].get("detail") == "ornek_selam(ad: str): str":
        print("lsp: TAMAM tamamlama listesinde ornek_selam(ad: str): str")
    else:
        print("lsp: HATA tamamlama: %r" % (bul[:1],))
        hata += 1
    sk2 = satirlar[sn].index("ornek_topla(") + len("ornek_topla(") + 1
    gonder({"jsonrpc": "2.0", "id": 4, "method": "textDocument/signatureHelp",
            "params": {"textDocument": {"uri": uri}, "position": {"line": sn, "character": sk2}}})
    s = yanit(p, 4)
    imzalar = ((s or {}).get("result") or {}).get("signatures") or []
    if imzalar and "ornek_topla(a: i64, b: i64)" in imzalar[0].get("label", ""):
        print("lsp: TAMAM imza yardimi")
    else:
        print("lsp: HATA imza yardimi: %r" % (imzalar,))
        hata += 1
    gonder({"jsonrpc": "2.0", "id": 9, "method": "shutdown", "params": None})
    yanit(p, 9)
    gonder({"jsonrpc": "2.0", "method": "exit", "params": None})
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
    return 1 if hata else 0


if __name__ == "__main__":
    sys.exit(main())
