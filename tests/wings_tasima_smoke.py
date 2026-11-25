"""Wings: rota bazli govde siniri (413), statik mount basliklari + MIME,
guvenlik basliklari, CORS izinli liste, uc bazli Cache-Control / 302 / 410 —
GERCEK soketlerle (TULPAR_ISLERI.md B5, B7, B8, B9; hamza-backend Go
davranisi kaynak).

Olculenler:
  B5  max_body(1 MB) + max_body_prefix("/api/upload", 10 MB): 1 MB'yi asan
      Content-Length GOVDE GONDERILMEDEN 413 aliyor (yalniz baslik gonderilir,
      sunucu govdeyi beklemeden cevap vermeli); /api/upload altinda 2 MB
      kabul, 11 MB 413. Sinirin altindaki gercek govde 200. Sunucu
      ayakta kalir. (Eskiden: baglanti sessizce kapaniyordu, durum kodu yok.)
  B7  /uploads/ mount: CSP + CORP + Cache-Control basliklari; image/webp,
      image/svg+xml, image/jpeg; dizin listesi yok (404); POST statik dosya
      vermez; `..` ile yol asimi 404.
  B8  security_headers(): 200, 404, 413 ve OPTIONS dahil her yanitta nosniff,
      X-Frame-Options DENY, Referrer-Policy, HSTS; uc bazli Cache-Control;
      302 + Location; 410.
  B9  cors([liste]): izinli Origin TAM eslesmeyle yansitiliyor; listede
      olmayan (ve onek/sonek benzeri) origin hic ACAO almiyor; Vary: Origin
      hep var; preflight 204 izinli origin'e ACAO + yontemler + max-age 300;
      cached_get rotasi liste kipinde baska origin'e ilkinin ACAO'sunu
      sizdirmiyor.
"""

from __future__ import annotations

