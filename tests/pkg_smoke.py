"""Smoke test for `tulpar pkg ...`. Creates a manifest, mutates it via
the CLI, and asserts the round-tripped TOML is what we expect.

Run:
    python tests/pkg_smoke.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["tulpar.exe", "./tulpar.exe", "./tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("could not find tulpar.exe in repo root")


def run(exe: str, args: list[str], cwd: str, expect_rc: int = 0) -> str:
    p = subprocess.run([exe, "pkg", *args], cwd=cwd, capture_output=True,
                       timeout=10, text=True)
    if p.returncode != expect_rc:
        raise RuntimeError(f"`pkg {' '.join(args)}` rc={p.returncode}\n"
                            f"stderr: {p.stderr}\nstdout: {p.stdout}")
    return p.stdout


def main() -> int:
    exe = find_tulpar_exe()
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="tulpar_pkg_") as wd:
        manifest = os.path.join(wd, "tulpar.toml")

        # 1) init
        out = run(exe, ["init", "smoke-pkg"], wd)
        if "Created" not in out:
            failures.append(f"init: missing 'Created' in {out!r}")
        with open(manifest, encoding="utf-8") as f:
            text = f.read()
        if 'name = "smoke-pkg"' not in text:
            failures.append(f"init: name not in manifest: {text!r}")
        # Regression for the registry-URL default: pkg init must seed
        # the canonical hosted registry so a fresh user can
        # `pkg add demo && pkg install` without editing the manifest.
        # If the default URL changes, update the literal here AND the
        # corresponding string in src/pkg/pkg_cli.cpp::cmd_init.
        if "https://api.pkg.tulparlang.dev" not in text:
            failures.append(
                f"init: default registry URL missing from manifest: {text!r}"
            )

        # 2) init refuses to overwrite
        run(exe, ["init"], wd, expect_rc=1)

        # 3) add (new + replace)
        run(exe, ["add", "wings@^0.2.0"], wd)
        run(exe, ["add", "router"], wd)               # default version "*"
        run(exe, ["add", "wings@^0.3.0"], wd)         # replace
        out = run(exe, ["list"], wd)
        if "wings" not in out or "^0.3.0" not in out:
            failures.append(f"add+list: wings@^0.3.0 missing in {out!r}")
        if "router" not in out or "*" not in out:
            failures.append(f"add+list: router=* missing in {out!r}")

        # 4) remove
        run(exe, ["remove", "router"], wd)
        out = run(exe, ["list"], wd)
        if "router" in out:
            failures.append(f"remove: router still listed in {out!r}")

        # 5) round-trip parsing — ensure manifest is still well-formed
        run(exe, ["list"], wd)

        # 5a) Pre-release + build-metadata semver specs must round-trip
        # through `add` and `list` verbatim — the parser accepts them,
        # the manifest serialiser preserves them, and the next read
        # reproduces the same string. Regression for parse_semver
        # ignoring everything after the first non-digit (which used to
        # silently drop `-rc1` / `+ci.42` between save and re-read).
        run(exe, ["add", "alpha-pin@1.0.0-rc1"], wd)
        run(exe, ["add", "build-tagged@1.0.0+ci.42"], wd)
        run(exe, ["add", "compound@>=1.0.0-rc1,<2.0.0"], wd)
        out = run(exe, ["list"], wd)
        for needle in ("1.0.0-rc1", "1.0.0+ci.42", ">=1.0.0-rc1,<2.0.0"):
            if needle not in out:
                failures.append(
                    f"semver round-trip: '{needle}' missing in pkg list: {out!r}"
                )
        run(exe, ["remove", "alpha-pin"], wd)
        run(exe, ["remove", "build-tagged"], wd)
        run(exe, ["remove", "compound"], wd)

        # 5b) `pkg search` / `pkg info` reject missing-registry input.
        # Run from an empty subdir with no manifest and no env override
        # so the "no registry configured" branch fires; this validates
        # the CLI parse path without hitting the network.
        empty_sub = os.path.join(wd, "no_manifest")
        os.makedirs(empty_sub, exist_ok=True)
        old_env_reg = os.environ.pop("TULPAR_REGISTRY", None)
        try:
            run(exe, ["search"], empty_sub, expect_rc=1)
            run(exe, ["info", "demo"], empty_sub, expect_rc=1)
            # 5c) Missing positional arg on `pkg info`.
            run(exe, ["info"], empty_sub, expect_rc=2)
            # 5d) Unknown flag is rejected loudly.
            run(exe, ["search", "--no-such-flag"], empty_sub, expect_rc=2)
        finally:
            if old_env_reg is not None:
                os.environ["TULPAR_REGISTRY"] = old_env_reg

        # 6) install a `path:` dep + verify it's vendored.
        # Build a tiny sibling package, point the manifest at it, install.
        # Strip the registry-versioned `wings` dep from step 3 first so
        # `pkg install` stays offline — wings isn't published on the
        # hosted registry, and now that `pkg init` seeds a registry URL
        # by default, an unresolved range hits the network and fails
        # the run on CI machines without the live registry reachable.
        run(exe, ["remove", "wings"], wd)
        sibling = os.path.join(wd, "vendored_lib")
        os.makedirs(sibling, exist_ok=True)
        with open(os.path.join(sibling, "vendored_lib.tpr"), "w",
                   encoding="utf-8") as f:
            f.write('func hello() { return "ok"; }\n')
        run(exe, ["add", "vendored_lib@path:./vendored_lib"], wd)
        out = run(exe, ["install"], wd)
        if "vendored_lib" not in out or "+" not in out:
            failures.append(f"install: missing summary line in {out!r}")
        vendored = os.path.join(wd, "tulpar_modules", "vendored_lib",
                                 "vendored_lib.tpr")
        if not os.path.exists(vendored):
            failures.append(f"install: vendored entry missing at {vendored}")

    if failures:
        print("FAIL:")
        for f in failures:
            print(" -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
