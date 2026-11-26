#!/usr/bin/env python3
"""listen_evented YAZMA YOLU ve COK CEKIRDEK (2026-11-26) — gercek surec, gercek soket.

Slowloris'in yazma ikizi: yanitini OKUMAYAN tek istemci (buyuk yanit, kucuk
alma tamponu) bloklu `socket_send`'de olay dongusunu durduruyordu. Simdi yanit
`http_send_queued` ile bloklamadan yaziliyor, kalan `http_poll`da bosaltiliyor.

OLCULEN
  1. Yavas okuyucu 8 MB isteyip okumuyorken baska istemcinin /ping'i evented'de
     hemen gecer (< 0.8 s).
     KONTROL K1: ayni kurgu bloklu yazan `listen()` modunda /ping'i 3 s icinde
     GECIREMEMELI — gecerse kurgu kilidi uretmiyor ve 1. olcum bos.
  2. Yavas okuyucu sonra okudugunda 8 MB'in TAMAMI ve dogru geliyor, ardindan
     sunucu baglantiyi kapatiyor (Connection: close -> bosalinca kapat).
  3. Pipelining: ayni baglantida once /buyuk sonra /ping — yanitlar SIRAYLA.
  4. Cok cekirdek: TULPAR_WINGS_WORKERS=4 ile CPU yogun 4 eszamanli istek,
     tek istegin suresinin < 2.5 katinda biter (paralel).
     KONTROL K2: 1 isci ile ayni 4 istek > 3 kat surer (seri) — olcum paralellik
     goruyor mu. (4'ten az cekirdekte 4. adim GORUNUR atlanir.)
  5. Cok iscide 32 baglanti x 10 istek: hepsi 200.
"""
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TUL = os.path.join(KOK, "tulpar")
BUYUK = 8 * 1024 * 1024

SERVER = r'''
import "wings";
str BUYUK_METIN = "x";
while (length(BUYUK_METIN) < 8388608) { BUYUK_METIN = BUYUK_METIN + BUYUK_METIN; }
func ping() { return {"ok": 1}; }
func buyuk() { return text(BUYUK_METIN); }
func is_yap() {
    int t = 0;
    for (int i = 0; i < 40000000; i++) { t = (t + i * 7) % 1000003; }
    return {"t": t};
}
get("/ping", "ping");
get("/buyuk", "buyuk");
get("/is", "is_yap");
if (env("MODE") == "evented") { listen_evented(toInt(env("SMOKE_PORT"))); }
else { listen(toInt(env("SMOKE_PORT"))); }
'''
fails = []


def check(ad, kosul, detay=""):
    print("  %s  %s %s" % ("gecti" if kosul else "DUSTU", ad, detay))
    if not kosul:
        fails.append(ad)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def get(port, path, timeout):
    t0 = time.monotonic()
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        s.settimeout(timeout)
        s.sendall(b"GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" % path.encode())
        data = b""
        while True:
            c = s.recv(65536)
            if not c:
                break
            data += c
        s.close()
        st = int(data[9:12]) if data.startswith(b"HTTP/1.1 ") else None
        return st, time.monotonic() - t0, data
    except OSError:
        return None, time.monotonic() - t0, b""


