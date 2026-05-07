"""LSP smoke test for `tulpar --lsp`.

Drives the server through a minimal initialize/didOpen/didChange/shutdown
sequence and asserts that diagnostics flow as expected. Run with:

    python tests/lsp_smoke.py

Exits non-zero on any failure. Designed to be cheap enough to invoke from
CI on every push but currently a manual harness — wire into run_tests.ps1
once it stabilises.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time


def frame(body: str) -> bytes:
    encoded = body.encode("utf-8")
    return f"Content-Length: {len(encoded)}\r\n\r\n".encode("ascii") + encoded


def read_msg(p: subprocess.Popen) -> dict:
    # Read headers
    line = b""
    headers = {}
    while True:
        ch = p.stdout.read(1)
        if not ch:
            raise RuntimeError("server closed stdout while reading headers")
        if ch == b"\r":
            nxt = p.stdout.read(1)
            if nxt != b"\n":
                line += ch + nxt
                continue
            if not line:
                break  # blank line = end of headers
            k, _, v = line.decode("ascii").partition(":")
            headers[k.strip().lower()] = v.strip()
            line = b""
        else:
            line += ch
    n = int(headers["content-length"])
    # `read(n)` on a pipe is "up to n bytes" — for a large body the pipe
    # buffer may deliver it in chunks. Loop until we have the full body.
    body = b""
    while len(body) < n:
        chunk = p.stdout.read(n - len(body))
        if not chunk:
            break
        body += chunk
    if len(body) != n:
        # Server died mid-write or framing was wrong — fall back to error
        # state so the caller can drain stderr for diagnostics.
        with open("lsp_smoke_bad_body.txt", "wb") as f:
            f.write(body)
        raise RuntimeError(
            f"short read: Content-Length said {n} but pipe gave {len(body)} bytes")
    try:
        return json.loads(body.decode("utf-8"))
    except json.JSONDecodeError:
        with open("lsp_smoke_bad_body.txt", "wb") as f:
            f.write(body)
        print(f"!!! malformed body of {n} bytes saved to lsp_smoke_bad_body.txt")
        print("!!! tail (last 200 bytes):", body[-200:])
        raise


def send(p: subprocess.Popen, msg: dict) -> None:
    p.stdin.write(frame(json.dumps(msg)))
    p.stdin.flush()


def find_tulpar_exe() -> str:
    # `TULPAR_EXE=...` overrides everything (used when running against a
    # freshly-built binary while the in-use copy is locked by an editor's
    # LSP child). Otherwise pick the first candidate that exists.
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    candidates = ["tulpar.exe", "./tulpar.exe", "./tulpar"]
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def main() -> int:
    exe = find_tulpar_exe()
    p = subprocess.Popen(
        [exe, "--lsp"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )

    failures: list[str] = []

    try:
        # 1. initialize
        send(p, {"jsonrpc": "2.0", "id": 1, "method": "initialize",
                 "params": {"processId": None, "rootUri": None, "capabilities": {}}})
        resp = read_msg(p)
        if resp.get("id") != 1:
            failures.append(f"initialize: bad id {resp!r}")
        if "capabilities" not in resp.get("result", {}):
            failures.append(f"initialize: missing capabilities {resp!r}")
        else:
            print("initialize OK:", json.dumps(resp["result"]["capabilities"]))

        # 2. initialized (notification)
        send(p, {"jsonrpc": "2.0", "method": "initialized", "params": {}})

        # 3. didOpen with valid file
        good = 'print("hello");\n'
        send(p, {"jsonrpc": "2.0", "method": "textDocument/didOpen",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr",
                                              "languageId": "tulpar",
                                              "version": 1, "text": good}}})
        resp = read_msg(p)
        if resp.get("method") != "textDocument/publishDiagnostics":
            failures.append(f"didOpen: expected publishDiagnostics, got {resp!r}")
        else:
            diags = resp["params"]["diagnostics"]
            if diags:
                failures.append(f"didOpen good: expected 0 diags, got {diags!r}")
            else:
                print("didOpen good OK: 0 diagnostics")

        # 4. didChange with broken file (call to undefined function)
        bad = 'noSuchFunction();\n'
        send(p, {"jsonrpc": "2.0", "method": "textDocument/didChange",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr", "version": 2},
                            "contentChanges": [{"text": bad}]}})
        resp = read_msg(p)
        if resp.get("method") != "textDocument/publishDiagnostics":
            failures.append(f"didChange: expected publishDiagnostics, got {resp!r}")
        else:
            diags = resp["params"]["diagnostics"]
            if not diags:
                failures.append("didChange bad: expected ≥1 diagnostic, got none")
            else:
                d = diags[0]
                print(f"didChange bad OK: {len(diags)} diag(s), first = "
                      f"line={d['range']['start']['line']} "
                      f"col={d['range']['start']['character']} "
                      f"sev={d['severity']} "
                      f"msg={d['message']!r}")

        # 5a. Hover on a user-defined function. didChange first to load
        # a known program, then ask for hover on the function name.
        # Tulpar's parameter syntax is `<type> <name>` (Pascal-ish), NOT
        # `<name>: <type>` — so the index test program reflects that.
        program = (
            "// Selamlama mesajı dönen yardımcı.\n"
            "func greet(str name): str {\n"
            "    return \"Merhaba, \" + name;\n"
            "}\n"
            "print(greet(\"Hamza\"));\n"
        )
        send(p, {"jsonrpc": "2.0", "method": "textDocument/didChange",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr", "version": 5},
                            "contentChanges": [{"text": program}]}})
        diag = read_msg(p)  # publishDiagnostics — expect 0 diagnostics
        if diag.get("method") != "textDocument/publishDiagnostics" or diag["params"]["diagnostics"]:
            failures.append(f"hover-prep: expected clean publishDiagnostics, got {diag!r}")

        # `func greet` is on line 1 (0-based). The identifier starts at
        # column 5 (`func ` + name).
        send(p, {"jsonrpc": "2.0", "id": 10, "method": "textDocument/hover",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 1, "character": 6}}})
        resp = read_msg(p)
        if resp.get("id") != 10 or resp.get("result") is None:
            failures.append(f"hover user-func: bad response {resp!r}")
        else:
            md = resp["result"]["contents"]["value"]
            if "func greet" not in md:
                failures.append(f"hover user-func: signature missing in {md!r}")
            elif "Selamlama" not in md:
                failures.append(f"hover user-func: leading comment missing in {md!r}")
            else:
                print("hover user-func OK")

        # 5b. Hover on a builtin (`print`).
        send(p, {"jsonrpc": "2.0", "id": 11, "method": "textDocument/hover",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 4, "character": 1}}})
        resp = read_msg(p)
        if resp.get("id") != 11 or resp.get("result") is None:
            failures.append(f"hover builtin: bad response {resp!r}")
        else:
            md = resp["result"]["contents"]["value"]
            if "print(" not in md or "builtin" not in md:
                failures.append(f"hover builtin: bad markdown {md!r}")
            else:
                print("hover builtin OK")

        # 5c. Completion request — expect at least the user function + a few
        # builtins + keywords.
        send(p, {"jsonrpc": "2.0", "id": 12, "method": "textDocument/completion",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 4, "character": 0}}})
        resp = read_msg(p)
        if resp.get("id") != 12:
            failures.append(f"completion: bad id {resp!r}")
        else:
            items = resp["result"]["items"]
            labels = {i["label"] for i in items}
            need = {"greet", "print", "len", "func", "if", "return"}
            missing = need - labels
            if missing:
                failures.append(f"completion: missing labels {missing}")
            else:
                print(f"completion OK: {len(items)} items ({len(need)} required all present)")

        # 5d. Definition request on the call to `greet`. Line 4 is
        # `print(greet("Hamza"));` — `greet` starts at col 6.
        send(p, {"jsonrpc": "2.0", "id": 13, "method": "textDocument/definition",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 4, "character": 7}}})
        resp = read_msg(p)
        if resp.get("id") != 13 or resp.get("result") is None:
            failures.append(f"definition: bad response {resp!r}")
        else:
            loc = resp["result"]
            ln = loc["range"]["start"]["line"]
            if ln != 1:
                failures.append(f"definition: expected line=1, got {ln} ({loc!r})")
            else:
                print("definition OK")

        # 5e. Find references on `greet`. Should list both the declaration
        # (line 1) and the call site (line 4) when includeDeclaration=true.
        send(p, {"jsonrpc": "2.0", "id": 14, "method": "textDocument/references",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 4, "character": 7},
                            "context": {"includeDeclaration": True}}})
        resp = read_msg(p)
        if resp.get("id") != 14 or not isinstance(resp.get("result"), list):
            failures.append(f"references: bad response {resp!r}")
        else:
            locs = resp["result"]
            lines = sorted({l["range"]["start"]["line"] for l in locs})
            if 1 not in lines or 4 not in lines:
                failures.append(f"references: missing declaration or call "
                                f"({lines!r})")
            else:
                print(f"references OK ({len(locs)} location(s) at lines {lines})")

        # 5f. Rename `greet` to `say_hi`. Expect a WorkspaceEdit with two
        # text edits (declaration + call) on the same uri.
        send(p, {"jsonrpc": "2.0", "id": 15, "method": "textDocument/rename",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 4, "character": 7},
                            "newName": "say_hi"}})
        resp = read_msg(p)
        if resp.get("id") != 15 or resp.get("result") is None:
            failures.append(f"rename: bad response {resp!r}")
        else:
            edits_by_uri = resp["result"].get("changes", {})
            file_edits = edits_by_uri.get("file:///tmp/good.tpr", [])
            new_names = {e["newText"] for e in file_edits}
            if new_names != {"say_hi"}:
                failures.append(f"rename: bad newText payloads {new_names!r}")
            elif len(file_edits) < 2:
                failures.append(f"rename: expected ≥2 edits, got {len(file_edits)}")
            else:
                print(f"rename OK ({len(file_edits)} edit(s) -> 'say_hi')")

        # 5g. signatureHelp — load a program with a clearly-positioned call
        # and ask for parameter info while the cursor is inside the parens.
        sig_program = (
            "func greet(str name): str {\n"   # line 0
            "    return \"hi, \" + name;\n"
            "}\n"
            "func add(int a, int b): int {\n"  # line 3
            "    return a + b;\n"
            "}\n"
            "print(greet(\"Hamza\"));\n"        # line 6: greet "(" at col 11
            "print(add(1, 2));\n"               # line 7: add "(" at col 9
        )
        send(p, {"jsonrpc": "2.0", "method": "textDocument/didChange",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr", "version": 6},
                            "contentChanges": [{"text": sig_program}]}})
        diag = read_msg(p)
        if diag.get("method") != "textDocument/publishDiagnostics" or \
                diag["params"]["diagnostics"]:
            failures.append(f"sig-help-prep: expected clean diags, got {diag!r}")

        # Cursor right after greet's `(` — inside its first param.
        send(p, {"jsonrpc": "2.0", "id": 20, "method": "textDocument/signatureHelp",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 6, "character": 12}}})
        resp = read_msg(p)
        if resp.get("id") != 20 or resp.get("result") is None:
            failures.append(f"signatureHelp greet: bad response {resp!r}")
        else:
            sigs = resp["result"]["signatures"]
            if not sigs or "greet" not in sigs[0]["label"]:
                failures.append(f"signatureHelp greet: bad label {sigs!r}")
            elif resp["result"]["activeParameter"] != 0:
                failures.append(
                    f"signatureHelp greet: expected activeParameter=0, "
                    f"got {resp['result']['activeParameter']}")
            else:
                print(f"signatureHelp user-func OK: {sigs[0]['label']!r} (active=0)")

        # Cursor after `1, ` in add(1, |2) — should report activeParameter=1.
        send(p, {"jsonrpc": "2.0", "id": 21, "method": "textDocument/signatureHelp",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 7, "character": 13}}})
        resp = read_msg(p)
        if resp.get("id") != 21 or resp.get("result") is None:
            failures.append(f"signatureHelp add active=1: bad response {resp!r}")
        else:
            ap = resp["result"]["activeParameter"]
            if ap != 1:
                failures.append(
                    f"signatureHelp add active=1: expected 1, got {ap}")
            else:
                print("signatureHelp active-param tracking OK (commas counted)")

        # Cursor inside print(...) — `print` is a builtin, so the catalog
        # entry should drive the response.
        send(p, {"jsonrpc": "2.0", "id": 22, "method": "textDocument/signatureHelp",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr"},
                            "position": {"line": 6, "character": 6}}})
        resp = read_msg(p)
        if resp.get("id") != 22 or resp.get("result") is None:
            failures.append(f"signatureHelp builtin: bad response {resp!r}")
        else:
            sigs = resp["result"]["signatures"]
            if not sigs or "print" not in sigs[0]["label"]:
                failures.append(f"signatureHelp builtin: bad label {sigs!r}")
            else:
                print(f"signatureHelp builtin OK: {sigs[0]['label']!r}")

        # 6. didChange with parse error (missing semicolon → expected ';')
        parse_bad = 'int x = 1\nint y = 2;\n'
        send(p, {"jsonrpc": "2.0", "method": "textDocument/didChange",
                 "params": {"textDocument": {"uri": "file:///tmp/good.tpr", "version": 3},
                            "contentChanges": [{"text": parse_bad}]}})
        resp = read_msg(p)
        if resp.get("method") != "textDocument/publishDiagnostics":
            failures.append(f"didChange parse: expected publishDiagnostics, got {resp!r}")
        else:
            diags = resp["params"]["diagnostics"]
            if not diags:
                failures.append("didChange parse: expected ≥1 parse diagnostic, got none")
            else:
                d = diags[0]
                print(f"didChange parse OK: {len(diags)} diag(s), first = "
                      f"line={d['range']['start']['line']} "
                      f"col={d['range']['start']['character']} "
                      f"sev={d['severity']}")

        # 6. shutdown / exit
        send(p, {"jsonrpc": "2.0", "id": 99, "method": "shutdown"})
        resp = read_msg(p)
        if resp.get("id") != 99:
            failures.append(f"shutdown: bad id {resp!r}")
        send(p, {"jsonrpc": "2.0", "method": "exit"})

        rc = p.wait(timeout=5)
        if rc != 0:
            failures.append(f"exit: rc={rc}")

    finally:
        # Drain stderr BEFORE terminating so we see any diagnostics the
        # server already wrote. We give the process a moment to flush, then
        # tear it down.
        if p.poll() is not None:
            err = p.stderr.read().decode("utf-8", errors="replace")
            if err.strip():
                print("--- server stderr ---")
                print(err)
        else:
            p.terminate()
            try:
                p.wait(timeout=2)
            except subprocess.TimeoutExpired:
                p.kill()
            err = p.stderr.read().decode("utf-8", errors="replace")
            if err.strip():
                print("--- server stderr ---")
                print(err)

    if failures:
        print("FAIL:")
        for f in failures:
            print("  -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
