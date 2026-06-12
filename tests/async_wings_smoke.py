"""Concurrency smoke for `listen_async`.

Verifies the multi-threaded Wings path doesn't crash under parallel
load and serves every request correctly. Note: handler bodies still
serialise under `_wings_handler_mu` (a Tulpar-level lock around the
shared `_request` global) until LLVM thread-local globals land — the
gain from `listen_async` today is parallel recv/send + the ability
to keep many idle connections open at once, NOT parallel handler
execution.

This test fires 8 concurrent fast-handler hits and asserts:
  * all 8 receive a 200 OK back
  * the server is still alive afterwards (no race-corruption crash)

Run:
    python tests/async_wings_smoke.py
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

SERVER_SRC = """\
import "wings";

func index_handler() {
    return {"hello": "world", "ok": 1};
}

get("/", "index_handler");
listen_async(8910);
"""


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def wait_for_port(host, port, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"server did not open {host}:{port}")


def hit_once(host: str, port: int) -> tuple[bool, float]:
    t0 = time.perf_counter()
    try:
        s = socket.create_connection((host, port), timeout=5.0)
        s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        buf = b""
        while True:
            chunk = s.recv(4096)
            if not chunk: break
            buf += chunk
            if b"\r\n\r\n" in buf and b"\"ok\":1" in buf: break
        s.close()
        dt = time.perf_counter() - t0
        return (b"\"ok\":1" in buf, dt)
    except Exception:
        return (False, time.perf_counter() - t0)


def main() -> int:
    exe = find_tulpar_exe()
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="tulpar_async_") as wd:
        tpr = os.path.join(wd, "async.tpr")
        out = os.path.join(wd, "async")
        with open(tpr, "w", encoding="utf-8") as f: f.write(SERVER_SRC)
        subprocess.check_call([exe, "build", tpr, out],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              timeout=60)
        bin_path = out + (".exe" if sys.platform == "win32" else "")

        env = os.environ.copy()
        env["TULPAR_HTTP_QUIET"] = "1"
        srv = subprocess.Popen([bin_path], stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE, env=env)
        try:
            wait_for_port("127.0.0.1", 8910, timeout=8.0)

            # Warm-up
            hit_once("127.0.0.1", 8910)

            # Fire 8 concurrent connections. Each gets one fast request.
            # The point is to confirm parallel accept + serve doesn't
            # corrupt globals or crash worker threads.
            N = 8
            results = [None] * N
            def worker(i):
                results[i] = hit_once("127.0.0.1", 8910)
            threads = [threading.Thread(target=worker, args=(i,)) for i in range(N)]
            t0 = time.perf_counter()
            for t in threads: t.start()
            for t in threads: t.join()
            wall = time.perf_counter() - t0

            ok_count = sum(1 for r in results if r and r[0])
            if ok_count != N:
                failures.append(f"only {ok_count}/{N} requests succeeded")
            else:
                print(f"async parallel: {N}/{N} succeeded in {wall*1000:.1f}ms")

            # Server should still be responsive after the burst.
            ok, _ = hit_once("127.0.0.1", 8910)
            if not ok:
                failures.append("server stopped responding after parallel burst")
            else:
                print("post-burst: server still responding")

        finally:
            try: srv.terminate()
            except OSError: pass
            try: srv.wait(timeout=2)
            except subprocess.TimeoutExpired: srv.kill()
            # Surface server stderr so worker-thread crashes etc. show up.
            err = srv.stderr.read().decode("utf-8", errors="replace")
            if err.strip():
                print("--- server stderr (last 30 lines) ---")
                for line in err.splitlines()[-30:]:
                    print(line)

    if failures:
        print("FAIL:")
        for f in failures: print("  -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
