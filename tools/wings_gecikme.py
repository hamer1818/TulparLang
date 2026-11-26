#!/usr/bin/env python3
"""Wings gecikme histogrami — servis modlarini ayni yukte olcer (2026-11-26).

    python3 tools/wings_gecikme.py [--istemci 32] [--istek 200] [--md]

Her mod icin iki yuk:
  duz    : N keep-alive istemci, her biri R kez GET /ping
  kotu   : ayni yuk + 2 YAVAS OKUYUCU (8 MB isteyip okumayan) + 2 SLOWLORIS
           (basligi damlatan) — kotu istemcilerin digerlerine etkisi
Her calisma --tavan saniyeyle sinirli; bitmeyen istek "tamamlanmadi" sayilir
(bloklayan modda kotu yuk = kilit, olcumun SONUCU budur).
Cikti: mod basina p50/p90/p99/maks (ms), RPS, zaman asimi sayisi. Istemci
Python oldugu icin mutlak sayilar istemci giderini icerir; modlar arasi FARK
anlamli olan. Sonuclar docs/mindmap/Wings Serve Modes.md'ye islenir.
"""
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TUL = os.path.join(KOK, "tulpar")
SERVER = r'''
import "wings";
str B = "x";
while (length(B) < 8388608) { B = B + B; }
func ping() { return {"ok": 1}; }
func buyuk() { return text(B); }
get("/ping", "ping");
get("/buyuk", "buyuk");
str mode = env("MODE");
int port = toInt(env("PORT"));
if (mode == "pool") { listen_pool(port, 4); }
else if (mode == "async") { listen_async(port); }
else if (mode == "evented") { listen_evented(port); }
else { listen(port); }
'''
MODLAR = [("listen", "single", 1), ("listen_pool(4)", "pool", 1), ("listen_async", "async", 1),
          ("listen_evented", "evented", 1), ("listen_evented x4", "evented", 4)]


def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def start(binp, mode, workers):
    port = free_port()
    env = dict(os.environ, PORT=str(port), MODE=mode, TULPAR_WINGS_WORKERS=str(workers),
               TULPAR_HTTP_QUIET="1", TULPAR_WINGS_NODOCS="1", TULPAR_WINGS_READ_TIMEOUT_MS="3000",
               DISPLAY="")
    p = subprocess.Popen([binp], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(300):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            time.sleep(0.2)
            return p, port
        except OSError:
            time.sleep(0.05)
    p.kill()
    raise SystemExit("sunucu acilmadi: " + mode)


def istemci(port, n, lat, timeouts, lock, son):
    req = b"GET /ping HTTP/1.1\r\nHost: x\r\n\r\n"
    s = None
    for _ in range(n):
        if time.perf_counter() > son:
            with lock:
                timeouts[0] += 1
            continue
        t0 = time.perf_counter()
        try:
            if s is None:
                s = socket.create_connection(("127.0.0.1", port), timeout=2)
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            s.sendall(req)
            buf = b""
            while b"\r\n\r\n" not in buf:
                c = s.recv(4096)
                if not c:
                    raise OSError("kapandi")
                buf += c
            head, rest = buf.split(b"\r\n\r\n", 1)
            cl = 0
            for ln in head.split(b"\r\n"):
                if ln.lower().startswith(b"content-length:"):
                    cl = int(ln.split(b":")[1])
            while len(rest) < cl:
                rest += s.recv(4096)
            if b"connection: close" in head.lower():
                s.close(); s = None
            with lock:
                lat.append((time.perf_counter() - t0) * 1000)
        except OSError:
            with lock:
                timeouts[0] += 1
            try:
                if s: s.close()
            except OSError:
                pass
            s = None
    if s:
        s.close()


def kotu_istemciler(port, dur):
    socks = []
    for _ in range(2):
        s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        try:
            s.connect(("127.0.0.1", port))
            s.sendall(b"GET /buyuk HTTP/1.1\r\nHost: x\r\n\r\n")
        except OSError:
            pass
        socks.append(s)

    def damla():
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.sendall(b"GET /ping HTTP/1.1\r\nHost: x\r\nX-Y: ")
            socks.append(s)
            while not dur.is_set():
                time.sleep(0.2)
                s.sendall(b"a")
        except OSError:
            pass
    ths = [threading.Thread(target=damla, daemon=True) for _ in range(2)]
    for t in ths: t.start()
    return socks


def yuzdelik(xs, q):
    if not xs:
        return float("nan")
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * (len(xs) - 1) + 0.5))]


def olc(binp, mode, workers, n_ist, n_req, kotu, tavan):
    p, port = start(binp, mode, workers)
    dur = threading.Event()
    try:
        socks = kotu_istemciler(port, dur) if kotu else []
        if kotu:
            time.sleep(0.4)
        lat, to, lock = [], [0], threading.Lock()
        t0 = time.perf_counter()
        ths = [threading.Thread(target=istemci, args=(port, n_req, lat, to, lock, t0 + tavan))
               for _ in range(n_ist)]
        for t in ths: t.start()
        for t in ths: t.join(tavan + 10)
        sure = time.perf_counter() - t0
        dur.set()
        for s in socks:
            try: s.close()
            except OSError: pass
        return {"p50": yuzdelik(lat, .5), "p90": yuzdelik(lat, .9), "p99": yuzdelik(lat, .99),
                "max": max(lat) if lat else float("nan"), "rps": len(lat) / sure, "to": to[0],
                "n": len(lat)}
    finally:
        p.kill(); p.wait(5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--istemci", type=int, default=32)
    ap.add_argument("--istek", type=int, default=200)
    ap.add_argument("--tavan", type=float, default=10.0, help="calisma basina sure tavani (s)")
    a = ap.parse_args()
    d = tempfile.mkdtemp(prefix="wings_gecikme_")
    open(os.path.join(d, "s.tpr"), "w").write(SERVER)
    binp = os.path.join(d, "s")
    r = subprocess.run([TUL, "build", os.path.join(d, "s.tpr"), binp], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr); return 1
    print("| mod | yuk | p50 ms | p90 ms | p99 ms | maks ms | RPS | tamamlanmadi |")
    print("|---|---|---:|---:|---:|---:|---:|---:|")
    for ad, mode, w in MODLAR:
        for kotu in (False, True):
            o = olc(binp, mode, w, a.istemci, a.istek, kotu, a.tavan)
            print("| %s | %s | %.2f | %.2f | %.2f | %.1f | %.0f | %d/%d |" % (
                ad, "kotu" if kotu else "duz", o["p50"], o["p90"], o["p99"], o["max"], o["rps"],
                o["to"], a.istemci * a.istek))
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