import http.client
import json
import os
import socket
import subprocess
import sys
import tempfile
import time


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["./tulpar", "tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("tulpar ikilisi yok (once ./build.sh, ya da TULPAR_EXE)")


SERVER = r'''
import "wings";

func health() {
    return {"status": "ok"};
}
func echo_len(req) {
    return {"n": length(req["body"])};
}
func stats(req) {
    return with_headers({"views": 1}, {"Cache-Control": "public, max-age=60"});
}
func go_link(req) {
    if (req["params"]["code"] == "gone") {
        return with_status({"message": "link expired"}, 410);
    }
    return redirect("https://example.com/hedef");
}
func cached() {
    return {"c": 1};
}

security_headers({});
cors(["https://hamza.tr", "https://www.hamza.tr"],
     {"methods": "GET, POST, PUT, PATCH, DELETE, OPTIONS",
      "headers": "Accept, Authorization, Content-Type, X-CSRF-Token",
      "expose": "Link", "credentials": true, "max_age": 300});
max_body(1048576);
max_body_prefix("/api/upload", 10485760);
get("/health", "health");
post("/api/echo", "echo_len");
post("/api/upload", "echo_len");
get("/api/stats", "stats");
get("/go/:code", "go_link");
cached_get("/api/cached", "cached");
static("/uploads/", env("SMOKE_UPLOADS"), {
    "Content-Security-Policy": "default-src 'none'; img-src 'self' data:; style-src 'unsafe-inline'; sandbox",
    "Cross-Origin-Resource-Policy": "cross-origin",
    "Cache-Control": "public, max-age=604800"});
listen_pool(toInt(env("SMOKE_PORT")), 4);
'''

SEC = {
    "x-content-type-options": "nosniff",
    "x-frame-options": "DENY",
    "referrer-policy": "strict-origin-when-cross-origin",
    "strict-transport-security": "max-age=31536000",
}


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def req(port, method, path, headers=None, body=None):
    try:
        return _req(port, method, path, headers, body)
    except OSError as e:
        return (-1, {}, f"{type(e).__name__}: {e}".encode())


def _req(port, method, path, headers=None, body=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
    h = {"Connection": "close"}
    h.update(headers or {})
    c.request(method, path, body=body, headers=h)
    r = c.getresponse()
    data = r.read()
    out = (r.status, {k.lower(): v for k, v in r.getheaders()}, data)
    c.close()
    return out


def head_only(port, path, length):
    """Yalniz baslik gonder (govde YOK), yaniti oku: sunucu govdeyi
    beklemeden cevap vermeli. Govdeyi bekleseydi 5 sn zaman asimi."""
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall((f"POST {path} HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
               f"Content-Length: {length}\r\n\r\n").encode())
    data = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        return None, data
    finally:
        s.close()
    line = data.split(b"\r\n", 1)[0].decode(errors="replace")
    parts = line.split(" ")
    return (int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else -1), data


def main() -> int:
    exe = find_tulpar_exe()
    d = tempfile.mkdtemp(prefix="tulpar_tasima_")
    up = os.path.join(d, "uploads")
    os.makedirs(os.path.join(up, "alt"))
    files = {"a.webp": b"RIFF\x00\x00\x00\x00WEBPVP8 ", "b.svg": b"<svg xmlns='http://www.w3.org/2000/svg'/>",
             "c.JPG": b"\xff\xd8\xff\xe0\x00\x10JFIF\x00", "alt/d.png": b"\x89PNG\r\n\x1a\n\x00\x00"}
    for n, b in files.items():
        with open(os.path.join(up, n), "wb") as fh:
            fh.write(b)
    with open(os.path.join(d, "gizli.txt"), "w") as fh:
        fh.write("GIZLI")
    src = os.path.join(d, "srv.tpr")
    with open(src, "w") as fh:
        fh.write(SERVER)
    binp = os.path.join(d, "srv")
    b = subprocess.run([exe, "build", src, binp], capture_output=True, text=True,
                       env=dict(os.environ, DISPLAY=""))
    if b.returncode != 0:
        print(b.stdout + b.stderr)
        print("FAIL: sunucu derlenmedi")
        return 1
    port = free_port()
    env = dict(os.environ, SMOKE_PORT=str(port), SMOKE_UPLOADS=up + "/",
               TULPAR_HTTP_QUIET="1", TULPAR_WINGS_NODOCS="1")
    env.pop("TULPAR_HTTP_MAX_BODY", None)
    p = subprocess.Popen([binp], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    fails = []

    def check(cond, why):
        if not cond:
            fails.append(why)

    def sec_ok(tag, hd):
        for k, v in SEC.items():
            check(hd.get(k) == v, f"{tag}: {k} = {hd.get(k)!r}")

    try:
        for _ in range(100):
            if req(port, "GET", "/health")[0] == 200:
                break
            time.sleep(0.05)
        else:
            print("FAIL: sunucu acilmadi: " + p.stderr.read().decode()[-500:])
            return 1

        # ---- B5 govde siniri
        st, raw = head_only(port, "/api/echo", 2 * 1024 * 1024)
        check(st == 413, f"B5: 2 MB /api/echo -> {st} (govde beklenmeden 413 olmali)")
        check(b"request body too large" in raw, "B5: 413 govdesi")
        if raw:
            hd413 = {l.split(b":", 1)[0].decode().lower(): l.split(b":", 1)[1].strip().decode()
                     for l in raw.split(b"\r\n\r\n")[0].split(b"\r\n")[1:] if b":" in l}
            sec_ok("B5/413", hd413)
        st, _ = head_only(port, "/api/upload", 11 * 1024 * 1024)
        check(st == 413, f"B5: 11 MB /api/upload -> {st}")
        st, _ = head_only(port, "/api/upload/alt", 11 * 1024 * 1024)
        check(st == 413, f"B5: 11 MB /api/upload/alt -> {st}")
        # /api/uploadx onek degil mi? En uzun onek eslesmesi: "/api/upload" onegi
        # "/api/uploadx"i de kapsar (Go strings.HasPrefix ile ayni) — 2 MB kabul.
        big = b"x" * (2 * 1024 * 1024)
        st, _, data = req(port, "POST", "/api/upload", {"Content-Type": "application/octet-stream"}, big)
        check(st == 200 and data.startswith(b"{") and json.loads(data).get("n") == len(big), f"B5: 2 MB upload -> {st} {data[:80]!r}")
        small = b"y" * (1024 * 1024 - 400)
        st, _, data = req(port, "POST", "/api/echo", {"Content-Type": "application/octet-stream"}, small)
        check(st == 200 and data.startswith(b"{") and json.loads(data).get("n") == len(small), f"B5: 1 MB alti /api/echo -> {st}")
        st, _, _ = req(port, "GET", "/health")
        check(st == 200, "B5: 413'lerden sonra sunucu yanit vermiyor")

        # ---- B7 statik
        expect = {"/uploads/a.webp": "image/webp", "/uploads/b.svg": "image/svg+xml",
                  "/uploads/c.JPG": "image/jpeg", "/uploads/alt/d.png": "image/png"}
        for path, ct in expect.items():
            st, hd, data = req(port, "GET", path)
            check(st == 200, f"B7: {path} -> {st}")
            check(hd.get("content-type") == ct, f"B7: {path} tur {hd.get('content-type')!r}")
            check(hd.get("content-security-policy", "").startswith("default-src 'none'")
                  and hd.get("content-security-policy", "").endswith("sandbox"), f"B7: {path} CSP")
            check(hd.get("cross-origin-resource-policy") == "cross-origin", f"B7: {path} CORP")
            check(hd.get("cache-control") == "public, max-age=604800", f"B7: {path} Cache-Control")
            check(data == files[path[len("/uploads/"):]], f"B7: {path} icerik")
            sec_ok("B7 " + path, hd)
        for path in ("/uploads/", "/uploads/alt/", "/uploads/alt"):
            st, _, data = req(port, "GET", path)
            check(st == 404, f"B7: dizin {path} -> {st} {data[:60]!r}")
        for path in ("/uploads/../gizli.txt", "/uploads/alt/../../gizli.txt", "/uploads/%2e%2e/gizli.txt"):
            st, _, data = req(port, "GET", path)
            check(st == 404 and b"GIZLI" not in data, f"B7: yol asimi {path} -> {st}")
        st, hd, _ = req(port, "GET", "/api/stats")
        check("content-security-policy" not in hd, "B7: mount basligi baska yanita sizdi")
        st, _, data = req(port, "POST", "/uploads/a.webp", {"Content-Type": "text/plain"}, b"")
        check(st == 404 and not data.startswith(b"RIFF"), f"B7: POST statik dosya verdi ({st})")

        # ---- B8 basliklar / durumlar
        st, hd, _ = req(port, "GET", "/api/stats")
        check(st == 200 and hd.get("cache-control") == "public, max-age=60", f"B8: stats Cache-Control {hd.get('cache-control')!r}")
        sec_ok("B8 200", hd)
        st, hd, _ = req(port, "GET", "/yok")
        check(st == 404, f"B8: 404 -> {st}")
        sec_ok("B8 404", hd)
        st, hd, _ = req(port, "GET", "/go/abc")
        check(st == 302 and hd.get("location") == "https://example.com/hedef", f"B8: 302 {st} {hd.get('location')!r}")
        st, hd, data = req(port, "GET", "/go/gone")
        check(st == 410 and b"link expired" in data, f"B8: 410 -> {st}")

        # ---- B9 CORS
        st, hd, _ = req(port, "GET", "/api/stats", {"Origin": "https://hamza.tr"})
        check(hd.get("access-control-allow-origin") == "https://hamza.tr", f"B9: izinli origin {hd.get('access-control-allow-origin')!r}")
        check("origin" in hd.get("vary", "").lower(), "B9: Vary: Origin yok")
        check(hd.get("access-control-allow-credentials") == "true", "B9: credentials")
        check(hd.get("access-control-expose-headers") == "Link", "B9: expose")
        st, hd, _ = req(port, "GET", "/api/stats", {"Origin": "https://www.hamza.tr"})
        check(hd.get("access-control-allow-origin") == "https://www.hamza.tr", "B9: ikinci izinli origin")
        for bad in ("https://evil.com", "https://hamza.tr.evil.com", "https://evilhamza.tr",
                    "http://hamza.tr", "https://hamza.tr:8443", "null", "*"):
            st, hd, _ = req(port, "GET", "/api/stats", {"Origin": bad})
            check("access-control-allow-origin" not in hd, f"B9: izinsiz origin {bad!r} ACAO aldi: {hd.get('access-control-allow-origin')!r}")
            check("origin" in hd.get("vary", "").lower(), f"B9: {bad} Vary yok")
        st, hd, _ = req(port, "GET", "/api/stats")
        check("access-control-allow-origin" not in hd, "B9: Origin'siz istekte ACAO")
        st, hd, _ = req(port, "OPTIONS", "/api/stats", {"Origin": "https://hamza.tr",
                                                       "Access-Control-Request-Method": "POST"})
        check(st == 204, f"B9: preflight -> {st}")
        check(hd.get("access-control-allow-origin") == "https://hamza.tr", "B9: preflight ACAO")
        check("PATCH" in hd.get("access-control-allow-methods", ""), "B9: preflight yontemler")
        check("X-CSRF-Token" in hd.get("access-control-allow-headers", ""), "B9: preflight basliklar")
        check(hd.get("access-control-max-age") == "300", "B9: max-age")
        sec_ok("B9 preflight", hd)
        st, hd, _ = req(port, "OPTIONS", "/api/stats", {"Origin": "https://evil.com",
                                                       "Access-Control-Request-Method": "POST"})
        check("access-control-allow-origin" not in hd, "B9: izinsiz preflight ACAO aldi")
        st, hd, _ = req(port, "GET", "/api/cached", {"Origin": "https://hamza.tr"})
        check(hd.get("access-control-allow-origin") == "https://hamza.tr", "B9: cached ilk istek ACAO")
        st, hd, _ = req(port, "GET", "/api/cached", {"Origin": "https://evil.com"})
        check("access-control-allow-origin" not in hd, f"B9: cached rota ilk origin'i sizdirdi: {hd.get('access-control-allow-origin')!r}")
        st, hd, _ = req(port, "GET", "/api/cached", {"Origin": "https://www.hamza.tr"})
        check(hd.get("access-control-allow-origin") == "https://www.hamza.tr", "B9: cached ikinci origin")

        check(p.poll() is None, f"sunucu oldu (rc={p.poll()})")
    finally:
        p.kill()
        p.wait()

    if fails:
        for f in fails:
            print("  - " + f)
        print(f"FAIL: wings_tasima_smoke {len(fails)} kontrol kirmizi")
        return 1
    print("PASS: wings_tasima_smoke — 413 govde okunmadan (rota onekli), statik CSP/CORP/"
          "MIME + listeleme/asim yok, guvenlik basliklari her yanitta, 302/410/"
          "Cache-Control, CORS izinli liste tam eslesme (+ cached rota sizmiyor)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
