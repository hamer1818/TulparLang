"""Slowloris: yavas istemciler sunucuyu kilitlemiyor (wings read_timeout,
2026-11-26). Gercek surec, gercek soketler, dort sunucu modu.

Saldiri: istemci baslik satirini yarim gonderir, sonra 200 ms'de bir TEK bayt
yollar — baslik hic bitmez, ama baglanti hic susmaz (recv basina zaman asimi
bunu yakalayamaz; toplam sure gerekir).

Olculenler (her mod: listen, listen_pool(2), listen_async, listen_evented;
read_timeout = T = 1500 ms):
  1. Yavas istemciler varken normal bir GET /ping gecer:
     evented'de hemen (< 0,8 s — tek iplik artik okurken bloklamiyor),
     digerlerinde en gec T + pay icinde (pool'da 2 isci = 2 yavas istemci).
  2. Yavas istemci T + pay icinde 408 alir ve baglantisi kapanir.
  3. Ilerleyen yavas govde KESILMEZ: 6 bayt, 400 ms arayla (toplam 2,4 s > T)
     -> 200. Govde kurali "recv arasi sure"dir, toplam degil.
  4. Hic bayt gondermeyen bosta baglanti (listen/pool/async) T + pay icinde
     sessizce kapanir — 408 yazilmaz.

Kontrol kosulari (kapinin bir sey olctugunu gosterir):
  K1. Pool + TULPAR_WINGS_READ_TIMEOUT_MS=0 (eski davranis): 2 yavas istemci
      varken normal istek 3 s icinde GECEMEMELI. Gecerse saldiri kurgusu
      kilidi uretmiyor demektir ve 1. olcum hicbir sey olcmuyordur.
  K2. Evented + sure 0: normal istek yine hemen gecer (bloklamayan okuma
      sure ayarindan bagimsiz); yavas istemci kapatilmaz.
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

T_MS = 1500
SLACK = 2.0   # s — CI kosuculari heterojen; kilit "sonsuz", pay rahat olabilir


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["./tulpar", "./tulpar.exe", "tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("tulpar ikilisi yok (once ./build.sh, ya da TULPAR_EXE)")


SERVER = r'''
import "wings";

func ping() {
    return {"ok": 1};
}

get("/ping", "ping");
post("/up", "ping");
str mode = env("MODE");
int port = toInt(env("SMOKE_PORT"));
if (mode == "pool") {
    listen_pool(port, 2);
} else if (mode == "evented") {
    listen_evented(port);
} else if (mode == "async") {
    listen_async(port);
} else {
    listen(port);
}
'''


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def raw_get(port, timeout):
    """GET /ping; (status, gecen_sure) ya da (None, sure) zaman asiminda."""
    t0 = time.monotonic()
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        s.settimeout(max(0.05, timeout - (time.monotonic() - t0)))
        s.sendall(b"GET /ping HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        data = b""
        while b"\r\n" not in data:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
        s.close()
        if data.startswith(b"HTTP/1.1 "):
            return int(data[9:12]), time.monotonic() - t0
        return None, time.monotonic() - t0
    except OSError:
        return None, time.monotonic() - t0


class SlowClient:
    """Yarim baslik + 200 ms'de bir bayt. Sunucunun cevabini/kapatmasini kaydeder."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.sendall(b"GET /ping HTTP/1.1\r\nHost: x\r\nX-Slow: ")
        self.t0 = time.monotonic()
        self.reply = b""
        self.closed_at = None
        self.stop = threading.Event()
        self.th = threading.Thread(target=self._run, daemon=True)
        self.th.start()

    def _run(self):
        self.sock.settimeout(0.2)
        while not self.stop.is_set():
            try:
                chunk = self.sock.recv(4096)
                if not chunk:
                    self.closed_at = time.monotonic() - self.t0
                    return
                self.reply += chunk
            except socket.timeout:
                pass
            except OSError:
                self.closed_at = time.monotonic() - self.t0
                return
            try:
                self.sock.sendall(b"a")
            except OSError:
                self.closed_at = time.monotonic() - self.t0
                return

    def wait(self, limit):
        self.th.join(limit)

    def close(self):
        self.stop.set()
        self.th.join(1)
        try:
            self.sock.close()
        except OSError:
            pass


def slow_body(port, nbytes, gap):
    """POST /up, govde bayt bayt `gap` arayla. Donen durum kodu ya da None."""
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=10)
        s.sendall(b"POST /up HTTP/1.1\r\nHost: x\r\nConnection: close\r\n"
                  b"Content-Type: text/plain\r\nContent-Length: %d\r\n\r\n" % nbytes)
        for _ in range(nbytes):
            time.sleep(gap)
            s.sendall(b"b")
        s.settimeout(10)
        data = s.recv(4096)
        s.close()
        if data.startswith(b"HTTP/1.1 "):
            return int(data[9:12])
    except OSError:
        pass
    return None


def idle_close_after(port, limit):
    """Hic bayt gondermeyen baglanti; (kapanma suresi, gelen veri)."""
    s = socket.create_connection(("127.0.0.1", port), timeout=limit)
    t0 = time.monotonic()
    data = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except OSError:
        return None, data
    finally:
        s.close()
    return time.monotonic() - t0, data


