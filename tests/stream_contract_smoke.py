#!/usr/bin/env python3
"""S4 — uc fazli akis sozlesmesinin fiksturu (SSE + WS).

Sozlesme (FINDINGS, P44/P46):
    akis BASLAMADAN throw  -> 500 + JSON zarfi
    akis ORTASINDA throw   -> zarf YAZILMAZ (yalniz log), baglanti kapanir
    her iki fazda          -> surec yasar

Neden ham soket, `curl` degil (kural #22 — tani araci da tanini parcasi):
P48'de `curl` TEK yanit gordu, ham soket IKI yanit gordu. Araç protokolu
"sizin adiniza" yorumlayip `Content-Length`te okumayi birakinca ihlal tam o
sinirin ardinda gizlendi. Ihlal ARANIRKEN olcum ham katmanda olmali.

Kirmiziya donebilirlik (#10): `/raw` rotasi KASITLI sozlesme ihlalidir
(sokete ham yazar, `{"_stream":1}` dondurmez). Zarf sayan olcum /sse ve
/ws'de 1, /raw'da 2 vermek ZORUNDA. /raw da 1 verirse dedektor kordur ve
digerlerinin yesili anlamsizdir — bu yuzden /raw bir test degil, sondanin
KENDI kanitidir.
"""
import os, socket, subprocess, sys, tempfile, time

PORT = 18781
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "stream_contract_server.tpr")

RED, GREEN, RESET = "\033[0;31m", "\033[0;32m", "\033[0m"
fails = []


def talk(req_bytes, settle=0.6):
    """Ham istek gonder, baglanti susana kadar HER BAYTI topla."""
    s = socket.create_connection(("127.0.0.1", PORT), 5)
    s.sendall(req_bytes)
    s.settimeout(settle)
    buf = b""
    try:
        while True:
            d = s.recv(65536)
            if not d:
                break
            buf += d
    except socket.timeout:
        pass
    finally:
        s.close()
    return buf


def get(path, extra=b"", keep=True):
    conn = b"" if keep else b"Connection: close\r\n"
    return talk(b"GET " + path + b" HTTP/1.1\r\nHost: x\r\n" + conn + extra + b"\r\n")


def check(name, cond, detail):
    tag = GREEN + "PASS" + RESET if cond else RED + "FAIL" + RESET
    print("  %s %-38s %s" % (tag, name, detail))
    if not cond:
        fails.append(name)
    return cond


def envelopes(buf):
    """Kac HTTP yanit zarfi var — sozlesmenin tek sayisal olcusu."""
    return buf.count(b"HTTP/1.1 ")


def main():
    tmp = tempfile.mkdtemp(prefix="s4_")
    binary = os.path.join(tmp, "s4srv")
    env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="")
    build = subprocess.run([os.path.join(ROOT, "tulpar"), "build", FIXTURE, binary],
                           capture_output=True, env=env, cwd=ROOT)
    if build.returncode != 0:
        print(RED + "FAIL" + RESET + " fikstur derlenmedi")
        print(build.stdout.decode("utf-8", "replace")[-2000:])
        return 1

    log = open(os.path.join(tmp, "srv.log"), "wb")
    srv = subprocess.Popen([binary], stdout=log, stderr=subprocess.STDOUT, env=env)
    try:
        for _ in range(120):
            try:
                socket.create_connection(("127.0.0.1", PORT), 0.2).close()
                break
            except OSError:
                time.sleep(0.05)
        else:
            print(RED + "FAIL" + RESET + " sunucu acilmadi")
            return 1

        print("S4 — uc fazli akis sozlesmesi")

        # --- Faz 1: akis baslamadan throw -> 500 + JSON, TEK zarf
        b = get(b"/pre")
        check("faz1: 500 dondu", b.startswith(b"HTTP/1.1 500"), b.split(b"\r\n")[0].decode("latin1"))
        check("faz1: JSON govde", b"error" in b, repr(b[-60:]))
        check("faz1: tek zarf", envelopes(b) == 1, "zarf=%d" % envelopes(b))

        # --- Faz 2a: SSE akis ortasi throw -> zarf YAZILMAZ
        b = get(b"/sse")
        check("faz2-sse: 200 akis basligi", b.startswith(b"HTTP/1.1 200"), b.split(b"\r\n")[0].decode("latin1"))
        check("faz2-sse: event-stream", b"text/event-stream" in b, "")
        check("faz2-sse: olay ulasti", b"data: " in b, "")
        check("faz2-sse: ZARF YAZILMADI", envelopes(b) == 1, "zarf=%d (500 gomulmus olsaydi 2)" % envelopes(b))
        check("faz2-sse: govdede 500 yok", b"500 Internal" not in b, "")

        # --- Faz 2b: WS akis ortasi throw -> cerceve akisina HTTP enjekte edilmez
        b = get(b"/ws", b"Upgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n")
        check("faz2-ws: 101 yukseltme", b.startswith(b"HTTP/1.1 101"), b.split(b"\r\n")[0].decode("latin1"))
        check("faz2-ws: accept-key dogru", b"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in b, "")
        check("faz2-ws: cerceve ulasti", b"\x81\x03ilk" in b, repr(b[-12:]))
        check("faz2-ws: HTTP ENJEKTE EDILMEDI", envelopes(b) == 1, "zarf=%d" % envelopes(b))
        tail = b.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in b else b
        check("faz2-ws: cerceve kuyrugu temiz", tail == b"\x81\x03ilk", repr(tail))

        # --- WS 400 dali: akis HIC baslamadi -> normal yol korunur
        b = get(b"/ws", b"Upgrade: websocket\r\n")
        check("ws-400: anahtarsiz 400", b.startswith(b"HTTP/1.1 400"), b.split(b"\r\n")[0].decode("latin1"))

        # --- ENJEKSIYON: sozlesme disi ham yazan handler -> IKI zarf
        b = get(b"/raw")
        n = envelopes(b)
        check("ENJEKSIYON /raw: iki zarf gorundu", n == 2,
              "zarf=%d %s" % (n, "" if n == 2 else "<- DEDEKTOR KOR: digerlerinin yesili anlamsiz"))

        # --- Faz 3: surec yasiyor
        b = get(b"/healthz", keep=False)
        check("faz3: surec yasiyor", b.startswith(b"HTTP/1.1 200") and b"alive" in b,
              b.split(b"\r\n")[0].decode("latin1"))
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()

    if fails:
        print(RED + "S4 sozlesmesi IHLAL: %d kontrol dustu" % len(fails) + RESET + " (%s)" % ", ".join(fails))
        return 1
    print(GREEN + "S4 kilitli" + RESET + " — uc faz + WS 400 dali + enjeksiyon kirmizi verebiliyor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
