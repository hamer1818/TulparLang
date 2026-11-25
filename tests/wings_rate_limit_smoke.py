"""Wings hiz sinirlayici + paylasilan sayac, GERCEK soketlerle ve
`listen_pool(port, 16)` altinda (TULPAR_ISLERI.md A3 + A4).

Olculenler:
  1. client_ip: loopback esten XFF yok sayilir, X-Real-IP (vekil) alinir.
  2. Sahte X-Forwarded-For ile sinir asilamaz (eskiden her istekte yeni bir
     XFF degeri yeni bir kova demekti).
  3. Iki vekilli istemci (farkli X-Real-IP) birbirini kilitlemez; sinir
     ROTA DESENINE bagli (/g/a ve /g/b ayni kovayi paylasir — Go'daki
     r.With(limiter) gibi).
  4. 16 isci x 64 eszamanli baglanti: 1500 istekte sinir 1000 -> TAM 1000
     tane 200 ve 500 tane 429 (kayip ya da fazla sayim yok).
  5. shared_incr sayaci 2000 eszamanli istekte kayipsiz: son deger 2000 ve
     donen degerler 1..2000'in tam kendisi.
  6. Giris akisi: 10 yanlis -> 11.'si 429 + Retry-After; baska IP etkilenmez;
     dogru parola sayaci sifirlar.
"""

from __future__ import annotations

import concurrent.futures as cf
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

func hit(req) {
    return {"ok": 1};
}
func g(req) {
    return {"ok": 1};
}
func whoami(req) {
    return {"ip": client_ip(req)};
}
func count(req) {
    return {"n": shared_incr("smoke.sayac", 0)};
}
func read_count(req) {
    return {"n": toInt(shared_get("smoke.sayac"))};
}
func login(req) {
    str ip = client_ip(req);
    if (!limiter_allow("login", ip, 10, 900)) {
        return too_many_requests(limiter_retry_after("login", ip, 900));
    }
    if (req["params"]["pw"] == "dogru") {
        limiter_reset("login", ip);
        return {"ok": 1};
    }
    return {"error": "bad credentials", "_status": 401};
}

