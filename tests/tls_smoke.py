"""End-to-end TLS smoke for Tulpar's `http_client` library.

Verifies the OpenSSL-gated TLS path is actually wired up.

On Linux/macOS: builds a tiny program that calls
`http_get("https://example.com")`, runs it, and asserts the response is
2xx with a non-empty body.

On Windows: only builds the program (no runtime probe). MinGW exes
launched via Python's subprocess have a pending issue where the TLS
handshake hangs even though `tulpar build foo.tpr && ./foo.exe` from
PowerShell completes in <500ms with a clean 200. The build-only check
still catches the most common regression class — link-time mismatches
between the AOT pipeline and the OpenSSL static archive (e.g. the
ws2_32-before-libssl ordering bug fixed in PR #92). Manual probe:

    .\\tulpar.exe build tests\\fixtures\\tls_probe.tpr __tls_probe
    .\\__tls_probe.exe

Skips itself (exit 0 with a SKIP message) when:

- The current `tulpar` was built without OpenSSL (TLS-disabled is a
  legitimate config; we don't want green CI to mandate OpenSSL).
- (Linux/macOS only) the host has no internet egress.

Run with:

    python tests/tls_smoke.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile


PROBE_PROGRAM = """\
import "http_client";

func main() {
    json r = http_get("https://example.com");
    print("OK=" + toString(r["ok"]));
    if (length(toString(r["status"])) > 0) {
        print("STATUS=" + toString(r["status"]));
    }
    if (length(toString(r["error"])) > 0) {
        print("ERROR=" + toString(r["error"]));
    }
    if (r["ok"] == 1) {
        print("BODY_LEN=" + toString(length(r["body"])));
    }
}

main();
"""


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def build_probe(exe: str, src_path: str, bin_base: str) -> tuple[int, str, str]:
    """Build the TLS probe. Returns (rc, stdout, stderr)."""
    proc = subprocess.run(
        [exe, "build", src_path, bin_base],
        capture_output=True,
        text=True,
        timeout=60,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout or "", proc.stderr or ""


def binary_links_tls(bin_path: str) -> bool:
    """Check that the produced binary actually has OpenSSL symbols
    statically linked. Without TLS the http_client builtin compiles
    fine but returns `{ok: 0, error: "TLS not compiled in"}` at
    runtime — the build-time check is whether the runtime archive
    emitted into the exe pulled libssl/libcrypto in.

    We grep for any of a handful of stable OpenSSL strings. False
    negatives are possible (compiler stripped strings) but in practice
    the SSL_CTX_new error messages stay around through O3. False
    positives basically can't happen — these strings are unique to
    OpenSSL.
    """
    try:
        with open(bin_path, "rb") as f:
            blob = f.read()
    except OSError:
        return False
    # Pick stable, OpenSSL-only markers. Any one match is enough.
    needles = (
        b"OpenSSL",
        b"SSL_CTX_new",
        b"OPENSSL_init_ssl",
        b"TLS_client_method",
    )
    return any(n in blob for n in needles)


def main() -> int:
    exe = find_tulpar_exe()
    cwd = os.getcwd()
    src_path = os.path.join(cwd, "__tls_probe.tpr")
    bin_base = os.path.join(cwd, "__tls_probe")
    bin_path = bin_base + (".exe" if sys.platform.startswith("win") else "")

    with open(src_path, "w", encoding="utf-8") as f:
        f.write(PROBE_PROGRAM)

    try:
        rc, build_out, build_err = build_probe(exe, src_path, bin_base)
        if rc != 0 or not os.path.exists(bin_path):
            print("FAIL: build failed")
            print(build_out[-1500:])
            print(build_err[-1500:])
            return 1

        # Build-time check: do we have OpenSSL statically linked? If not,
        # this is a TLS-disabled build (still legit) — emit SKIP.
        if not binary_links_tls(bin_path):
            print("SKIP: tulpar built without OpenSSL "
                  "(no SSL_* markers in produced binary)")
            return 0

        # Windows: stop here. Manual verification recipe is in the
        # docstring above.
        if sys.platform.startswith("win"):
            print("tls_smoke OK (build-only on Windows): probe binary "
                  "links libssl/libcrypto. Run manually for runtime "
                  "verification.")
            return 0

        # Linux/macOS: actually run the probe. If the network is dead
        # we skip rather than fail.
        try:
            run = subprocess.run(
                [bin_path],
                capture_output=True,
                text=True,
                timeout=20,
                encoding="utf-8",
                errors="replace",
            )
        except subprocess.TimeoutExpired:
            print("FAIL: probe binary timed out (20s) — TLS handshake hung")
            return 1

        out = (run.stdout or "") + "\n" + (run.stderr or "")
        kv: dict[str, str] = {}
        for line in out.splitlines():
            if "=" in line and not line.startswith("[AOT"):
                k, _, v = line.partition("=")
                kv[k.strip()] = v.strip()

        ok = kv.get("OK", "")
        status = kv.get("STATUS", "")
        error = kv.get("ERROR", "")
        body_len = kv.get("BODY_LEN", "")

        if ok == "0" and "TLS not compiled in" in error:
            print(f"SKIP: tulpar built without OpenSSL ({error!r})")
            return 0
        if ok == "0" and any(
            tok in error.lower()
            for tok in ("connect", "resolve", "timeout", "host")
        ):
            print(f"SKIP: no network or host unreachable ({error!r})")
            return 0
        if ok != "1":
            print(f"FAIL: ok={ok!r}, status={status!r}, error={error!r}")
            print(out[-1000:])
            return 1
        if status != "200":
            print(f"FAIL: expected status=200, got {status!r}")
            return 1
        try:
            n = int(body_len)
        except ValueError:
            n = 0
        if n <= 0:
            print(f"FAIL: empty body (BODY_LEN={body_len!r})")
            return 1

        print(f"tls_smoke OK: https://example.com -> 200, body {n} bytes")
        return 0
    finally:
        for p in [src_path, bin_path,
                  bin_base + ".ll", bin_base + ".o",
                  bin_base + ".stdout", bin_base + ".stderr"]:
            try:
                os.unlink(p)
            except OSError:
                pass


if __name__ == "__main__":
    sys.exit(main())
