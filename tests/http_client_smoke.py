"""Smoke test for the outbound `http_client` builtins.

Spins up a Wings server, has a Tulpar client (built with `import "http_client"`)
hit it via http_get / http_post / http_get_json, and asserts the
response shape.

Run:
    python tests/http_client_smoke.py
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
    return {"hello": "world", "n": 42};
}

func echo_handler() {
    return _request["body"];
}

get("/", "index_handler");
post("/echo", "echo_handler");
listen(8801);
"""

CLIENT_SRC = """\
import "http_client";

print("=== http_get ===");
json r = http_get("http://127.0.0.1:8801/");
if (r["ok"]) {
    print("ok status=" + toString(r["status"]));
    print("body=" + r["body"]);
} else {
    print("err: " + r["error"]);
}

print("=== http_get_json ===");
json j = http_get_json("http://127.0.0.1:8801/");
if (j["ok"]) {
    json d = j["data"];
    print("hello=" + d["hello"]);
    print("n=" + toString(d["n"]));
}

print("=== http_post ===");
json p = http_post("http://127.0.0.1:8801/echo", "{\\"x\\":1}");
print("post status=" + toString(p["status"]));
print("post body=" + p["body"]);

print("=== bad-url ===");
json bad = http_get("https://example/");
print("bad ok=" + toString(bad["ok"]));
"""


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def wait_for_port(host, port, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"server did not open {host}:{port}")


def main() -> int:
    exe = find_tulpar_exe()
    failures = []

    with tempfile.TemporaryDirectory(prefix="tulpar_httpc_") as wd:
        # Build server
        srv_tpr = os.path.join(wd, "server.tpr")
        srv_bin = os.path.join(wd, "server")
        with open(srv_tpr, "w", encoding="utf-8") as f: f.write(SERVER_SRC)
        subprocess.check_call([exe, "build", srv_tpr, srv_bin],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              timeout=60)
        srv_exe = srv_bin + (".exe" if sys.platform == "win32" else "")

        # Build client
        cli_tpr = os.path.join(wd, "client.tpr")
        cli_bin = os.path.join(wd, "client")
        with open(cli_tpr, "w", encoding="utf-8") as f: f.write(CLIENT_SRC)
        subprocess.check_call([exe, "build", cli_tpr, cli_bin],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              timeout=60)
        cli_exe = cli_bin + (".exe" if sys.platform == "win32" else "")

        env = os.environ.copy()
        env["TULPAR_HTTP_QUIET"] = "1"
        srv = subprocess.Popen([srv_exe], stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE, env=env)
        try:
            wait_for_port("127.0.0.1", 8801, timeout=8.0)
            res = subprocess.run([cli_exe], capture_output=True, timeout=15,
                                 text=True)
            out = res.stdout
            print(out)

            # Assertions
            checks = [
                ("ok status=200", "http_get reported success"),
                ('"hello":"world"', "body contains hello key"),
                ("hello=world", "http_get_json parsed body"),
                ("n=42", "http_get_json parsed numeric field"),
                ("post status=200", "http_post returned 200"),
                ("bad ok=0", "https:// URL is rejected (no TLS yet)"),
            ]
            for needle, why in checks:
                if needle not in out:
                    failures.append(f"{why}: missing {needle!r}")
        finally:
            try: srv.terminate()
            except OSError: pass
            try: srv.wait(timeout=2)
            except subprocess.TimeoutExpired: srv.kill()

    if failures:
        print("FAIL:")
        for f in failures: print("  -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
