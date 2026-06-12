"""Smoke test for `listen_pool(port, n_workers)` in lib/wings.tpr.

Builds a tiny wings server that uses the worker-pool listener (PR #92)
and verifies that:

1. The binary builds and starts without crashing.
2. The pool actually serves requests in parallel — fire 8 HTTP GETs at
   the same time and require all 8 to come back 200 within 5s.
3. The server process survives the burst (no thread-pool exhaustion,
   no race-condition crash).

Run with:

    python tests/listen_pool_smoke.py
"""

from __future__ import annotations

import concurrent.futures
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


SOURCE_TEMPLATE = """\
import "wings";

func home() {
    return {"msg": "pool ok"};
}

get("/", "home");
listen_pool(__PORT__, 4);
"""


def get_url(url: str) -> int:
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code  # any HTTP status counts as "server responded"
    except Exception:
        return 0


def main() -> int:
    exe = find_tulpar_exe()
    # Use a less-likely-to-collide port. Tests run sequentially in CI but
    # 3000/3001 are taken by other examples in the same suite.
    port = 30421

    workdir = tempfile.mkdtemp()
    src_path = os.path.join(workdir, "pool_smoke.tpr")
    bin_base = os.path.join(workdir, "pool_smoke")
    bin_path = bin_base + (".exe" if sys.platform.startswith("win") else "")

    with open(src_path, "w", encoding="utf-8") as f:
        f.write(SOURCE_TEMPLATE.replace("__PORT__", str(port)))

    # 1. Build
    proc = subprocess.run(
        [exe, "build", src_path, bin_base],
        capture_output=True,
        text=True,
        timeout=60,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0 or not os.path.exists(bin_path):
        print("FAIL: build failed")
        print(proc.stdout[-1000:])
        print(proc.stderr[-1000:])
        return 1

    # 2. Spawn server
    server = subprocess.Popen(
        [bin_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(2)
        if server.poll() is not None:
            print(f"FAIL: server died during startup, exit={server.returncode}")
            return 1

        # 3. Burst — 8 parallel requests, expect every one to be 200.
        url = f"http://127.0.0.1:{port}/"
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            results = list(ex.map(lambda _: get_url(url), range(8)))

        if not all(s == 200 for s in results):
            print(f"FAIL: not all requests 200 ({results!r})")
            return 1

        # 4. Server still alive after the burst.
        if server.poll() is not None:
            print(f"FAIL: server died after burst, exit={server.returncode}")
            return 1

        print(f"listen_pool OK: 8/8 requests 200, server alive")
        return 0
    finally:
        try:
            server.terminate()
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
        # Cleanup
        for p in [src_path, bin_path, bin_base + ".ll", bin_base + ".o"]:
            try:
                os.unlink(p)
            except OSError:
                pass
        try:
            os.rmdir(workdir)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
