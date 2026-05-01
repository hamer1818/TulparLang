"""End-to-end smoke for the new production-helper surface:

* /healthz auto-route
* /metrics auto-route + counters
* wings_openapi() returns an OpenAPI 3.0 doc shaped from registered routes
* log_info / log_error emit JSON lines on stdout

Run:
    python tests/production_smoke.py
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

SRC = """\
import "wings";

func index_handler() {
    log_info("served /");
    return {"hello": "world"};
}

func openapi_handler() {
    return wings_openapi("Smoke API", "0.1.0");
}

get("/", "index_handler");
get("/openapi.json", "openapi_handler");
listen(8802);
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


def fetch_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=5) as r:
        return json.loads(r.read())


def main() -> int:
    exe = find_tulpar_exe()
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="tulpar_prod_") as wd:
        tpr = os.path.join(wd, "prod.tpr")
        out = os.path.join(wd, "prod")
        with open(tpr, "w", encoding="utf-8") as f: f.write(SRC)
        subprocess.check_call([exe, "build", tpr, out],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              timeout=60)
        bin_path = out + (".exe" if sys.platform == "win32" else "")

        env = os.environ.copy()
        env["TULPAR_HTTP_QUIET"] = "1"
        srv = subprocess.Popen([bin_path], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=env)
        try:
            wait_for_port("127.0.0.1", 8802, timeout=8.0)

            # Make a few hits so /metrics counters move
            for _ in range(3):
                fetch_json("http://127.0.0.1:8802/")

            health = fetch_json("http://127.0.0.1:8802/healthz")
            if health.get("status") != "ok":
                failures.append(f"/healthz: bad payload {health!r}")
            elif "uptime_s" not in health or "now" not in health:
                failures.append(f"/healthz: missing keys {list(health.keys())}")
            else:
                print(f"healthz OK uptime={health['uptime_s']}s now={health['now']}")

            metrics = fetch_json("http://127.0.0.1:8802/metrics")
            # 3 calls to "/" + 1 call each to /healthz and /metrics so far.
            # The /metrics call itself is counted, so total >= 4 by the time
            # we see it.
            if metrics.get("requests_total", 0) < 4:
                failures.append(f"/metrics: requests_total too low {metrics!r}")
            elif metrics.get("requests_2xx", 0) < 4:
                failures.append(f"/metrics: requests_2xx too low {metrics!r}")
            else:
                print(f"metrics OK total={metrics['requests_total']} "
                      f"2xx={metrics['requests_2xx']} 4xx={metrics['requests_4xx']}")

            spec = fetch_json("http://127.0.0.1:8802/openapi.json")
            if spec.get("openapi") != "3.0.0":
                failures.append(f"openapi: bad version {spec.get('openapi')!r}")
            paths = spec.get("paths", {})
            if "/" not in paths or "get" not in paths["/"]:
                failures.append(f"openapi: missing GET / route {paths!r}")
            elif "/healthz" not in paths:
                failures.append(f"openapi: missing /healthz route {list(paths.keys())}")
            else:
                print(f"openapi OK paths={sorted(paths.keys())}")

            # Drain server stdout — should contain at least one log_info JSON line.
            srv.terminate()
            try: srv.wait(timeout=2)
            except subprocess.TimeoutExpired: srv.kill()
            tail = srv.stdout.read().decode("utf-8", errors="replace")
            log_lines = [l for l in tail.splitlines() if "served /" in l]
            if not log_lines:
                failures.append("log_info: no JSON line with 'served /' on stdout")
            else:
                # Confirm it's valid JSON
                try:
                    obj = json.loads(log_lines[0])
                    if obj.get("level") != "info" or "@timestamp" not in obj:
                        failures.append(f"log_info: bad shape {obj!r}")
                    else:
                        print(f"log_info OK ({len(log_lines)} line(s))")
                except json.JSONDecodeError:
                    failures.append(f"log_info: not JSON: {log_lines[0]!r}")
        finally:
            if srv.poll() is None:
                srv.kill()

    if failures:
        print("FAIL:")
        for f in failures: print("  -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
