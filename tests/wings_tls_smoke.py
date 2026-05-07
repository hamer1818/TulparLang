"""Smoke test for `wings_tls(port, cert, key)` — Wings TLS listener.

Generates a self-signed cert/key pair on the fly, builds a tiny HTTPS
server with `wings_tls`, fires a single `https://localhost` GET
through Python's urllib (with cert verification disabled — same as
`curl --insecure`), and asserts a 200 response.

Skips itself with exit 0 + a SKIP message if:
- `openssl` is not on PATH (cert generation impossible).
- The Tulpar binary was built without OpenSSL (TLS-disabled config).

Run with:

    python tests/wings_tls_smoke.py
"""

from __future__ import annotations

import os
import shutil
import ssl
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
import "wings_tls";

func home() {
    return {"msg": "tls ok"};
}

get("/", "home");
wings_tls(__PORT__, "__CERT__", "__KEY__");
"""


def gen_self_signed(workdir: str) -> tuple[str, str] | None:
    cert = os.path.join(workdir, "server.crt")
    key = os.path.join(workdir, "server.key")
    openssl = shutil.which("openssl")
    if not openssl:
        return None
    try:
        subprocess.run(
            [openssl, "req", "-x509", "-newkey", "rsa:2048",
             "-keyout", key, "-out", cert,
             "-days", "1", "-nodes", "-subj", "/CN=localhost"],
            capture_output=True, timeout=30, check=True,
        )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    if not (os.path.exists(cert) and os.path.exists(key)):
        return None
    return cert, key


def main() -> int:
    exe = find_tulpar_exe()
    port = 30443

    workdir = tempfile.mkdtemp()
    try:
        certkey = gen_self_signed(workdir)
        if not certkey:
            print("SKIP: openssl unavailable for cert generation")
            return 0
        cert_path, key_path = certkey

        # Tulpar source paths inside the temp workdir need forward
        # slashes to escape Windows backslash interpretation when
        # baked into the .tpr literal.
        cert_lit = cert_path.replace("\\", "/")
        key_lit = key_path.replace("\\", "/")
        src_path = os.path.join(workdir, "tls_smoke.tpr")
        bin_base = os.path.join(workdir, "tls_smoke")
        bin_path = bin_base + (".exe" if sys.platform.startswith("win") else "")

        src = (SOURCE_TEMPLATE
               .replace("__PORT__", str(port))
               .replace("__CERT__", cert_lit)
               .replace("__KEY__", key_lit))
        with open(src_path, "w", encoding="utf-8") as f:
            f.write(src)

        proc = subprocess.run(
            [exe, "build", src_path, bin_base],
            capture_output=True, text=True, timeout=60,
            encoding="utf-8", errors="replace",
        )
        if proc.returncode != 0 or not os.path.exists(bin_path):
            print("FAIL: build failed")
            print(proc.stdout[-1500:])
            print(proc.stderr[-1500:])
            return 1

        server = subprocess.Popen(
            [bin_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            time.sleep(2)
            if server.poll() is not None:
                # If the binary printed "TLS init failed" it's the
                # TLS-disabled case (or an environmental cert issue).
                err = (server.stdout.read() + server.stderr.read()).decode(
                    "utf-8", errors="replace") if server.stdout else ""
                if "TLS init failed" in err or "without OpenSSL" in err:
                    print("SKIP: tulpar binary lacks OpenSSL TLS support")
                    return 0
                print(f"FAIL: server died during startup, exit={server.returncode}")
                print(err[-500:])
                return 1

            # Build an unverified SSL context (self-signed cert).
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE

            try:
                with urllib.request.urlopen(
                    f"https://127.0.0.1:{port}/",
                    timeout=5, context=ctx,
                ) as r:
                    status = r.status
                    body = r.read().decode("utf-8", errors="replace")
            except Exception as e:
                # Server may have crashed during the handshake.
                if server.poll() is not None:
                    print(f"FAIL: server died during request "
                          f"(exit={server.returncode}); request err={e}")
                else:
                    print(f"FAIL: request failed: {e}")
                return 1

            if status != 200:
                print(f"FAIL: status={status} (want 200)")
                return 1
            if "tls ok" not in body:
                print(f"FAIL: body missing 'tls ok' (got {body!r})")
                return 1

            print(f"wings_tls OK: HTTPS GET / -> {status}, body {len(body)} B")
            return 0
        finally:
            try:
                server.terminate()
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
    finally:
        try:
            shutil.rmtree(workdir, ignore_errors=True)
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
