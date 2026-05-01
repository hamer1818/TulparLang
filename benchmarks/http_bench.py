"""HTTP throughput benchmark for Tulpar's Wings/Router server.

Spawns a Tulpar server, hammers it with N requests over K parallel
connections (each connection reuses keep-alive), and reports req/sec.
Compares against an equivalent baseline running on Python's built-in
HTTP server so the absolute number has a familiar reference point.

Run:

    python benchmarks/http_bench.py [--requests 5000] [--connections 8]
"""

from __future__ import annotations

import argparse
import os
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time

WINGS_SRC = """\
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

PY_BASELINE_SRC = """\
import http.server, json, sys
from http.server import ThreadingHTTPServer

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def do_GET(self):
        body = json.dumps({"hello": "world"}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a, **k): pass

ThreadingHTTPServer.allow_reuse_address = True
with ThreadingHTTPServer(("127.0.0.1", 8766), H) as srv:
    sys.stdout.write("ready\\n"); sys.stdout.flush()
    srv.serve_forever()
"""

NODE_BASELINE_SRC = """\
const http = require("http");
const body = JSON.stringify({hello: "world"});
const srv = http.createServer((req, res) => {
  res.writeHead(200, {
    "Content-Type": "application/json",
    "Content-Length": Buffer.byteLength(body),
  });
  res.end(body);
});
srv.keepAliveTimeout = 60000;
srv.listen(8767, "127.0.0.1", () => {
  process.stdout.write("ready\\n");
});
"""


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def wait_for_port(host: str, port: int, timeout: float = 10.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"server did not open {host}:{port} within {timeout}s")


def hammer(host: str, port: int, n_requests: int, n_threads: int) -> tuple[float, int, list[float]]:
    """Drive `n_requests` GETs over `n_threads` keep-alive connections.
    Returns (wall_seconds, completed, per_thread_seconds)."""

    per_thread = [0.0] * n_threads
    completed = [0] * n_threads
    request = (b"GET / HTTP/1.1\r\n"
               b"Host: localhost\r\n"
               b"\r\n")
    each = n_requests // n_threads
    barrier = threading.Barrier(n_threads + 1)

    def worker(idx: int):
        sock = socket.create_connection((host, port), timeout=15.0)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        buf = bytearray()
        barrier.wait()
        t0 = time.perf_counter()
        try:
            for _ in range(each):
                sock.sendall(request)
                # Read one HTTP/1.1 response
                while b"\r\n\r\n" not in buf:
                    chunk = sock.recv(4096)
                    if not chunk: return
                    buf.extend(chunk)
                he = buf.index(b"\r\n\r\n") + 4
                cl = 0
                for line in buf[:he].decode("ascii", errors="replace").split("\r\n"):
                    if line.lower().startswith("content-length:"):
                        cl = int(line.split(":", 1)[1].strip()); break
                while len(buf) < he + cl:
                    chunk = sock.recv(4096)
                    if not chunk: return
                    buf.extend(chunk)
                buf = buf[he + cl:]
                completed[idx] += 1
        finally:
            sock.close()
            per_thread[idx] = time.perf_counter() - t0

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(n_threads)]
    for t in threads: t.start()
    barrier.wait()
    t0 = time.perf_counter()
    for t in threads: t.join()
    wall = time.perf_counter() - t0
    return wall, sum(completed), per_thread


def bench_server(label: str, start_cmd: list, port: int, n_requests: int,
                 n_threads: int, ready_marker: str | None = None,
                 stdout_pipe: bool = False, env: dict | None = None) -> dict:
    proc = subprocess.Popen(start_cmd,
                             stdout=subprocess.PIPE if stdout_pipe else subprocess.DEVNULL,
                             stderr=subprocess.PIPE,
                             env=env)
    try:
        if ready_marker and stdout_pipe:
            for line in proc.stdout:
                if ready_marker in line.decode("utf-8", errors="replace"):
                    break
        wait_for_port("127.0.0.1", port, timeout=10.0)
        # 1 short warmup pass.
        hammer("127.0.0.1", port, max(50, n_threads * 10), n_threads)
        wall, done, per_thread = hammer("127.0.0.1", port, n_requests, n_threads)
        rps = done / wall if wall > 0 else 0
        return {"label": label, "done": done, "wall_s": wall, "rps": rps,
                "per_thread_s": per_thread}
    finally:
        try: proc.terminate()
        except Exception: pass
        try: proc.wait(timeout=2)
        except subprocess.TimeoutExpired: proc.kill()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--requests", type=int, default=5000,
                    help="total HTTP requests across all threads")
    ap.add_argument("--connections", type=int, default=8,
                    help="parallel keep-alive TCP connections")
    args = ap.parse_args()

    exe = find_tulpar_exe()

    results = []
    with tempfile.TemporaryDirectory(prefix="tulpar_bench_") as wd:
        # Build Tulpar server
        tpr_path = os.path.join(wd, "wings_bench.tpr")
        bin_path = os.path.join(wd, "wings_bench")
        with open(tpr_path, "w", encoding="utf-8") as f:
            f.write(WINGS_SRC)
        subprocess.check_call([exe, "build", tpr_path, bin_path],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              timeout=60)
        tulpar_bin = bin_path + (".exe" if sys.platform == "win32" else "")

        # Tulpar Wings — TULPAR_HTTP_QUIET silences the per-request access
        # log on the server side so we measure the network-path cost, not
        # console I/O. This matches how a benchmarked Go/Node server runs.
        env = os.environ.copy()
        env["TULPAR_HTTP_QUIET"] = "1"
        results.append(bench_server("Tulpar Wings", [tulpar_bin], 8765,
                                     args.requests, args.connections,
                                     env=env))

        # Python baseline (ThreadingHTTPServer)
        py_path = os.path.join(wd, "py_baseline.py")
        with open(py_path, "w", encoding="utf-8") as f:
            f.write(PY_BASELINE_SRC)
        results.append(bench_server("Python ThreadingHTTP", [sys.executable, "-u", py_path],
                                     8766, args.requests, args.connections,
                                     ready_marker="ready", stdout_pipe=True))

        # Node.js baseline (only if `node` is on PATH)
        if any(os.path.exists(os.path.join(p, "node.exe" if sys.platform == "win32" else "node"))
               for p in os.environ.get("PATH", "").split(os.pathsep)):
            node_path = os.path.join(wd, "node_baseline.js")
            with open(node_path, "w", encoding="utf-8") as f:
                f.write(NODE_BASELINE_SRC)
            results.append(bench_server("Node.js http", ["node", node_path],
                                         8767, args.requests, args.connections,
                                         ready_marker="ready", stdout_pipe=True))

    print()
    print(f"Requests: {args.requests}, Connections: {args.connections} (keep-alive)")
    print(f"{'Server':<22} {'Done':>6} {'Wall (s)':>10} {'req/sec':>12}")
    for r in results:
        print(f"{r['label']:<22} {r['done']:>6} {r['wall_s']:>10.3f} {r['rps']:>12.0f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