def start(binp, mode, workers=1):
    port = free_port()
    env = dict(os.environ, SMOKE_PORT=str(port), MODE=mode, TULPAR_WINGS_WORKERS=str(workers),
               TULPAR_HTTP_QUIET="1", TULPAR_WINGS_NODOCS="1", DISPLAY="")
    p = subprocess.Popen([binp], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(300):
        st, _, _ = get(port, "/ping", 1)
        if st == 200:
            return p, port
        time.sleep(0.05)
    p.kill()
    raise SystemExit("FAIL: sunucu acilmadi (%s)" % mode)


def slow_reader(port, keep=False):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    s.connect(("127.0.0.1", port))
    s.sendall(b"GET /buyuk HTTP/1.1\r\nHost: x\r\n%s\r\n" % (b"" if keep else b"Connection: close\r\n"))
    return s


def read_response(s, timeout=20):
    """Bir HTTP yaniti: (durum, govde). Content-Length'e gore."""
    s.settimeout(timeout)
    buf = b""
    while b"\r\n\r\n" not in buf:
        c = s.recv(65536)
        if not c:
            return None, b"", buf
        buf += c
    head, rest = buf.split(b"\r\n\r\n", 1)
    n = 0
    for ln in head.split(b"\r\n"):
        if ln.lower().startswith(b"content-length:"):
            n = int(ln.split(b":")[1])
    while len(rest) < n:
        c = s.recv(1 << 20)
        if not c:
            break
        rest += c
    return int(head[9:12]), rest[:n], rest[n:]


def main():
    d = tempfile.mkdtemp(prefix="wings_yazma_")
    src = os.path.join(d, "srv.tpr")
    open(src, "w").write(SERVER)
    binp = os.path.join(d, "srv")
    r = subprocess.run([TUL, "build", src, binp], capture_output=True, text=True,
                       env=dict(os.environ, DISPLAY=""))
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        print("FAIL: sunucu derlenmedi")
        return 1

    # 1-2) yavas okuyucu, evented
    p, port = start(binp, "evented")
    try:
        sr = slow_reader(port)
        time.sleep(0.5)
        st, dt, _ = get(port, "/ping", 5)
        check("evented: yavas okuyucu varken /ping hemen", st == 200 and dt < 0.8, "(%s, %.2f s)" % (st, dt))
        st2, body, _ = read_response(sr)
        ok_body = st2 == 200 and len(body) == BUYUK and body.count(b"x") == BUYUK
        check("kuyruktaki 8 MB eksiksiz teslim", ok_body, "(%s, %d bayt)" % (st2, len(body)))
        sr.settimeout(5)
        try:
            kapandi = sr.recv(1) == b""
        except OSError:
            kapandi = True
        check("bosalinca sunucu baglantiyi kapatti (Connection: close)", kapandi)
        sr.close()
        # 3) pipelining
        s = slow_reader(port, keep=True)
        s.sendall(b"GET /ping HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        a_st, a_body, rest = read_response(s)
        # ikinci yanitin bir kismi `rest`te olabilir
        s.settimeout(5)
        tail = rest
        while True:
            try:
                c = s.recv(65536)
            except OSError:
                break
            if not c:
                break
            tail += c
        s.close()
        check("pipelining: once 8 MB, sonra /ping, sirayla",
              a_st == 200 and len(a_body) == BUYUK and tail.startswith(b"HTTP/1.1 200") and b'{"ok":1}' in tail)
    finally:
        p.kill(); p.wait(5)

    # K1) kontrol: bloklu yazan listen()
    p, port = start(binp, "single")
    try:
        sr = slow_reader(port)
        time.sleep(0.5)
        st, dt, _ = get(port, "/ping", 3)
        check("KONTROL K1: bloklu listen() yavas okuyucuyla KILITLI", st is None, "(%s, %.2f s)" % (st, dt))
        sr.close()
    finally:
        p.kill(); p.wait(5)

    # 4-5) cok cekirdek
    cores = os.cpu_count() or 1

    def paralel_sure(port, n):
        res = []
        ths = [threading.Thread(target=lambda: res.append(get(port, "/is", 60))) for _ in range(n)]
        t0 = time.monotonic()
        for t in ths: t.start()
        for t in ths: t.join()
        return time.monotonic() - t0, [x[0] for x in res]

    if cores >= 4:
        sonuc = {}
        for w in (1, 4):
            p, port = start(binp, "evented", w)
            try:
                tek, _ = paralel_sure(port, 1)
                dort, sts = paralel_sure(port, 4)
                sonuc[w] = (tek, dort, sts)
                if w == 4:
                    ok = 0
                    lock = threading.Lock()

                    def burst():
                        nonlocal ok
                        for _ in range(10):
                            st, _, _ = get(port, "/ping", 10)
                            if st == 200:
                                with lock:
                                    ok += 1
                    ths = [threading.Thread(target=burst) for _ in range(32)]
                    for t in ths: t.start()
                    for t in ths: t.join()
                    check("4 iscide 32 baglanti x 10 istek hepsi 200", ok == 320, "(%d/320)" % ok)
            finally:
                p.kill(); p.wait(5)
        t1, t4, sts = sonuc[4]
        check("4 isci: 4 CPU yogun istek paralel (< 2.5x)", all(x == 200 for x in sts) and t4 < 2.5 * t1,
              "(tek %.2f s, dort %.2f s, %.1fx)" % (t1, t4, t4 / t1))
        k1, k4, _ = sonuc[1]
        check("KONTROL K2: 1 isci ayni yukte seri (> 3x)", k4 > 3.0 * k1,
              "(tek %.2f s, dort %.2f s, %.1fx)" % (k1, k4, k4 / k1))
    else:
        print("  ATLANDI cok cekirdek olcumu (%d cekirdek < 4)" % cores)

    if fails:
        print("FAIL: wings_yazma_smoke — %d dustu" % len(fails))
        return 1
    print("PASS: wings_yazma_smoke — evented yavas okuyucuyla bloklamiyor (kontrol: listen kilitli), "
          "8 MB eksiksiz + kapanis, pipelining sirasi, 4 isci paralel (kontrol: 1 isci seri)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
