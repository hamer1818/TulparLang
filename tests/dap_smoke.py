"""Smoke test for `tulpar debug <file.tpr>` — the DAP server.

Walks the full Plan 07 Part B handshake and verifies it produces the
expected wire output:

  1. initialize    →  response with capabilities
                  →  `initialized` event
  2. launch        →  AOT-builds the .tpr with `--debug`, spawns
                     `gdb --interpreter=mi3 -nx <binary>`, returns
                     success=true once both are ready.
  3. setBreakpoints  →  forwards each line to gdb via `-break-insert`
                       and returns the verified Breakpoint[]
                       reply. PR 4c accepts either verified or
                       deferred-pending — the program is so short
                       that whether the breakpoint actually fires
                       before exit is timing-dependent.
  4. configurationDone  →  sends `-exec-run` to gdb. The smoke
                          then drains DAP events until it sees
                          `terminated` (with a 10 s timeout). The
                          inferior `print("hello from dap smoke")`
                          races to completion well before 10 s so
                          this gates a real gdb subprocess working
                          end-to-end.
  5. threads / evaluate stub / disconnect — same as the prior smoke.

If gdb isn't installed (CI macOS, some dev machines), launch comes
back as success=false with a "failed to spawn gdb" message and the
smoke verifies THAT path instead — it doesn't fail the run just
because gdb is absent.

Run:
    python tests/dap_smoke.py
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def gdb_available() -> bool:
    # Quick PATH probe so the smoke can branch instead of failing on
    # environments without gdb installed. We don't actually parse the
    # output — `--version` is just a cheap "does the binary exist and
    # respond" check.
    exe = shutil.which("gdb") or shutil.which("gdb.exe")
    if not exe:
        return False
    try:
        subprocess.run([exe, "--version"], capture_output=True, timeout=5)
        return True
    except (OSError, subprocess.TimeoutExpired):
        return False


def send(proc: subprocess.Popen, msg: dict) -> None:
    body = json.dumps(msg).encode()
    proc.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode())
    proc.stdin.write(body)
    proc.stdin.flush()


def recv(proc: subprocess.Popen, timeout: float | None = None) -> dict | None:
    # Read Content-Length header and any other headers until the
    # \r\n\r\n separator. Same shape LSP / DAP both use.
    deadline = None if timeout is None else (time.monotonic() + timeout)
    headers = b""
    while b"\r\n\r\n" not in headers:
        if deadline is not None and time.monotonic() > deadline:
            return None
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
    body = rest
    while len(body) < cl:
        chunk = proc.stdout.read(cl - len(body))
        if not chunk:
            return None
        body += chunk
    return json.loads(body)


def recv_response(proc: subprocess.Popen, command: str,
                  collected_events: list[dict],
                  timeout: float = 5.0) -> dict | None:
    """Recv messages until we see a `response` for `command`, stashing
    any `event` messages into `collected_events` along the way.

    The gdb reader thread emits `stopped` / `terminated` / `output`
    events asynchronously, so a response can be preceded by an
    arbitrary number of events on the wire."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        m = recv(proc, timeout=max(0.1, deadline - time.monotonic()))
        if m is None:
            return None
        if m.get("type") == "event":
            collected_events.append(m)
            continue
        if m.get("type") == "response" and m.get("command") == command:
            return m
        # Some other shape — keep waiting.
    return None