post("/hit", "hit");
get("/g/:x", "g");
get("/whoami", "whoami");
get("/count", "count");
get("/read", "read_count");
post("/login/:pw", "login");
rate_limit_route("POST", "/hit", 1000, 60);
rate_limit_route("GET", "/g/:x", 3, 60);
listen_pool(toInt(env("SMOKE_PORT")), 16);
'''


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def req(port, method, path, headers=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
    h = {"Connection": "close"}
    h.update(headers or {})
    c.request(method, path, body=b"" if method == "POST" else None, headers=h)
    r = c.getresponse()
    body = r.read()
    out = (r.status, dict((k.lower(), v) for k, v in r.getheaders()), body)
    c.close()
    return out


def main() -> int:
    exe = find_tulpar_exe()
    d = tempfile.mkdtemp(prefix="tulpar_rl_")
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
    env = dict(os.environ, SMOKE_PORT=str(port), TULPAR_HTTP_QUIET="1",
               TULPAR_WINGS_NODOCS="1")
    p = subprocess.Popen([binp], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    fails = []

    def check(cond, why):
        if not cond:
            fails.append(why)

    try:
        for _ in range(100):
            try:
                req(port, "GET", "/whoami")
                break
            except OSError:
                time.sleep(0.05)
        else:
            print("FAIL: sunucu acilmadi: " + p.stderr.read().decode()[-500:])
            return 1

        # 1. client_ip
        ip = lambda h: json.loads(req(port, "GET", "/whoami", h)[2])["ip"]
        check(ip({}) == "127.0.0.1", f"es: {ip({})}")
        check(ip({"X-Forwarded-For": "9.9.9.9"}) == "127.0.0.1", "XFF okundu")
        check(ip({"X-Real-IP": "198.51.100.9"}) == "198.51.100.9", "vekil X-Real-IP alinmadi")
        check(ip({"X-Real-IP": "yalan"}) == "127.0.0.1", "gecersiz X-Real-IP alindi")

        # 2. Sahte XFF siniri asamaz (anahtar = es 127.0.0.1)
        codes = [req(port, "GET", f"/g/a", {"X-Forwarded-For": f"10.0.0.{i}"})[0]
                 for i in range(6)]
        check(codes == [200, 200, 200, 429, 429, 429], f"XFF ile sinir asildi: {codes}")
        st, hd, _ = req(port, "GET", "/g/a", {"X-Forwarded-For": "1.1.1.1"})
        ra = hd.get("retry-after", "")
        check(st == 429 and ra.isdigit() and 1 <= int(ra) <= 60, f"Retry-After: {st} {ra!r}")

        # 3. Iki vekilli istemci birbirini kilitlemez; kova rota deseninde
        A = {"X-Real-IP": "198.51.100.1"}
        B = {"X-Real-IP": "198.51.100.2"}
        ca = [req(port, "GET", "/g/a" if i % 2 else "/g/b", A)[0] for i in range(4)]
        check(ca == [200, 200, 200, 429], f"A (desen paylasimi): {ca}")
        cb = [req(port, "GET", "/g/z", B)[0] for i in range(3)]
        check(cb == [200, 200, 200], f"B, A tarafindan kilitlendi: {cb}")
        check(req(port, "GET", "/whoami", A)[0] == 200, "sinirsiz rota sinirlandi")

        # 4. Pool altinda sinir sayimi kayipsiz
        C = {"X-Real-IP": "203.0.113.50"}
        with cf.ThreadPoolExecutor(64) as ex:
            res = list(ex.map(lambda _: req(port, "POST", "/hit", C)[0], range(1500)))
        n200, n429 = res.count(200), res.count(429)
        check(n200 == 1000 and n429 == 500, f"pool sinir sayimi: 200={n200} 429={n429} diger={1500 - n200 - n429}")

        # 5. shared_incr sayaci kayipsiz
        with cf.ThreadPoolExecutor(64) as ex:
            ns = list(ex.map(lambda _: json.loads(req(port, "GET", "/count")[2])["n"], range(2000)))
        last = json.loads(req(port, "GET", "/read")[2])["n"]
        check(last == 2000, f"sayac kayipli: {last}")
        check(sorted(ns) == list(range(1, 2001)), "donen degerler 1..2000 degil (yinelenen/eksik)")

        # 6. Giris akisi
        L = {"X-Real-IP": "192.0.2.10"}
        M = {"X-Real-IP": "192.0.2.11"}
        lc = [req(port, "POST", "/login/yanlis", L)[0] for _ in range(11)]
        check(lc == [401] * 10 + [429], f"giris: {lc}")
        st, hd, _ = req(port, "POST", "/login/dogru", L)
        check(st == 429 and hd.get("retry-after", "").isdigit(), f"kilitliyken dogru parola: {st}")
        check(req(port, "POST", "/login/yanlis", M)[0] == 401, "baska IP kilitlendi")
        check(req(port, "POST", "/login/dogru", M)[0] == 200, "dogru parola reddedildi")
        mc = [req(port, "POST", "/login/yanlis", M)[0] for _ in range(10)]
        check(mc == [401] * 10, f"basarili giris sayaci sifirlamadi: {mc}")

        check(p.poll() is None, f"sunucu oldu (rc={p.poll()})")
    finally:
        p.kill()
        p.wait()

    if fails:
        for f in fails:
            print("  - " + f)
        print(f"FAIL: wings_rate_limit_smoke {len(fails)} kontrol kirmizi")
        return 1
    print("PASS: wings_rate_limit_smoke — XFF yok sayiliyor, vekil X-Real-IP; sahte XFF "
          "siniri asamiyor; IP'ler birbirini kilitlemiyor; listen_pool(16) 1500 istekte "
          "tam 1000/500; shared_incr 2000/2000; giris 10/15dk + reset")
    return 0


if __name__ == "__main__":
    sys.exit(main())
