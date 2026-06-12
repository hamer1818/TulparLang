"""End-to-end keep-alive smoke test for Tulpar's HTTP server.

Spins up a tiny Wings server, opens ONE TCP connection, sends three
HTTP/1.1 requests back to back, and verifies that all three responses
arrive on the same socket. Without keep-alive the server would close
after the first response and the second send would fail.

Run:

    python tests/keepalive_smoke.py
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import tempfile
import time

SERVER_SRC = """\
import "wings";

func index_handler() {
    return {"hello": "world"};
}

func ping_handler() {
    return {"pong": 1};
}

get("/", "index_handler");
get("/ping", "ping_handler");

listen(8765);
"""


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def build_server(exe: str, src: str, workdir: str) -> str:
    tpr_path = os.path.join(workdir, "ka_server.tpr")
    out_path = os.path.join(workdir, "ka_server")
    with open(tpr_path, "w", encoding="utf-8") as f:
        f.write(src)
    subprocess.check_call([exe, "build", tpr_path, out_path],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE,
                          timeout=60)
    bin_path = out_path + (".exe" if sys.platform == "win32" else "")
    if not os.path.exists(bin_path):
        raise RuntimeError(f"server binary not produced: {bin_path}")
    return bin_path


def wait_for_port(host: str, port: int, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"server did not open {host}:{port} within {timeout}s")


def read_one_response(sock: socket.socket, buf: bytearray) -> tuple[bytes, bytearray]:
    """Read until we have a full HTTP/1.x response (headers + Content-Length
    body) on `sock`, draining bytes from `buf` first. Returns (response,
    leftover_buf).
    """
    while b"\r\n\r\n" not in buf:
        data = sock.recv(4096)
        if not data:
            return bytes(buf), bytearray()
        buf.extend(data)
    header_end = buf.index(b"\r\n\r\n") + 4
    headers = buf[:header_end].decode("ascii", errors="replace")
    cl = 0
    for line in headers.split("\r\n"):
        if line.lower().startswith("content-length:"):
            cl = int(line.split(":", 1)[1].strip())
            break
    needed = header_end + cl
    while len(buf) < needed:
        data = sock.recv(4096)
        if not data:
            break
        buf.extend(data)
    return bytes(buf[:needed]), bytearray(buf[needed:])


def main() -> int:
    exe = find_tulpar_exe()

    with tempfile.TemporaryDirectory(prefix="tulpar_ka_") as wd:
        bin_path = build_server(exe, SERVER_SRC, wd)
        proc = subprocess.Popen([bin_path],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE)
        try:
            wait_for_port("127.0.0.1", 8765, timeout=8.0)

            failures: list[str] = []

            sock = socket.create_connection(("127.0.0.1", 8765), timeout=5.0)
            try:
                requests = [
                    b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                    b"GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n",
                    b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                ]
                buf = bytearray()
                responses = []
                for i, req in enumerate(requests):
                    sock.sendall(req)
                    resp, buf = read_one_response(sock, buf)
                    responses.append(resp)
                    if not resp:
                        failures.append(f"request {i}: empty response")
                        break
                    text = resp.decode("utf-8", errors="replace")
                    if "200" not in text.split("\r\n", 1)[0]:
                        failures.append(f"request {i}: bad status line: "
                                        f"{text.split(chr(13), 1)[0]!r}")

                # Verify all three responses came back on the same socket.
                if len(responses) == 3:
                    print(f"keep-alive: 3/3 responses received on one TCP socket")
                    # First two should advertise keep-alive, third should be close.
                    for i in (0, 1):
                        if "Connection: keep-alive" not in responses[i].decode(
                                "utf-8", errors="replace"):
                            failures.append(f"response {i}: missing "
                                            f"`Connection: keep-alive`")
                    if "Connection: close" not in responses[2].decode(
                            "utf-8", errors="replace"):
                        failures.append("response 2: missing `Connection: close`")
                else:
                    failures.append(f"only {len(responses)}/3 responses arrived")

            finally:
                try: sock.close()
                except OSError: pass

            if failures:
                print("FAIL:")
                for f in failures:
                    print("  -", f)
                return 1
            print("ALL CHECKS PASSED")
            return 0

        finally:
            try: proc.terminate()
            except OSError: pass
            try: proc.wait(timeout=2)
            except subprocess.TimeoutExpired: proc.kill()


if __name__ == "__main__":
    sys.exit(main())
