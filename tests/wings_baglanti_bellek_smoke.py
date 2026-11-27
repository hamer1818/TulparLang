"""Baglanti basina bellek (2026-11-27): yeni baglantilar sunucuyu buyutmuyor.

NEDEN. `listen_async` her baglanti icin bir thread aciyor (thread_create +
thread_detach). Iki sizinti olculdu, ikisi de thread bitince birakilmayan
thread-basina bellek:
  * iscinin dizgi arenasi (ilk dizgide 1 MB'lik blok) — runtime_bindings.cpp
    "ISCI ARENASI" (thread bitince kesin olu bloklar birakiliyor);
  * aot_http_recv_request'in 64 KB'lik `thread_local` baslik tamponu.
Olculen (Linux x86_64, 3000 yeni baglanti, Connection: close):
  duzeltmeden once  heap +3 277 580 KB, RSS +39 484 KB
  arena duzeltmesi  heap   +192 298 KB, RSS +12 380 KB
  ikisi birden      heap       +187 KB, RSS    +220 KB
Soak testleri (srv_int, listen() 500k+) bunu gormuyordu: tek baglanti
uzerinde keep-alive ile cok istek gonderiyorlar, yeni baglanti acmiyorlar.

NE OLCULUYOR. Dort modun her birinde (listen, listen_pool(2), listen_async,
listen_evented): 200 baglantilik isinma, sonra N yeni baglanti; sunucunun
kendi `mem_stats()["heap_in_use"]` artisi esigin altinda olmali. Ardindan
Bellek RFC Kural 7.1/7.2: N/2 istek gecici deger kurup `throw` eden bir
handler'a gider — hepsi 500 donmeli, sonra /ping 200, heap yine duz (istek
noktasi try'in disinda dusuruluyor; elle dogrulanmisti, testi yoktu). Esik genis
(malloc ici dalgalanma) ama eski sizintiyi ~500 kat asagida yakalar.
Kontrol (elle, 2026-11-27): duzeltmesiz runtime ile async modu KIRMIZI
(2000 baglantida heap +2 185 409 KB). Ikinci sabotaj: listen()'in istek
basina arena_restore'u silinince single KIRMIZI (2000 baglanti +6 175 KB,
1000 throw +4 503 KB; normalde +0 / +1 KB). Tuzak: kontrolu depo kokunden kosma —
bagliyici calisma dizinindeki ./libtulpar_runtime.a'yi aliyor, eski derleyici
yeni arsivle baglanir ve kontrol bos yere yesil verir (ilk denemede oldu).

heap_in_use bu platformda -1 ise (Windows) SKIP.
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import tempfile
import time

N = 2000
ESIK_KB = 2048        # N yeni baglanti (normal: en cok ~+125 KB, async)
ESIK_THROW_KB = 1024  # N/2 throw (normal: en cok ~+62 KB)


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

func ping(json req) {
    return text("pong-" + toString(len(req["path"])));
}
func mem(json req) {
    return text(toString(mem_stats()["heap_in_use"]));
}
// Bellek RFC Kural 7.1/7.2: handler'da throw -> 500, sunucu devam eder.
// Atmadan once gecici json/dizgi kurar: istek noktasi try'in DISINDA
// dusurulmeli, yani bunlar sizmamali.
func patla(json req) {
    json t = {"a": "gecici-" + toString(len(req["path"])), "l": [1, 2, 3]};
    str s = "dizgi-" + t["a"];
    throw "patladi: " + s;
}

get("/ping", "ping");
get("/mem", "mem");
get("/patla", "patla");
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


def get(port, path, timeout=5.0, status=200):
    """Yeni baglanti + GET + kapanis. Durum `status` ise govde (str), yoksa None."""
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        s.sendall(("GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" % path).encode())
        data = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
        s.close()
    except OSError:
        return None
    if not data.startswith(b"HTTP/1.1 %d" % status):
        return None
    return data.split(b"\r\n\r\n", 1)[-1].decode(errors="replace")


def heap(port):
    b = get(port, "/mem")
    try:
        return int(b.strip()) if b is not None else None
    except ValueError:
        return None


def rss_kb(pid):
    try:
        with open("/proc/%d/status" % pid) as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return None


def olc(binp, mode):
    port = free_port()
    env = dict(os.environ, SMOKE_PORT=str(port), MODE=mode, TULPAR_HTTP_QUIET="1",
               TULPAR_WINGS_NODOCS="1", DISPLAY="")
    p = subprocess.Popen([binp], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(200):
            if get(port, "/ping", 1) is not None:
                break
            time.sleep(0.05)
        else:
            return None, "sunucu acilmadi"
        for _ in range(200):
            get(port, "/ping")
        time.sleep(0.3)  # ayrik thread'ler bitsin
        h0 = heap(port)
        if h0 is None:
            return None, "/mem okunamadi"
        if h0 < 0:
            return "skip", None
        r0 = rss_kb(p.pid)
        kayip = 0
        for _ in range(N):
            if get(port, "/ping") is None:
                kayip += 1
        time.sleep(0.5)
        h1 = heap(port)
        r1 = rss_kb(p.pid)
        if h1 is None:
            return None, "/mem okunamadi (sonra)"
        rss = "" if r0 is None or r1 is None else ", RSS %+d KB" % (r1 - r0)
        # Kural 7.1/7.2: N/2 atan istek -> hepsi 500, ardindan /ping 200,
        # heap duz (atan handler'in gecici degerleri noktayla gider).
        h2 = heap(port)
        bes_yuz_degil = 0
        for _ in range(N // 2):
            if get(port, "/patla", status=500) is None:
                bes_yuz_degil += 1
        sonra_ping = get(port, "/ping") is not None
        time.sleep(0.3)
        h3 = heap(port)
        if h2 is None or h3 is None:
            return None, "/mem okunamadi (throw sonrasi)"
        return ((h1 - h0) // 1024, kayip, rss, (h3 - h2) // 1024, bes_yuz_degil, sonra_ping), None
    finally:
        p.kill()
        p.wait(5)


def main() -> int:
    exe = find_tulpar_exe()
    d = tempfile.mkdtemp(prefix="tulpar_baglanti_bellek_")
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
    for mode in ["single", "pool", "async", "evented"]:
        res, err = olc(binp, mode)
        if res == "skip":
            print("SKIP: wings_baglanti_bellek_smoke — heap_in_use bu platformda yok")
            return 0
        if err:
            fails.append("%s: %s" % (mode, err))
            continue
        kb, kayip, rss, tkb, bes_yuz_degil, sonra_ping = res
        line = "%s: %d yeni baglanti heap %+d KB%s; %d throw heap %+d KB" % (
            mode, N, kb, rss, N // 2, tkb)
        print("  " + line)
        notes.append("%s %+d/%+d KB" % (mode, kb, tkb))
        if kayip:
            fails.append("%s: %d istek cevapsiz" % (mode, kayip))
        if kb >= ESIK_KB:
            fails.append("%s (esik %d KB)" % (line, ESIK_KB))
        if bes_yuz_degil:
            fails.append("%s: atan handler'in %d istegi 500 donmedi (Kural 7.2)" % (mode, bes_yuz_degil))
        if not sonra_ping:
            fails.append("%s: throw'lardan sonra /ping 200 degil (sunucu devam etmeli)" % mode)
        if tkb >= ESIK_THROW_KB:
            fails.append("%s: %d throw heap'i %+d KB buyuttu (Kural 7.1, esik %d KB)"
                         % (mode, N // 2, tkb, ESIK_THROW_KB))

    if fails:
        for f in fails:
            print("  DUSTU " + f)
        print("FAIL: wings_baglanti_bellek_smoke")
        return 1
    print("PASS: wings_baglanti_bellek_smoke — %d yeni baglanti + %d throw->500 "
          "(sonra 200) heap duz (%s)" % (N, N // 2, ", ".join(notes)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
