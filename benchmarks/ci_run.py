#!/usr/bin/env python3
"""CI benchmark runner.

End-to-end orchestrator invoked by `.github/workflows/bench.yml` after
every push to `main`. Builds the comparator binaries (C, Rust, Go,
Java), AOT-compiles the Tulpar variants, runs each benchmark
`REPEATS` times with `time.perf_counter()` (best wall time wins), and
runs the HTTP throughput suite via the existing `http_bench.py`
helpers. Output is two files:

  - benchmarks/RESULTS.json — machine-readable, full result set
  - benchmarks/RESULTS.md   — human-readable; only the auto-managed
    section between the BENCH:* markers is rewritten — prose context
    below the markers is preserved verbatim

The goal is that no human ever hand-edits the perf numbers in
RESULTS.md or README.md again. CI runs, regenerates, commits.

Usage:

    python3 benchmarks/ci_run.py [--repeats 5] [--http-requests 3000]
                                 [--http-connections 4] [--no-http]
                                 [--no-java]

Toolchain lookups are best-effort: if `rustc` or `go` is missing, that
language is silently skipped — the run does not fail. Tulpar AOT and
the C path are required (anything else is comparison context).
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

# Make sibling http_bench module importable regardless of cwd.
HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))

import http_bench  # noqa: E402

EXE_SUFFIX = ".exe" if sys.platform == "win32" else ""
TULPAR_EXE = REPO / ("tulpar" + EXE_SUFFIX)

# ----------------------------------------------------------------------
# Schema version. Bump when the JSON shape changes incompatibly so
# update_readme.py can fail loudly on stale schema instead of writing
# malformed tables.
# ----------------------------------------------------------------------
SCHEMA_VERSION = 1


# ----------------------------------------------------------------------
# Toolchain discovery. We don't rely on bool(shutil.which) alone — we
# also try `--version` so e.g. a stub `rustc` shim that fails on first
# invocation gets correctly classified as unavailable.
# ----------------------------------------------------------------------
def have(cmd: str, version_arg: str = "--version") -> bool:
    if not shutil.which(cmd):
        return False
    try:
        subprocess.run([cmd, version_arg],
                       capture_output=True, timeout=10, check=False)
        return True
    except (subprocess.SubprocessError, OSError):
        return False


def detect_runner_info() -> dict:
    """Capture the runner identity so we can show it in RESULTS.md."""
    info = {
        "os": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "cpu_count": os.cpu_count() or 0,
    }
    # Linux: /proc/cpuinfo has the model name.
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        try:
            for line in cpuinfo.read_text().splitlines():
                if line.startswith("model name"):
                    info["cpu_model"] = line.split(":", 1)[1].strip()
                    break
        except OSError:
            pass
    # macOS: sysctl
    if info["os"] == "Darwin":
        try:
            r = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                               capture_output=True, text=True, timeout=5)
            if r.returncode == 0:
                info["cpu_model"] = r.stdout.strip()
        except (subprocess.SubprocessError, OSError):
            pass
    return info


def git_sha() -> str:
    try:
        r = subprocess.run(["git", "rev-parse", "HEAD"],
                           cwd=REPO, capture_output=True, text=True, timeout=5)
        if r.returncode == 0:
            return r.stdout.strip()
    except (subprocess.SubprocessError, OSError):
        pass
    return os.environ.get("GITHUB_SHA", "unknown")


# ----------------------------------------------------------------------
# Build comparator binaries. Each builder is best-effort; failure is
# logged and the language drops out of the result table.
# ----------------------------------------------------------------------
def build_natives(skip_java: bool) -> dict[str, bool]:
    """Return {language: built_ok} so the run loop can skip missing langs."""
    built = {"c": False, "rust": False, "go": False, "java": False,
             "node": False, "python": False, "tulpar_aot": False,
             "tulpar_vm": False}

    # C — required.
    if have("gcc"):
        try:
            subprocess.run(["gcc", "-O2", "benchmarks/fib.c", "-o", f"benchmarks/fib_c{EXE_SUFFIX}"],
                           cwd=REPO, check=True, timeout=60)
            subprocess.run(["gcc", "-O2", "benchmarks/loopsum.c", "-o", f"benchmarks/loopsum_c{EXE_SUFFIX}"],
                           cwd=REPO, check=True, timeout=60)
            built["c"] = True
        except subprocess.SubprocessError as e:
            print(f"[bench] gcc build failed: {e}", file=sys.stderr)

    # Rust.
    if have("rustc"):
        try:
            subprocess.run(["rustc", "-C", "opt-level=3", "benchmarks/fib.rs",
                            "-o", f"benchmarks/fib_rs{EXE_SUFFIX}"],
                           cwd=REPO, check=True, timeout=120,
                           stderr=subprocess.DEVNULL)
            subprocess.run(["rustc", "-C", "opt-level=3", "benchmarks/loopsum.rs",
                            "-o", f"benchmarks/loopsum_rs{EXE_SUFFIX}"],
                           cwd=REPO, check=True, timeout=120,
                           stderr=subprocess.DEVNULL)
            built["rust"] = True
        except subprocess.SubprocessError as e:
            print(f"[bench] rustc build failed: {e}", file=sys.stderr)

    # Go.
    if have("go", "version"):
        try:
            subprocess.run(["go", "build", "-o", f"benchmarks/fib_go{EXE_SUFFIX}",
                            "benchmarks/fib.go"],
                           cwd=REPO, check=True, timeout=120)
            subprocess.run(["go", "build", "-o", f"benchmarks/loopsum_go{EXE_SUFFIX}",
                            "benchmarks/loopsum.go"],
                           cwd=REPO, check=True, timeout=120)
            built["go"] = True
        except subprocess.SubprocessError as e:
            print(f"[bench] go build failed: {e}", file=sys.stderr)

    # Java.
    if not skip_java and have("javac"):
        try:
            subprocess.run(["javac", "fib.java", "loopsum.java"],
                           cwd=REPO / "benchmarks", check=True, timeout=120)
            built["java"] = True
        except subprocess.SubprocessError as e:
            print(f"[bench] javac failed: {e}", file=sys.stderr)

    # Node, Python — interpreters, no compile step. We just check
    # they're installed.
    built["node"] = have("node", "--version")
    built["python"] = True  # we're running in Python

    # Tulpar AOT — required.
    if not TULPAR_EXE.exists():
        raise SystemExit(f"[bench] tulpar binary not found at {TULPAR_EXE}")

    for name in ("loopsum", "fib"):
        # Wipe stale .o so a previous failed build doesn't poison the
        # next run. The AOT pipeline writes <stem>.o intermediate +
        # <stem>(.exe) final.
        for stale in REPO.glob(f"benchmarks/{name}_tulpar*"):
            if stale.suffix in {".o", ".exe", ""}:
                try:
                    stale.unlink()
                except OSError:
                    pass
        try:
            subprocess.run([str(TULPAR_EXE), "build",
                            f"benchmarks/{name}.tpr",
                            f"benchmarks/{name}_tulpar"],
                           cwd=REPO, check=True, timeout=120,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as e:
            err = (e.stderr or b"").decode("utf-8", "replace")
            print(f"[bench] tulpar AOT build failed for {name}: {err}",
                  file=sys.stderr)
            raise
    built["tulpar_aot"] = True
    built["tulpar_vm"] = True

    return built


# ----------------------------------------------------------------------
# CPU benchmark execution.
# ----------------------------------------------------------------------
def best_of(cmd: list[str], repeats: int, env: dict | None = None) -> tuple[float, str]:
    """Run `cmd` `repeats` times, returning (best_ms, last_stdout_line).

    Wall time is `time.perf_counter` deltas around `subprocess.run` —
    same shape every language pays. We don't try to subtract process
    startup; for short runs (< 100 ms) Python and Java are penalized
    by their own startup cost, which is part of the workload we
    actually want to measure.
    """
    best_ms: float | None = None
    last_out = ""
    for _ in range(repeats):
        t0 = time.perf_counter()
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=180, env=env, check=False)
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        if r.returncode != 0:
            # Don't pollute the best-of with a crashed run, but keep
            # stderr visible for debugging.
            print(f"[bench] non-zero exit {r.returncode} from {cmd!r}: "
                  f"{(r.stderr or '').strip()[:200]}", file=sys.stderr)
            continue
        if best_ms is None or elapsed_ms < best_ms:
            best_ms = elapsed_ms
            stdout_lines = (r.stdout or "").strip().splitlines()
            last_out = stdout_lines[-1] if stdout_lines else ""
    if best_ms is None:
        raise RuntimeError(f"every invocation of {cmd!r} failed")
    return round(best_ms, 1), last_out


def cpu_table(built: dict[str, bool], repeats: int) -> list[dict]:
    """Build the CPU benchmark result table, language-by-language.

    Each variant entry: (group, language, command-list-or-None).
    A None command means the language wasn't built; we emit a
    placeholder row so the README still shows the column. We always
    skip rows where the toolchain is missing entirely.
    """
    pf = lambda *p: str(REPO.joinpath(*p))

    plan: list[tuple[str, str, list[str] | None]] = [
        # loopsum group
        ("loopsum", "Tulpar AOT (LLVM)",
         [pf("benchmarks", f"loopsum_tulpar{EXE_SUFFIX}")] if built["tulpar_aot"] else None),
        ("loopsum", "Tulpar VM",
         [str(TULPAR_EXE), "--vm", "benchmarks/loopsum.tpr"] if built["tulpar_vm"] else None),
        ("loopsum", "C (gcc -O2)",
         [pf("benchmarks", f"loopsum_c{EXE_SUFFIX}")] if built["c"] else None),
        ("loopsum", "Rust (-O3)",
         [pf("benchmarks", f"loopsum_rs{EXE_SUFFIX}")] if built["rust"] else None),
        ("loopsum", "Go",
         [pf("benchmarks", f"loopsum_go{EXE_SUFFIX}")] if built["go"] else None),
        ("loopsum", "Java",
         ["java", "-cp", "benchmarks", "loopsum"] if built["java"] else None),
        ("loopsum", "Node.js",
         ["node", "benchmarks/loopsum.js"] if built["node"] else None),
        ("loopsum", "Python",
         [sys.executable, "benchmarks/loopsum.py"]),
        # fib group
        ("fib", "Tulpar AOT (LLVM)",
         [pf("benchmarks", f"fib_tulpar{EXE_SUFFIX}")] if built["tulpar_aot"] else None),
        ("fib", "Tulpar VM",
         [str(TULPAR_EXE), "--vm", "benchmarks/fib.tpr"] if built["tulpar_vm"] else None),
        ("fib", "C (gcc -O2)",
         [pf("benchmarks", f"fib_c{EXE_SUFFIX}")] if built["c"] else None),
        ("fib", "Rust (-O3)",
         [pf("benchmarks", f"fib_rs{EXE_SUFFIX}")] if built["rust"] else None),
        ("fib", "Go",
         [pf("benchmarks", f"fib_go{EXE_SUFFIX}")] if built["go"] else None),
        ("fib", "Java",
         ["java", "-cp", "benchmarks", "fib"] if built["java"] else None),
        ("fib", "Node.js",
         ["node", "benchmarks/fib.js"] if built["node"] else None),
        ("fib", "Python",
         [sys.executable, "benchmarks/fib.py"]),
    ]

    rows: list[dict] = []
    for group, lang, cmd in plan:
        if cmd is None:
            print(f"[bench] skip {group}/{lang}: toolchain missing")
            continue
        print(f"[bench] running {group}/{lang} (best of {repeats})")
        # Wrap best_of: a single hung / crashed comparator must not
        # take down the entire CPU run. We log, mark the row missing,
        # and keep going. The CI table renders the missing cell as
        # `—` so the failure is visible without being fatal.
        try:
            ms, out = best_of(cmd, repeats=repeats)
        except (subprocess.TimeoutExpired, subprocess.SubprocessError,
                RuntimeError, OSError) as e:
            print(f"[bench]   -> FAILED: {type(e).__name__}: {e}",
                  file=sys.stderr)
            continue
        rows.append({
            "benchmark": group,
            "language": lang,
            "best_ms": ms,
            "output": out,
        })
        print(f"[bench]   -> {ms} ms  out={out!r}")
    return rows


# ----------------------------------------------------------------------
# HTTP benchmark — reuse http_bench.py's bench_server / WINGS_VARIANTS
# instead of shelling out, so we get structured data directly.
# ----------------------------------------------------------------------
def http_table(requests: int, connections: int) -> list[dict]:
    """Run the four Wings listener variants + Python + Node baselines."""
    import tempfile

    results: list[dict] = []
    env = os.environ.copy()
    env["TULPAR_HTTP_QUIET"] = "1"

    with tempfile.TemporaryDirectory(prefix="tulpar_bench_") as wd:
        # Each Wings variant: write a one-file Tulpar source, build it,
        # bench it. Same handler; only the listener call changes.
        for label, port, call, suffix, register in http_bench.WINGS_VARIANTS:
            tpr_path = Path(wd) / f"wings_{suffix}.tpr"
            bin_base = Path(wd) / f"wings_{suffix}"
            tpr_path.write_text(http_bench.wings_src(call, register=register),
                                encoding="utf-8")
            try:
                subprocess.run([str(TULPAR_EXE), "build", str(tpr_path), str(bin_base)],
                               check=True, timeout=120,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            except subprocess.SubprocessError as e:
                print(f"[bench] wings build failed for {label}: {e}",
                      file=sys.stderr)
                continue
            tulpar_bin = str(bin_base) + EXE_SUFFIX
            try:
                r = http_bench.bench_server(label, [tulpar_bin], port,
                                            requests, connections, env=env)
                results.append({"server": r["label"], "rps": round(r["rps"], 0),
                                "wall_s": round(r["wall_s"], 3),
                                "completed": r["done"]})
                print(f"[bench]   {label}: {r['rps']:.0f} rps")
            except Exception as e:  # noqa: BLE001
                print(f"[bench] {label} failed: {e}", file=sys.stderr)

        # Python ThreadingHTTPServer baseline.
        py_path = Path(wd) / "py_baseline.py"
        py_path.write_text(http_bench.PY_BASELINE_SRC, encoding="utf-8")
        try:
            r = http_bench.bench_server(
                "Python ThreadingHTTP",
                [sys.executable, "-u", str(py_path)],
                8766, requests, connections,
                ready_marker="ready", stdout_pipe=True)
            results.append({"server": r["label"], "rps": round(r["rps"], 0),
                            "wall_s": round(r["wall_s"], 3), "completed": r["done"]})
            print(f"[bench]   Python: {r['rps']:.0f} rps")
        except Exception as e:  # noqa: BLE001
            print(f"[bench] Python baseline failed: {e}", file=sys.stderr)

        # Node baseline.
        if shutil.which("node"):
            node_path = Path(wd) / "node_baseline.js"
            node_path.write_text(http_bench.NODE_BASELINE_SRC, encoding="utf-8")
            try:
                r = http_bench.bench_server(
                    "Node.js http",
                    ["node", str(node_path)],
                    8767, requests, connections,
                    ready_marker="ready", stdout_pipe=True)
                results.append({"server": r["label"], "rps": round(r["rps"], 0),
                                "wall_s": round(r["wall_s"], 3), "completed": r["done"]})
                print(f"[bench]   Node: {r['rps']:.0f} rps")
            except Exception as e:  # noqa: BLE001
                print(f"[bench] Node baseline failed: {e}", file=sys.stderr)

    return results


# ----------------------------------------------------------------------
# RESULTS.md regeneration. We rewrite ONLY the auto-managed section
# between the BENCH:RESULTS_START / END markers; prose context outside
# the markers is preserved verbatim.
# ----------------------------------------------------------------------
RESULTS_START = "<!-- BENCH:RESULTS START -->"
RESULTS_END = "<!-- BENCH:RESULTS END -->"


# Mirror of the HTTP_SERVER_MODELS map in update_readme.py. Kept inline
# here so RESULTS.md and README.md show identical "what is this server"
# descriptions for the same data — neither file references the other.
HTTP_SERVER_MODELS = {
    "Tulpar listen":           "single thread, one request at a time",
    "Tulpar listen_async":     "OS thread spawned per connection",
    "Tulpar listen_pool x8":   "8 pre-spawned worker threads share accept()",
    "Tulpar listen_evented":   "single thread, poll()-multiplexed",
    "Tulpar evented + cache":  "evented + wire-byte cache for cached_get routes",
    "Node.js http":            "single-thread event loop",
    "Python ThreadingHTTP":    "OS thread spawned per request",
}


def render_results_md_section(payload: dict) -> str:
    """Build the auto-managed Markdown block dropped into RESULTS.md."""
    run = payload["run"]
    cpu = payload["cpu"]
    http = payload["http"]["results"]

    lines: list[str] = []
    lines.append(RESULTS_START)
    lines.append("")
    lines.append("> _This block is auto-generated by `benchmarks/ci_run.py` "
                 "on every push to `main`. Do not hand-edit — your changes "
                 "will be overwritten by the next CI run. Prose context "
                 "below the END marker is preserved._")
    lines.append("")
    lines.append(f"**Run:** `{run['timestamp_utc']}` UTC · "
                 f"commit `{run['git_short']}` · "
                 f"runner `{run['runner'].get('os')}` "
                 f"`{run['runner'].get('cpu_model', 'unknown CPU')}` · "
                 f"best of {run['repeats']}.")
    lines.append("")

    # CPU table.
    lines.append("## CPU benchmarks")
    lines.append("")
    lines.append(f"Best wall time of {run['repeats']} runs, in milliseconds. "
                 "**Lower is faster.**")
    lines.append("")
    by_lang = sorted({r["language"] for r in cpu})
    benches = ["loopsum", "fib"]
    header = "| Language | " + " | ".join(b for b in benches) + " |"
    sep = "|" + "---|" * (len(benches) + 1)
    lines.append(header)
    lines.append(sep)
    lookup = {(r["benchmark"], r["language"]): r["best_ms"] for r in cpu}
    for lang in by_lang:
        row = [f"**{lang}**" if lang.startswith("Tulpar") else lang]
        for b in benches:
            v = lookup.get((b, lang))
            row.append(f"{v}" if v is not None else "—")
        lines.append("| " + " | ".join(row) + " |")
    lines.append("")

    # Ratios — Tulpar AOT vs C / Node / Python, for each benchmark.
    # The headline claim sits in README; this table is the
    # full per-benchmark breakdown anyone can audit.
    aot_rows = {r["benchmark"]: r["best_ms"] for r in cpu
                if r["language"] == "Tulpar AOT (LLVM)"}
    c_rows = {r["benchmark"]: r["best_ms"] for r in cpu
              if r["language"] == "C (gcc -O2)"}
    node_rows = {r["benchmark"]: r["best_ms"] for r in cpu
                 if r["language"] == "Node.js"}
    py_rows = {r["benchmark"]: r["best_ms"] for r in cpu
               if r["language"] == "Python"}
    if aot_rows and c_rows:
        lines.append("**Speed ratios** — how Tulpar AOT compares to each reference language:")
        lines.append("")
        lines.append("| Workload | vs C (gcc -O2) | vs Node.js | vs Python |")
        lines.append("|---|---:|---:|---:|")
        for b in benches:
            if b not in aot_rows or b not in c_rows:
                continue
            vc = round(aot_rows[b] / c_rows[b], 2)
            vc_cell = f"{vc}× of C"
            vn = (f"**{round(node_rows[b] / aot_rows[b], 1)}× faster**"
                  if b in node_rows else "—")
            vp = (f"**{round(py_rows[b] / aot_rows[b])}× faster**"
                  if b in py_rows else "—")
            lines.append(f"| {b} | {vc_cell} | {vn} | {vp} |")
        lines.append("")

    # HTTP table. Renders once per non-empty results list — the
    # low-conc baseline first, then the high-conc variant under its
    # own subheading so readers can compare single-stream perf against
    # multi-core scaling without conflating them.
    def _render_http(results: list[dict], requests: int, connections: int,
                     subheading: str, note: str) -> None:
        if not results:
            return
        lines.append(subheading)
        lines.append("")
        lines.append(f"{requests:,} GETs over {connections} keep-alive "
                     "connections; single localhost run; each server hosting "
                     "the same JSON handler. **Higher req/sec is better.**"
                     .replace(",", " "))
        if note:
            lines.append("")
            lines.append(note)
        lines.append("")
        node = next((r for r in results if r["server"].startswith("Node")), None)
        lines.append("| Server | Scheduling model | req/sec | vs Node.js |")
        lines.append("|---|---|---:|---:|")
        for r in sorted(results, key=lambda x: -x["rps"]):
            server = r["server"]
            label = f"**{server}**" if server.startswith("Tulpar") else server
            model = HTTP_SERVER_MODELS.get(server, "—")
            rps = int(r["rps"])
            if node and node["rps"] and rps > 0:
                ratio = r["rps"] / node["rps"]
                if server.startswith("Node"):
                    vs_node = "_(baseline)_"
                elif ratio >= 1:
                    vs_node = f"**{ratio:.2f}× faster**"
                else:
                    vs_node = f"{1.0/ratio:.0f}× slower"
            elif rps == 0:
                vs_node = "_(failed — 0 rps)_"
            else:
                vs_node = "—"
            rps_fmt = f"{rps:,}".replace(",", " ")
            lines.append(f"| {label} | {model} | {rps_fmt} | {vs_node} |")
        lines.append("")

    if http:
        _render_http(
            http, payload['http']['requests'], payload['http']['connections'],
            "## HTTP throughput (low concurrency)",
            "_Apples-to-apples view: a single client thread per connection, "
            "modest parallelism. Comparable to a CLI tool or background "
            "agent hitting the API._")

    http_high_payload = payload.get("http_high", {})
    if http_high_payload.get("results"):
        _render_http(
            http_high_payload["results"],
            http_high_payload["requests"],
            http_high_payload["connections"],
            "## HTTP throughput (high concurrency)",
            "_Higher parallelism — same per-thread workload as the low-conc "
            "table above, but 4× the concurrent connections. Surfaces the "
            "multi-core scaling that single-stream benchmarks hide._")

    lines.append(RESULTS_END)
    return "\n".join(lines)


def splice_section(path: Path, start: str, end: str, body: str) -> bool:
    """Replace the [start..end] block in `path` with `body`. Returns
    True if the file changed. If the markers are missing the section
    is appended."""
    if not path.exists():
        path.write_text(body + "\n", encoding="utf-8")
        return True
    text = path.read_text(encoding="utf-8")
    if start in text and end in text:
        before, _, rest = text.partition(start)
        _, _, after = rest.partition(end)
        new_text = before + body + after
    else:
        new_text = text.rstrip() + "\n\n" + body + "\n"
    if new_text != text:
        path.write_text(new_text, encoding="utf-8")
        return True
    return False


# ----------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repeats", type=int, default=5,
                    help="repeats per CPU benchmark (best wall time wins)")
    ap.add_argument("--http-requests", type=int, default=3000,
                    help="total HTTP requests for the low-concurrency pass")
    ap.add_argument("--http-connections", type=int, default=4,
                    help="parallel keep-alive connections for the low-concurrency pass")
    ap.add_argument("--http-high-multiplier", type=int, default=4,
                    help="scale factor for the second high-concurrency HTTP "
                         "pass — requests AND connections are multiplied by "
                         "this, so the per-thread workload stays constant and "
                         "only the concurrency varies (4× by default: 4 conn "
                         "+ 750 reqs/thread vs 16 conn + 750 reqs/thread). "
                         "Set to 1 to disable the high-concurrency pass.")
    ap.add_argument("--no-http", action="store_true",
                    help="skip the HTTP throughput suite")
    ap.add_argument("--no-java", action="store_true",
                    help="skip Java even if javac is available "
                         "(JVM startup dominates short runs and varies "
                         "wildly on shared CI runners)")
    ap.add_argument("--out-json", default="benchmarks/RESULTS.json")
    ap.add_argument("--out-md", default="benchmarks/RESULTS.md")
    args = ap.parse_args()

    print(f"[bench] repo: {REPO}")
    print(f"[bench] tulpar: {TULPAR_EXE} (exists={TULPAR_EXE.exists()})")
    print(f"[bench] toolchains: gcc={have('gcc')} rustc={have('rustc')} "
          f"go={have('go', 'version')} javac={have('javac')} "
          f"node={have('node', '--version')}")

    built = build_natives(skip_java=args.no_java)
    cpu = cpu_table(built, repeats=args.repeats)

    http = []
    http_high: list[dict] = []
    if not args.no_http:
        try:
            http = http_table(args.http_requests, args.http_connections)
        except Exception as e:  # noqa: BLE001
            print(f"[bench] HTTP suite failed: {e}", file=sys.stderr)

        # Second pass at higher concurrency. Same per-thread workload
        # (requests * multiplier across connections * multiplier =
        # same per-conn count), so the only experimental variable is
        # how many concurrent keep-alive streams the server is juggling.
        # Lets readers compare Tulpar's low-conc single-stream perf
        # against its high-conc multi-core scaling without conflating
        # the two in one number.
        if args.http_high_multiplier > 1:
            try:
                mult = args.http_high_multiplier
                http_high = http_table(args.http_requests * mult,
                                       args.http_connections * mult)
            except Exception as e:  # noqa: BLE001
                print(f"[bench] HTTP high-conc suite failed: {e}",
                      file=sys.stderr)

    payload = {
        "schema_version": SCHEMA_VERSION,
        "run": {
            "timestamp_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "git_sha": git_sha(),
            "git_short": git_sha()[:7],
            "runner": detect_runner_info(),
            "repeats": args.repeats,
            "loopsum_iterations": 10_000_000,
            "fib_n": 35,
        },
        "cpu": cpu,
        "http": {
            "requests": args.http_requests,
            "connections": args.http_connections,
            "results": http,
        },
        "http_high": {
            "requests": args.http_requests * args.http_high_multiplier,
            "connections": args.http_connections * args.http_high_multiplier,
            "results": http_high,
        },
    }

    out_json = REPO / args.out_json
    out_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"[bench] wrote {out_json}")

    md_body = render_results_md_section(payload)
    out_md = REPO / args.out_md
    changed = splice_section(out_md, RESULTS_START, RESULTS_END, md_body)
    print(f"[bench] {out_md} {'updated' if changed else 'unchanged'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
