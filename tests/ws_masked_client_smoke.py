#!/usr/bin/env python3
"""Masked-frame WebSocket smoke: a real client-side (masked) text frame.

RFC 6455 requires client->server frames to be MASKED; browsers always mask.
examples/32_wings_ws_frames.tpr covers the unmasked server->client
direction, so this harness covers the other half: it builds a tiny Tulpar
echo server (wings_ws_recv_frame -> wings_ws_send_frame), sends a masked
text frame from Python, and asserts the payload came back unmasked intact.

Manual / not wired into CI (same status as wings_tls_smoke.py).
Run from the repo root:  python3 tests/ws_masked_client_smoke.py
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

PORT = 18772
SERVER_SRC = """
import "wings";
int server_fd = socket_server("127.0.0.1", %d);
if (server_fd < 0) { print("FAIL bind"); exit(1); }
print("hazir");
int client = socket_accept(server_fd);
json f = wings_ws_recv_frame(client);
if (f["ok"] == 1) {
    wings_ws_send_frame(client, 1, "eko:" + toString(f["payload"]));
} else {
    print("recv FAIL: " + toString(f["error"]));
}
socket_close(client);
socket_close(server_fd);
""" % PORT


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    # WINDOWS TEMIZLIK YARISI: Windows, daha yeni sonlanmis bir .exe'nin
    # goruntu tanitici serbest birakilana kadar silinmesine izin vermez, ve
    # bu birakma ASENKRON. Olculdu 2026-09-19: ayni sonda arka arkaya iki kez
    # kosturuldugunda biri `PermissionError: [WinError 5] ...wsecho_bin.exe`
    # ile DUSTU, otekisi gecti — testin OLCTUGU sey (maskeli cerceve eko'su)
    # ikisinde de dogruydu. Temizligin basarisizligi bir test sonucu degildir,
    # o yuzden hukum verdirmiyor. (Python 3.10 oncesinde parametre yok.)
    tmp_kwargs = {}
    if sys.version_info >= (3, 10):
        tmp_kwargs["ignore_cleanup_errors"] = True
    with tempfile.TemporaryDirectory(**tmp_kwargs) as tmp:
        # WINDOWS. Uc ayri nokta; ucu de kardes sonda wings_tls_smoke.py'de
        # zaten dogru yapiliyor, bu dosya geride kalmisti (olculdu 2026-09-19,
        # yerel Windows derlemesi):
        #   1. Derleyicinin adi `tulpar.exe`; `./tulpar` diye aranirsa yok.
        #   2. `tulpar build <kaynak> <ad>` ciktiyi `<ad>.exe` yazar, yani
        #      `os.path.exists("<ad>")` False donuyordu ve sonda
        #      "FAIL: server did not compile" diyordu — oysa bir ustteki satir
        #      "[AOT] Successfully created". Mesaj YANLIS sucluyordu: derleme
        #      degil, ARAMA basarisizdi. Bu, paketlerdeki TEK kirmiziydi.
        #   3. Alt surec ciktisi yerel kod sayfasiyla (cp1254) cozulurse
        #      Turkce tanilar bozulur ya da UnicodeDecodeError atar.
        exe_suffix = ".exe" if sys.platform.startswith("win") else ""
        tulpar = None
        for cand in ("./tulpar" + exe_suffix, "tulpar" + exe_suffix):
            if os.path.exists(cand):
                tulpar = cand
                break
        if tulpar is None:
            print("FAIL: tulpar%s bulunamadi (depo kokunden calistirin)"
                  % exe_suffix)
            return 1

        src = os.path.join(tmp, "wsecho.tpr")
        bin_base = os.path.join(tmp, "wsecho_bin")
        binary = bin_base + exe_suffix
        with open(src, "w", encoding="utf-8") as f:
            f.write(SERVER_SRC)
        build = subprocess.run([tulpar, "build", src, bin_base],
                               capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
        if build.returncode != 0 or not os.path.exists(binary):
            print("FAIL: server did not compile\n" + build.stdout + build.stderr)
            return 1

        server = subprocess.Popen([binary], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        try:
            time.sleep(1.0)  # bind + accept window
            payload = "merhaba ws".encode()
            mask = b"\x11\x22\x33\x44"
            masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            frame = b"\x81" + bytes([0x80 | len(payload)]) + mask + masked

            s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
            s.sendall(frame)
            hdr = s.recv(2)
            length = hdr[1] & 0x7F
            data = s.recv(length)
            s.close()

            opcode, fin = hdr[0] & 0x0F, hdr[0] >> 7
            expected = b"eko:" + payload
            if opcode == 1 and fin == 1 and data == expected:
                print("PASS masked client frame unmasked + echoed (%r)" % data.decode())
                return 0
            print("FAIL opcode=%d fin=%d payload=%r (expected %r)"
                  % (opcode, fin, data, expected))
            return 1
        finally:
            server.kill()
            server.wait()
            # Boru taniticisini ACIKCA birak; yoksa .exe'yi tutan ikinci bir
            # tanitici kaliyor ve yukaridaki temizlik yarisi buyuyor.
            if server.stdout is not None:
                server.stdout.close()


if __name__ == "__main__":
    sys.exit(main())
