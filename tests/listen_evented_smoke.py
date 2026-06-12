"""Smoke test for `listen_evented(port)` — Wings' poll()-multiplexed
single-thread server.

Builds a tiny server, fires 8 concurrent HTTP GETs, and asserts every
one comes back 200 with the server still alive afterwards. Same shape
as `tests/listen_pool_smoke.py` but exercises the event-loop code
path (`socket_poll` + `socket_set_nonblocking` builtins, single-thread
multiplexing across many fds).

Run with:

    python tests/listen_evented_smoke.py
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
    return {"msg": "evented ok"};
}

get("/", "home");
listen_evented(__PORT__);
"""


def get_url(url: str) -> int:
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code
    except Exception:
        return 0


def main() -> int:
    exe = find_tulpar_exe()
    port = 30431

    workdir = tempfile.mkdtemp()
    src_path = os.path.join(workdir, "ev_smoke.tpr")
    bin_base = os.path.join(workdir, "ev_smoke")
    bin_path = bin_base + (".exe" if sys.platform.startswith("win") else "")

    with open(src_path, "w", encoding="utf-8") as f:
        f.write(SOURCE_TEMPLATE.replace("__PORT__", str(port)))

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
        print(proc.stdout[-1500:])
        print(proc.stderr[-1500:])
        return 1

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

        url = f"http://127.0.0.1:{port}/"
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            results = list(ex.map(lambda _: get_url(url), range(8)))

        if not all(s == 200 for s in results):
            print(f"FAIL: not all requests 200 ({results!r})")
            return 1

        if server.poll() is not None:
            print(f"FAIL: server died after burst, exit={server.returncode}")
            return 1

        print(f"listen_evented OK: 8/8 requests 200, server alive")
        return 0
    finally:
        try:
            server.terminate()
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
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