def drain_until(proc: subprocess.Popen,
                 collected_events: list[dict],
                 stop_on: tuple[str, ...],
                 timeout: float = 10.0) -> str | None:
    """Drain DAP messages, stashing events in `collected_events`,
    until one of the named events arrives or timeout. Returns the
    event name that caused the stop (or None on timeout).

    Used after configurationDone:
      - if the breakpoint fires, we see `stopped` and the inferior is
        paused — caller can exercise stackTrace / variables.
      - if the program runs to completion without hitting the
        breakpoint, we see `terminated` instead.
      - on timeout, caller fails the smoke."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        m = recv(proc, timeout=max(0.1, deadline - time.monotonic()))
        if m is None:
            return None
        collected_events.append(m)
        if m.get("type") == "event" and m.get("event") in stop_on:
            return m.get("event")
    return None


def main() -> int:
    exe = find_tulpar_exe()
    failures: list[str] = []
    have_gdb = gdb_available()
    print(f"[smoke] gdb available: {have_gdb}", file=sys.stderr)

    # Stage a tiny .tpr in the OS temp dir so the launch step has a
    # real source file to point at. We use the OS-temp resolver
    # (`tempfile.mkstemp`) rather than `/tmp/...` so the smoke runs on
    # Windows native binaries (where /tmp doesn't resolve to a real
    # path the AOT pipeline can fopen) as well as POSIX.
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
        events: list[dict] = []

        # 1) initialize -> response with capabilities body
        send(proc, {
            "seq": 1, "type": "request", "command": "initialize",
            "arguments": {"clientID": "smoke", "adapterID": "tulpar"},
        })
        r = recv_response(proc, "initialize", events)
        if not r:
            failures.append("initialize: no response")
        elif not r.get("success"):
            failures.append(f"initialize: success=false: {r!r}")
        elif "supportsConfigurationDoneRequest" not in (r.get("body") or {}):
            failures.append(f"initialize: missing capabilities body: {r!r}")

        # 2) initialized event MUST arrive after the initialize response.
        # The current adapter emits it inside handle_initialize, so it
        # should already be in `events`.
        if not any(e.get("event") == "initialized" for e in events):
            evt = recv(proc, timeout=2.0)
            if evt:
                events.append(evt)
        if not any(e.get("event") == "initialized" for e in events):
            failures.append("never saw `initialized` event")

        # 3) launch -> AOT-builds the program with debug info AND
        # spawns gdb. The pipeline's stdout is redirected to stderr
        # for the duration of the build so it can't corrupt the DAP
        # wire. If gdb isn't installed, success=false with a clear
        # message naming gdb — the smoke validates THAT path on
        # systems without gdb instead of declaring failure.
        send(proc, {
            "seq": 2, "type": "request", "command": "launch",
            "arguments": {"program": tpr_path, "stopOnEntry": False},
        })
        r = recv_response(proc, "launch", events, timeout=30.0)
        launch_ok = bool(r and r.get("success"))
        if not r:
            failures.append("launch: no response")
        elif not launch_ok:
            msg = (r.get("message") or "")
            if have_gdb:
                failures.append(f"launch: success=false despite gdb present: {r!r}")
            elif "gdb" not in msg.lower():
                failures.append(f"launch: failed but message doesn't mention gdb: {r!r}")
            # If gdb is missing, accept the structured failure and
            # skip the inferior portion of the test — disconnect
            # below will still verify the adapter shuts down cleanly.

        if launch_ok:
            # 4) setBreakpoints -> forwards `-break-insert` to gdb
            # per line and returns the verified Breakpoint[] reply.
            send(proc, {
                "seq": 3, "type": "request", "command": "setBreakpoints",
                "arguments": {
                    "source": {"path": tpr_path},
                    "breakpoints": [{"line": 1}],
                },
            })
            r = recv_response(proc, "setBreakpoints", events, timeout=5.0)
            if not r:
                failures.append("setBreakpoints: no response")
            elif not r.get("success"):
                failures.append(f"setBreakpoints: success=false: {r!r}")
            else:
                bps = (r.get("body") or {}).get("breakpoints") or []
                if len(bps) != 1:
                    failures.append(f"setBreakpoints: expected 1 entry, got {bps!r}")
                # Either verified=true (gdb resolved it) or verified=
                # false with a message (couldn't resolve at the .tpr
                # line — depends on how DWARF maps `print(...)`) is
                # acceptable. We just want a structured reply, not a
                # hang.

            # 5) configurationDone -> response, then -exec-run runs
            # the inferior. We expect a `terminated` event within
            # 10 s as the tiny program completes.
            send(proc, {
                "seq": 4, "type": "request", "command": "configurationDone",
                "arguments": {},
            })
            r = recv_response(proc, "configurationDone", events, timeout=5.0)
            if not r or not r.get("success"):
                failures.append(f"configurationDone: wrong shape: {r!r}")

            # Drain events from `-exec-run`. Two legitimate outcomes:
            #   - `stopped` (breakpoint fired and the inferior is now
            #     paused — exercise stackTrace / scopes / variables
            #     while gdb still has a live frame to report on).
            #   - `terminated` (the program ran to completion without
            #     hitting the breakpoint, e.g. DWARF didn't map the
            #     requested line cleanly — that's fine, we still
            #     verify the shape of the inspection responses via
            #     the empty-stack path).
            outcome = drain_until(proc, events, ("stopped", "terminated"),
                                  timeout=10.0)
            if outcome is None:
                seen = [e.get("event") for e in events if e.get("type") == "event"]
                failures.append(
                    f"neither `stopped` nor `terminated` within 10s (saw: {seen!r})"
                )

            # Exercise the stack-inspection trio regardless of outcome:
            # the handlers must produce well-shaped JSON either way
            # (live frames when paused, empty arrays when the
            # inferior is gone). We assert shape, not values, so the
            # smoke doesn't depend on whether the breakpoint actually
            # resolved against the DWARF line table.
            send(proc, {
                "seq": 100, "type": "request", "command": "stackTrace",
                "arguments": {"threadId": 1},
            })
            r = recv_response(proc, "stackTrace", events, timeout=5.0)
            if not r:
                failures.append("stackTrace: no response")
            elif not r.get("success"):
                failures.append(f"stackTrace: success=false: {r!r}")
            else:
                body = r.get("body") or {}
                if "stackFrames" not in body or "totalFrames" not in body:
                    failures.append(f"stackTrace: missing keys: {body!r}")
                else:
                    frames = body["stackFrames"]
                    if outcome == "stopped" and len(frames) == 0:
                        # Paused at a breakpoint but gdb reported no
                        # frames — would mean either DWARF emit or
                        # MI parsing is broken. Flag it.
                        failures.append("stackTrace: stopped but empty frames")

            send(proc, {
                "seq": 101, "type": "request", "command": "scopes",
                "arguments": {"frameId": 0},
            })
            r = recv_response(proc, "scopes", events, timeout=5.0)
            if not r or not r.get("success"):
                failures.append(f"scopes: wrong shape: {r!r}")
            else:
                scopes = (r.get("body") or {}).get("scopes") or []
                if len(scopes) != 1 or scopes[0].get("name") != "Locals":
                    failures.append(f"scopes: expected one Locals scope, got {scopes!r}")
                elif (scopes[0].get("variablesReference") or 0) <= 0:
                    failures.append(f"scopes: Locals reference <=0: {scopes!r}")

            send(proc, {
                "seq": 102, "type": "request", "command": "variables",
                "arguments": {"variablesReference": 1},
            })
            r = recv_response(proc, "variables", events, timeout=5.0)
            if not r or not r.get("success"):
                failures.append(f"variables: wrong shape: {r!r}")
            else:
                body = r.get("body") or {}
                if "variables" not in body or not isinstance(body["variables"], list):
                    failures.append(f"variables: missing array: {body!r}")

        # 6) threads -> exactly one fake thread (id=1, name="main")
        send(proc, {
            "seq": 5, "type": "request", "command": "threads",
            "arguments": {},
        })
        r = recv_response(proc, "threads", events, timeout=5.0)
        if not r or not r.get("success"):
            failures.append(f"threads: success=false: {r!r}")
        elif launch_ok:
            threads = (r.get("body") or {}).get("threads") or []
            if len(threads) != 1 or threads[0].get("id") != 1:
                failures.append(f"threads: expected [{{id:1,...}}], got {threads!r}")

        # 7) An unimplemented request still returns a structured
        # "not implemented" response, not a hang. `evaluate` isn't
        # wired up in PR 4c either.
        send(proc, {
            "seq": 6, "type": "request", "command": "evaluate",
            "arguments": {"expression": "1+1", "context": "repl"},
        })
        r = recv_response(proc, "evaluate", events, timeout=5.0)
        if not r:
            failures.append("evaluate stub: no response")
        elif r.get("success") is not False:
            failures.append(f"evaluate stub: expected success=false: {r!r}")

        # 8) disconnect -> success, process exits 0
        send(proc, {
            "seq": 7, "type": "request", "command": "disconnect",
            "arguments": {},
        })
        r = recv_response(proc, "disconnect", events, timeout=5.0)
        if not r or not r.get("success"):
            failures.append(f"disconnect: wrong shape: {r!r}")

        # 9) clean exit within 5s. gdb tear-down can take ~1s, so
        # give it a slightly more generous budget than the scaffold's
        # 3s. handle_disconnect calls g_gdb.stop() before replying so
        # the gdb subprocess is gone by the time we get here.
        try:
            rc = proc.wait(timeout=5)
            if rc != 0:
                failures.append(f"adapter exit code = {rc}, expected 0")
        except subprocess.TimeoutExpired:
            failures.append("adapter did not exit within 5s after disconnect")
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
