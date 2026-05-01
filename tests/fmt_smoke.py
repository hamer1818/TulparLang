"""Smoke test for `tulpar fmt` — checks indentation, trailing whitespace,
final newline, and idempotency. Run with:

    python tests/fmt_smoke.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile


SAMPLE = (
    "// Demo unformatted input.\n"
    "func   add(int a,int b):int {\n"
    "return a+b ;\n"
    "   }\n"
    "\n"
    "\n"
    "func main() {\n"
    "        int x = 1;\n"
    "   if (x > 0) {\n"
    # `pozitif,  ok` has two spaces — the formatter must NOT touch it
    "print(\"pozitif,  ok\");\n"
    "       } else {\n"
    "   print(\"negatif\");\n"
    "}\n"
    "}\n"
)


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def run_fmt(exe: str, source: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".tpr", delete=False,
                                      encoding="utf-8") as f:
        f.write(source)
        path = f.name
    try:
        out = subprocess.check_output([exe, "fmt", path], timeout=10)
        return out.decode("utf-8")
    finally:
        os.unlink(path)


def main() -> int:
    exe = find_tulpar_exe()
    failures: list[str] = []

    formatted = run_fmt(exe, SAMPLE)

    # Check 1: no trailing whitespace on any line.
    for i, line in enumerate(formatted.split("\n")):
        if line.endswith(" ") or line.endswith("\t"):
            failures.append(f"line {i+1} has trailing whitespace: {line!r}")

    # Check 2: ends with exactly one newline.
    if not formatted.endswith("\n"):
        failures.append("file does not end with newline")
    elif formatted.endswith("\n\n"):
        failures.append("file ends with multiple newlines")

    # Check 3: closing braces align with their opener.
    lines = formatted.rstrip("\n").split("\n")
    # Find a `}` at the end of a function — should have no leading
    # whitespace if it closes a top-level function.
    found_top_close = False
    for line in lines:
        if line == "}":
            found_top_close = True
            break
    if not found_top_close:
        failures.append("no top-level closing `}` found at column 0")

    # Check 4: 4-space indent inside the top-level functions.
    if not any(line.startswith("    ") and "return" in line for line in lines):
        failures.append("expected `    return` inside add()")

    # Check 5: idempotent — formatting the formatted source returns the same.
    once = run_fmt(exe, formatted)
    if once != formatted:
        failures.append("not idempotent (running fmt twice produced different output)")

    # Check 6: blank-line runs collapsed.
    if "\n\n\n" in formatted:
        failures.append("triple newline (run of 2+ blank lines) survived formatting")

    # Check 7: token pass — `int a,int b` becomes `int a, int b`,
    # `a+b` becomes `a + b`, `:int` becomes `: int`, `b ;` -> `b;`.
    if "int a, int b" not in formatted:
        failures.append("token pass: `int a, int b` not produced")
    if "a + b" not in formatted:
        failures.append("token pass: `a + b` not produced")
    if "): int " not in formatted:
        failures.append("token pass: `: int` (return type) not produced")
    if "b ;" in formatted:
        failures.append("token pass: stray ` ;` not stripped")

    # Check 8: string content preserved verbatim — the `,  ` (two spaces)
    # inside `"pozitif,  ok"` must NOT have been collapsed.
    if "pozitif,  ok" not in formatted:
        failures.append("string-content normalised (must not be touched)")

    if failures:
        print("FAIL:")
        for f in failures:
            print("  -", f)
        print()
        print("--- formatter output ---")
        print(formatted)
        return 1
    print("ALL CHECKS PASSED")
    print()
    print("--- formatter output ---")
    print(formatted)
    return 0


if __name__ == "__main__":
    sys.exit(main())
