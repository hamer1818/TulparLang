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
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "wsecho.tpr")
        binary = os.path.join(tmp, "wsecho_bin")
        with open(src, "w") as f:
            f.write(SERVER_SRC)
        build = subprocess.run(["./tulpar", "build", src, binary],
                               capture_output=True, text=True)
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


if __name__ == "__main__":
    sys.exit(main())
