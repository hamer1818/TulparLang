#!/usr/bin/env python3
"""Masked-frame WebSocket smoke: real client-side (masked) frames, SEVERAL of
them on ONE connection.

RFC 6455 requires client->server frames to be MASKED; browsers always mask.
examples/32_wings_ws_frames.tpr covers the unmasked server->client
direction, so this harness covers the other half: it builds a tiny Tulpar
echo server (wings_ws_recv_frame -> wings_ws_send_frame in a loop), sends
masked text frames from Python, and asserts every payload comes back
unmasked and intact, in order.

MULTI-FRAME (K270, 2026-09-27). Until then the smoke sent ONE frame, so the
long-open "consecutive recv on MinGW shifts the fd / garbles the second
frame" report (HANDOFF.md, STATUS.md) could not be measured anywhere. Now
one connection carries:
  1. a short frame (7-bit length),
  2. a 200-byte frame (16-bit extended length, both directions),
  3. two frames COALESCED into a single sendall() — the second must still be
     read as its own frame, not lost or merged (buffering / fd drift),
  4. a final short frame after the coalesced pair.
Linux measured green (2026-09-27). Windows CI runs this harness in a
dedicated step (build-windows, "WebSocket cok cerceve") because the Python
audits are otherwise skipped there.

Run from the repo root:  python3 tests/ws_masked_client_smoke.py [tulpar]
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

PORT = 18772
FRAMES = 5  # server echoes exactly this many frames, then closes
SERVER_SRC = """
import "wings";
int server_fd = socket_server("127.0.0.1", %d);
if (server_fd < 0) { print("FAIL bind"); exit(1); }
print("hazir");
int client = socket_accept(server_fd);
int i = 0;
while (i < %d) {
    json f = wings_ws_recv_frame(client);
    if (f["ok"] == 1) {
        wings_ws_send_frame(client, 1, "eko" + toString(i) + ":" + toString(f["payload"]));
    } else {
        print("recv FAIL " + toString(i) + ": " + toString(f["error"]));
    }
    i = i + 1;
}
socket_close(client);
socket_close(server_fd);
""" % (PORT, FRAMES)


def masked_frame(payload: bytes, mask: bytes) -> bytes:
    n = len(payload)
    if n < 126:
        hdr = b"\x81" + bytes([0x80 | n])
    else:
        hdr = b"\x81" + bytes([0x80 | 126]) + n.to_bytes(2, "big")
    return hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


def recv_exact(s: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("connection closed after %d/%d bytes" % (len(buf), n))
        buf += chunk
    return buf


def recv_frame(s: socket.socket):
    hdr = recv_exact(s, 2)
    fin, opcode = hdr[0] >> 7, hdr[0] & 0x0F
    n = hdr[1] & 0x7F
    if n == 126:
        n = int.from_bytes(recv_exact(s, 2), "big")
    elif n == 127:
        n = int.from_bytes(recv_exact(s, 8), "big")
    return fin, opcode, recv_exact(s, n)


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    tulpar = sys.argv[1] if len(sys.argv) > 1 else "./tulpar"
    if not os.path.exists(tulpar) and os.path.exists(tulpar + ".exe"):
        tulpar += ".exe"
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "wsecho.tpr")
        binary = os.path.join(tmp, "wsecho_bin")
        with open(src, "w") as f:
            f.write(SERVER_SRC)
        build = subprocess.run([tulpar, "build", src, binary],
                               capture_output=True, text=True)
        if not os.path.exists(binary) and os.path.exists(binary + ".exe"):
            binary += ".exe"
        if build.returncode != 0 or not os.path.exists(binary):
            print("FAIL: server did not compile\n" + build.stdout + build.stderr)
            return 1

        server = subprocess.Popen([binary], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        got = []
        try:
            time.sleep(1.0)  # bind + accept window
            s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
            p0 = b"merhaba ws"
            p1 = bytes(ord("a") + (i % 26) for i in range(200))
            p2, p3, p4 = b"birinci-bitisik", b"ikinci-bitisik", b"son"
            m = [b"\x11\x22\x33\x44", b"\xa5\x5a\x0f\xf0", b"\x01\x02\x03\x04",
                 b"\xde\xad\xbe\xef", b"\x7f\x00\x7f\x00"]
            plan = [("kisa", [p0]), ("200 bayt (16-bit uzunluk)", [p1]),
                    ("bitisik iki cerceve", [p2, p3]), ("son kisa", [p4])]
            idx = 0
            for what, payloads in plan:
                s.sendall(b"".join(masked_frame(p, m[idx + k])
                                   for k, p in enumerate(payloads)))
                for p in payloads:
                    fin, opcode, data = recv_frame(s)
                    expected = b"eko%d:" % idx + p
                    ok = opcode == 1 and fin == 1 and data == expected
                    got.append((what, idx, ok, opcode, fin, data, expected))
                    idx += 1
            s.close()
        except (OSError, ConnectionError) as e:
            got.append(("baglanti", len(got), False, 0, 0, repr(e).encode(), b""))
        finally:
            server.kill()
            out = server.communicate()[0].decode("utf-8", "replace")

        bad = [g for g in got if not g[2]]
        if not bad and len(got) == FRAMES:
            print("PASS %d masked client frames on one connection unmasked + echoed "
                  "(short, 16-bit length, coalesced pair, trailing)" % FRAMES)
            return 0
        for what, i, ok, opcode, fin, data, expected in got:
            print("  %s cerceve %d (%s): opcode=%d fin=%d %r (beklenen %r)"
                  % ("ok  " if ok else "FAIL", i, what, opcode, fin, data[:60], expected[:60]))
        if out.strip():
            print("  sunucu ciktisi: " + out.strip()[-300:])
        print("FAIL multi-frame WebSocket: %d/%d frames intact" % (len(got) - len(bad), FRAMES))
        return 1


if __name__ == "__main__":
    sys.exit(main())
