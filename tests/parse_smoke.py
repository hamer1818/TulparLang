"""Smoke test for parser multi-error recovery + duplicate-emission fix.

Runs `tulpar --aot` against a deliberately broken source with three
missing-semicolon errors and verifies that:

1. All three errors are reported (not just the first one — the parser
   used to bail at EOF when no `;` sentinel was found).
2. Each error appears exactly once (typeinfer used to pretty-print
   parse errors before the AOT pipeline re-parsed and printed them
   again — see PR ???).

Run with:

    python tests/parse_smoke.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile


# Three statements without trailing semicolons. Each should produce one
# parse error. Without keyword-sentinel recovery, only the first ever
# surfaced — the panic-mode walker ate the rest of the file looking for
# a `;` that never came.
BROKEN_SOURCE = (
    "int x = 1\n"
    "int y = 2\n"
    "int z = 3\n"
)


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def strip_ansi(s: str) -> str:
    # Diagnostics use ANSI colour codes for terminal output; smoke tests
    # care about content not styling.
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def main() -> int:
    exe = find_tulpar_exe()

    with tempfile.NamedTemporaryFile("w", suffix=".tpr", delete=False,
                                      encoding="utf-8") as f:
        f.write(BROKEN_SOURCE)
        path = f.name
    try:
        # `--aot` is the path that runs the typeinfer pre-pass + the
        # real parse, which is exactly the path that historically
        # double-reported.
        proc = subprocess.run(
            [exe, "--aot", path],
            capture_output=True,
            text=True,
            timeout=15,
            encoding="utf-8",
            errors="replace",
        )
    finally:
        os.unlink(path)

    # AOT must fail (exit non-zero) on any parse error.
    if proc.returncode == 0:
        print("FAIL: tulpar exited 0 on a parse-broken source")
        print("--- stdout ---")
        print(proc.stdout)
        print("--- stderr ---")
        print(proc.stderr)
        return 1

    err = strip_ansi(proc.stderr + proc.stdout)

    # Match every "ayrıştırma hatası" / "parse error" line. Each
    # occurrence is one diagnostic.
    diag_lines = [
        ln for ln in err.splitlines()
        if re.search(r"(ayr[iı]?[şs]?t[iı]rma hatas[iı]|parse error)", ln,
                     re.IGNORECASE)
    ]

    failures = []

    # Multi-error: all three statements should produce diagnostics.
    if len(diag_lines) < 3:
        failures.append(
            f"multi-error: expected ≥3 parse diagnostics, got {len(diag_lines)}"
        )

    # No duplicate: count distinct (line, message) pairs from the
    # `--> filename:N` markers. Each statement should map to exactly
    # one location marker — pre-fix, every statement got two ((stdin)
    # + filename).
    # `-->` lines can carry Windows paths with drive letters (`C:\...`),
    # so we anchor on the trailing `:NN` and capture everything before
    # it on the same line as the filename.
    location_lines = re.findall(r"-->\s*(.+?):(\d+)\s*$", err, re.MULTILINE)
    location_counts: dict[tuple[str, str], int] = {}
    for loc in location_lines:
        location_counts[loc] = location_counts.get(loc, 0) + 1

    duplicated = [k for k, v in location_counts.items() if v > 1]
    if duplicated:
        failures.append(
            f"duplicate-emission: same (file, line) reported >1 time: "
            f"{duplicated}"
        )

    # Line-number recovery: the three errors should land on lines 1, 2,
    # 3 (where the user actually forgot the `;`), not 2, 3, 4 (where the
    # parser's cursor was when it noticed the gap). This is a separate
    # `expect()` fix from the multi-error recovery — easy to regress
    # accidentally if someone reverts to `current().line()` for the
    # error report.
    reported_lines = sorted({int(n) for (_, n) in location_lines})
    expected_lines = [1, 2, 3]
    if reported_lines != expected_lines:
        failures.append(
            f"line-recovery: expected errors at lines {expected_lines}, "
            f"got {reported_lines}"
        )

    # Sanity: at least one of the location markers should match the
    # tempfile we just wrote (rules out a typeinfer-only path that
    # reports `(stdin)` while the filename context is unset).
    if location_lines:
        unique_files = {f for (f, _) in location_lines}
        if all(f == "(stdin)" for f in unique_files):
            failures.append(
                f"filename-context: every location marker is `(stdin)`, "
                f"expected at least one with the actual filename "
                f"({unique_files!r})"
            )

    if failures:
        print("FAIL:")
        for f in failures:
            print("  -", f)
        print("--- stderr ---")
        print(proc.stderr)
        return 1

    print(f"parse multi-error OK ({len(diag_lines)} distinct diagnostics, "
          f"{len(location_counts)} unique locations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
