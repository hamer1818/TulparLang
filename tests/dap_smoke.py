"""Smoke test for `tulpar debug <file.tpr>` — the DAP scaffold from
Plan 07 PR 4.

Walks the minimal handshake (`initialize` → response + `initialized`
event → `disconnect`), checks that an unimplemented request gets a
structured "not implemented" error (rather than a hang), and verifies
the process exits cleanly afterwards.

Run:
    python tests/dap_smoke.py
"""

from __future__ import annotations

import json
import os
import subprocess
import sys


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def send(proc: subprocess.Popen, msg: dict) -> None:
    body = json.dumps(msg).encode()
    proc.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode())
    proc.stdin.write(body)
    proc.stdin.flush()


def recv(proc: subprocess.Popen) -> dict | None:
    # Read Content-Length header and any other headers until the
    # \r\n\r\n separator. Same shape LSP / DAP both use.
    headers = b""
    while b"\r\n\r\n" not in headers:
        c = proc.stdout.read(1)
        if not c:
            return None
        headers += c
    head, _, rest = headers.partition(b"\r\n\r\n")
    cl = None
    for h in head.split(b"\r\n"):
        if h.lower().startswith(b"content-length"):
            cl = int(h.split(b":", 1)[1].strip())
            break
    assert cl is not None, f"no Content-Length in {head!r}"
    body = rest + proc.stdout.read(cl - len(rest))
    return json.loads(body)


def main() -> int:
    exe = find_tulpar_exe()
    failures: list[str] = []

    # Stage a tiny .tpr in the OS temp dir so the launch step has a
    # real source file to point at. We use the OS-temp resolver
    # (`tempfile.mkstemp`) rather than `/tmp/...` so the smoke runs on
    # Windows native binaries (where /tmp doesn't resolve to a real
    # path the AOT pipeline can fopen) as well as POSIX.
    import tempfile
    tpr_fd, tpr_path = tempfile.mkstemp(prefix="dap_smoke_", suffix=".tpr")
    with os.fdopen(tpr_fd, "w", encoding="utf-8") as f:
        f.write('print("hello from dap smoke");\n')
    # AOT pipeline emits next to the source basename; clean it up too.
    out_path = tpr_path[:-4]  # strip .tpr
    cleanup_paths = [tpr_path, out_path, out_path + ".exe",
                     out_path + ".o", out_path + ".ll"]

    proc = subprocess.Popen(
        [exe, "debug", tpr_path],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )

    try:
        # 1) initialize -> response with capabilities body
        send(proc, {
            "seq": 1, "type": "request", "command": "initialize",
            "arguments": {"clientID": "smoke", "adapterID": "tulpar"},
        })
        r = recv(proc)
        if not r or r.get("type") != "response" or r.get("command") != "initialize":
            failures.append(f"initialize: wrong shape: {r!r}")
        elif not r.get("success"):
            failures.append(f"initialize: success=false: {r!r}")
        elif "supportsConfigurationDoneRequest" not in (r.get("body") or {}):
            failures.append(f"initialize: missing capabilities body: {r!r}")

        # 2) initialized event MUST arrive after the initialize response
        evt = recv(proc)
        if not evt or evt.get("type") != "event" or evt.get("event") != "initialized":
            failures.append(f"initialized event: wrong shape: {evt!r}")

        # 3) launch -> AOT-builds the program with debug info, replies
        # success. The pipeline's stdout is redirected to stderr for the
        # duration of the build so it can't corrupt the DAP wire (every
        # `[AOT] ...` byte on stdout here would break Content-Length
        # framing). A failed build comes back as success=false with a
        # `message`, never as a hang.
        send(proc, {
            "seq": 2, "type": "request", "command": "launch",
            "arguments": {"program": tpr_path, "stopOnEntry": False},
        })
        r = recv(proc)
        if not r or r.get("command") != "launch":
            failures.append(f"launch: wrong shape: {r!r}")
        elif not r.get("success"):
            failures.append(f"launch: success=false: {r!r}")

        # 4) configurationDone -> success (PR 4c will hand control to
        # gdb here; today it's a no-op acknowledge).
        send(proc, {
            "seq": 3, "type": "request", "command": "configurationDone",
            "arguments": {},
        })
        r = recv(proc)
        if not r or not r.get("success"):
            failures.append(f"configurationDone: wrong shape: {r!r}")

        # 5) threads -> exactly one fake thread (id=1, name="main")
        send(proc, {
            "seq": 4, "type": "request", "command": "threads",
            "arguments": {},
        })
        r = recv(proc)
        if not r or not r.get("success"):
            failures.append(f"threads: success=false: {r!r}")
        else:
            threads = (r.get("body") or {}).get("threads") or []
            if len(threads) != 1 or threads[0].get("id") != 1:
                failures.append(f"threads: expected [{{id:1,...}}], got {threads!r}")

        # 6) An unimplemented request still returns a structured
        # "not implemented" response, not a hang. We pick `evaluate`
        # specifically because PR 4c hasn't wired it up yet.
        send(proc, {
            "seq": 5, "type": "request", "command": "evaluate",
            "arguments": {"expression": "1+1", "context": "repl"},
        })
        r = recv(proc)
        if not r or r.get("command") != "evaluate":
            failures.append(f"evaluate stub: wrong shape: {r!r}")
        elif r.get("success") is not False:
            failures.append(f"evaluate stub: expected success=false: {r!r}")

        # 7) disconnect -> success, process exits 0
        send(proc, {
            "seq": 6, "type": "request", "command": "disconnect",
            "arguments": {},
        })
        r = recv(proc)
        if not r or r.get("command") != "disconnect" or not r.get("success"):
            failures.append(f"disconnect: wrong shape: {r!r}")

        # 5) clean exit within 3s
        try:
            rc = proc.wait(timeout=3)
            if rc != 0:
                failures.append(f"adapter exit code = {rc}, expected 0")
        except subprocess.TimeoutExpired:
            failures.append("adapter did not exit within 3s after disconnect")
            proc.kill()

    finally:
        try:
            proc.kill()
        except Exception:
            pass
        for p in cleanup_paths:
            try:
                os.remove(p)
            except OSError:
                pass

    if failures:
        print("FAIL:")
        for f in failures:
            print(" -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