def start(binp, mode, read_to):
    port = free_port()
    env = dict(os.environ, SMOKE_PORT=str(port), MODE=mode,
               TULPAR_WINGS_READ_TIMEOUT_MS=str(read_to),
               TULPAR_HTTP_QUIET="1", TULPAR_WINGS_NODOCS="1", DISPLAY="")
    p = subprocess.Popen([binp], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(200):
        st, _ = raw_get(port, 1)
        if st == 200:
            return p, port
        time.sleep(0.05)
    p.kill()
    raise SystemExit("FAIL: sunucu acilmadi (" + mode + ")")


def stop(p):
    p.kill()
    p.wait(5)


def main() -> int:
    exe = find_tulpar_exe()
    d = tempfile.mkdtemp(prefix="tulpar_slowloris_")
    src = os.path.join(d, "srv.tpr")
    with open(src, "w") as fh:
        fh.write(SERVER)
    binp = os.path.join(d, "srv")
    r = subprocess.run([exe, "build", src, binp], capture_output=True, text=True,
                       env=dict(os.environ, DISPLAY=""))
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        print("FAIL: sunucu derlenmedi")
        return 1
    if not os.path.exists(binp) and os.path.exists(binp + ".exe"):
        binp += ".exe"

    fails = []
    notes = []
    T = T_MS / 1000.0

    def check(cond, why):
        if not cond:
            fails.append(why)

    # mod -> kac yavas istemci (pool = isci sayisi; evented'de tek iplik)
    plan = [("single", 1), ("pool", 2), ("async", 2), ("evented", 4)]
    for mode, nslow in plan:
        p, port = start(binp, mode, T_MS)
        try:
            slows = [SlowClient(port) for _ in range(nslow)]
            time.sleep(0.3)   # yavaslar sunucuda oturmus olsun
            st, dt = raw_get(port, T + SLACK + 1)
            if mode == "evented":
                check(st == 200 and dt < 0.8,
                      "%s: yavas istemci varken /ping hemen gecmedi (durum %s, %.2f s)" % (mode, st, dt))
            else:
                check(st == 200 and dt < T + SLACK,
                      "%s: yavas istemci varken /ping %.1f s icinde gecmedi (durum %s, %.2f s)"
                      % (mode, T + SLACK, st, dt))
            notes.append("%s /ping %.2fs" % (mode, dt))
            for c in slows:
                c.wait(T + SLACK + 1)
                check(c.closed_at is not None and c.closed_at < T + SLACK + 0.5,
                      "%s: yavas istemci zamaninda kapatilmadi (%s)" % (mode, c.closed_at))
                check(c.reply.startswith(b"HTTP/1.1 408"),
                      "%s: yavas istemciye 408 yazilmadi (%r)" % (mode, c.reply[:40]))
                c.close()

            st = slow_body(port, 6, 0.4)
            check(st == 200, "%s: ilerleyen yavas govde kesildi (durum %s)" % (mode, st))

            if mode != "evented":
                closed, data = idle_close_after(port, T + SLACK + 1)
                check(closed is not None and closed < T + SLACK,
                      "%s: bosta baglanti zamaninda kapanmadi (%s)" % (mode, closed))
                check(data == b"", "%s: bosta baglantiya yanit yazildi (%r)" % (mode, data[:40]))
            st, _ = raw_get(port, 3)
            check(st == 200, "%s: saldiridan sonra sunucu cevap vermiyor (%s)" % (mode, st))
        finally:
            stop(p)

    # K1: sure kapali -> pool kilitlenmeli (kurgu kilidi gercekten uretiyor mu?)
    p, port = start(binp, "pool", 0)
    try:
        slows = [SlowClient(port) for _ in range(2)]
        time.sleep(0.3)
        st, dt = raw_get(port, 3)
        check(st is None, "K1: sure KAPALIYKEN pool 2 yavas istemciye ragmen cevap verdi "
                          "(%s, %.2f s) — saldiri kurgusu kilidi uretmiyor, olcum bos" % (st, dt))
        for c in slows:
            c.close()
    finally:
        stop(p)

    # K2: sure kapali, evented yine de bloklamiyor
    p, port = start(binp, "evented", 0)
    try:
        slows = [SlowClient(port) for _ in range(4)]
        time.sleep(0.3)
        st, dt = raw_get(port, 3)
        check(st == 200 and dt < 0.8, "K2: sure kapali evented /ping gecmedi (%s, %.2f s)" % (st, dt))
        time.sleep(T + 0.5)
        check(all(c.closed_at is None for c in slows),
              "K2: sure kapaliyken evented yavas istemciyi kapatti")
        for c in slows:
            c.close()
    finally:
        stop(p)

    if fails:
        for f in fails:
            print("  - " + f)
        print("FAIL: wings_slowloris_smoke (%d)" % len(fails))
        return 1
    print("PASS: wings_slowloris_smoke — 4 mod: yavas istemci 408 + kapanis, digerleri gecer "
          "(%s); yavas govde kesilmez; kontrol: sure 0 iken pool kilitli" % ", ".join(notes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
