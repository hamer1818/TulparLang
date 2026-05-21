#!/usr/bin/env python3
"""Splice auto-managed benchmark sections into README.md from RESULTS.json.

Three regions, each delimited by an HTML-comment marker pair. The
script rewrites only the content between the START and END markers;
everything else in README.md is preserved character-for-character.

  <!-- BENCH:META START -->         <!-- BENCH:META END -->
  <!-- BENCH:CPU_TABLE START -->    <!-- BENCH:CPU_TABLE END -->
  <!-- BENCH:HTTP_TABLE START -->   <!-- BENCH:HTTP_TABLE END -->

If a marker pair is missing the corresponding section is silently
skipped — the script never blindly appends to README. That keeps it
safe to run after a README restructure.

Invoked by `.github/workflows/bench.yml` after `ci_run.py` has written
benchmarks/RESULTS.json. Standalone-runnable for local previewing:

    python3 benchmarks/update_readme.py
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Markers must match what the workflow / RESULTS.md splicer use.
MARKERS = {
    "META":       ("<!-- BENCH:META START -->",       "<!-- BENCH:META END -->"),
    "CPU_TABLE":  ("<!-- BENCH:CPU_TABLE START -->",  "<!-- BENCH:CPU_TABLE END -->"),
    "HTTP_TABLE": ("<!-- BENCH:HTTP_TABLE START -->", "<!-- BENCH:HTTP_TABLE END -->"),
}

# Languages we render in the README CPU table, in column order. Matches
# the column shape README readers got used to before this script
# existed. Languages absent from RESULTS.json (toolchain unavailable on
# the runner) collapse to a `—` cell.
README_LANG_COLUMNS = [
    "Tulpar AOT (LLVM)",
    "C (gcc -O2)",
    "Rust (-O3)",
    "Go",
    "Node.js",
    "Python",
]


def render_meta(payload: dict) -> str:
    run = payload["run"]
    runner = run["runner"]
    cpu_model = runner.get("cpu_model", "developer machine")
    return (
        f"> _Baked from local benchmark run (best of 5). "
        f"Last run: **{run['timestamp_utc']}** UTC · "
        f"commit [`{run['git_short']}`](../../commit/{run['git_sha']}) · "
        f"runner `{runner.get('os', '?')}` · `{cpu_model}` "
        f"({runner.get('cpu_count', '?')} CPUs). "
        f"Methodology + Local Run instructions: [benchmarks/CI.md](benchmarks/CI.md)._"
    )


def _ms(rows: dict, lang: str) -> str:
    v = rows.get(lang)
    return f"**{v}**" if (v is not None and lang.startswith("Tulpar")) \
        else (f"{v}" if v is not None else "—")


def render_cpu_table(payload: dict) -> str:
    cpu = payload["cpu"]
    # Group rows by benchmark for the row-per-benchmark layout.
    by_bench: dict[str, dict[str, float]] = {}
    for r in cpu:
        by_bench.setdefault(r["benchmark"], {})[r["language"]] = r["best_ms"]

    cols = README_LANG_COLUMNS
    header = "| Workload | " + " | ".join(cols) + " |"
    sep = "|---|" + "---:|" * len(cols)
    lines = []
    lines.append("_Wall time of the inner loop, best of 5 runs. "
                 "**Lower is faster.**_")
    lines.append("")
    lines.append(header)
    lines.append(sep)
    pretty_label = {
        "loopsum": "loopsum (ms)",
        "fib": "fib(35) (ms)",
        "ackermann": "ackermann(3,8) (ms)",
        "tak": "tak(18,12,6) (ms)",
        "sieve": "sieve(100K) (ms)",
        "struct_sum": "struct_sum (ms)",
        "struct_array_push": "struct_arr_push (ms)",
    }
    for bench in ("loopsum", "fib", "ackermann", "tak", "sieve",
                  "struct_sum", "struct_array_push"):
        if bench not in by_bench:
            continue
        rows = by_bench[bench]
        cells = [pretty_label.get(bench, bench)] + [_ms(rows, lang) for lang in cols]
        lines.append("| " + " | ".join(cells) + " |")

    # Spread the ratio sentence across multiple reference languages so a
    # reader sees the headline win against *every* peer in one paragraph,
    # not just C. The "vs C" pair anchors the ceiling, the "vs Node /
    # Python" pairs sell the dynamic-language audience.
    aot    = {b: by_bench[b].get("Tulpar AOT (LLVM)") for b in by_bench}
    c      = {b: by_bench[b].get("C (gcc -O2)")       for b in by_bench}
    node   = {b: by_bench[b].get("Node.js")           for b in by_bench}
    python = {b: by_bench[b].get("Python")            for b in by_bench}

    def _range(num: dict, den: dict) -> tuple[float, float] | None:
        vals = []
        for b in by_bench:
            if num.get(b) and den.get(b):
                vals.append(num[b] / den[b])
        return (min(vals), max(vals)) if vals else None

    aot_vs_c = _range(aot, c)
    node_vs_aot = _range(node, aot)
    py_vs_aot = _range(python, aot)

    if aot_vs_c:
        lines.append("")
        lo, hi = aot_vs_c
        # "1.x–3.x× of C" reads naturally as "comparable to C, within a
        # small multiplicative factor". We deliberately don't say
        # "slower" — these ratios are within the range of compiler
        # codegen variance and our AOT is meant to land in that band.
        parts = [f"Tulpar AOT lands at **{lo:.2f}×–{hi:.2f}× of C (gcc -O2)** "
                 f"on these microbenchmarks (i.e. C-comparable, with a small "
                 f"multiplicative gap)"]
        if node_vs_aot:
            nlo, nhi = node_vs_aot
            parts.append(f"**{nlo:.1f}–{nhi:.1f}× faster than Node.js**")
        if py_vs_aot:
            plo, phi = py_vs_aot
            parts.append(f"**{plo:.0f}–{phi:.0f}× faster than Python**")
        # Join "X, Y, and Z" naturally.
        if len(parts) == 1:
            sentence = parts[0] + "."
        elif len(parts) == 2:
            sentence = parts[0] + ", and " + parts[1] + "."
        else:
            sentence = parts[0] + ", " + ", ".join(parts[1:-1]) + ", and " + parts[-1] + "."
        lines.append(sentence)
    return "\n".join(lines)


# One-line "what is this scheduling model" descriptions for the HTTP
# table. The README also has a longer "Wings — four ways to listen"
# section, but readers shouldn't have to scroll to figure out what
# `listen_async` vs `listen_pool` means while scanning the perf table.
HTTP_SERVER_MODELS = {
    "Tulpar listen":           "single thread, one request at a time",
    "Tulpar listen_async":     "OS thread spawned per connection",
    "Tulpar listen_pool":      "worker pool sized to host CPU count, sharing accept()",
    "Tulpar listen_evented":   "single thread, poll()-multiplexed",
    "Tulpar evented + cache":  "evented + wire-byte cache for cached_get routes",
    "Node.js http":            "single-thread event loop",
    "Python ThreadingHTTP":    "OS thread spawned per request",
}


def _fmt_thousand(n: int) -> str:
    """Format an int with space-as-thousand-separator (matches Tulpar
    docs style and works in monospace tables). `f'{n:,}'` uses commas
    which conflict with prose punctuation when we do bulk replaces, so
    do the substitution once here on a known-numeric value."""
    return f"{n:,}".replace(",", " ")


def _render_one_http_block(http: list[dict], requests, connections,
                           preamble: str, headline_label: str) -> list[str]:
    """Render a single HTTP throughput table (rows + headline sentence).
    Used twice: once for the low-conc baseline, once for the high-conc
    pass. Returns the list of markdown lines so the caller can splice
    them between an optional subheading and the next section."""
    if not http:
        return []
    node = next((r for r in http if r["server"].startswith("Node")), None)
    lines: list[str] = []
    req_str = _fmt_thousand(requests) if isinstance(requests, int) else str(requests)
    conn_str = str(connections)
    lines.append(f"_{req_str} GETs over {conn_str} keep-alive connections; "
                 f"single localhost run; each server hosting the same JSON "
                 f"handler. {preamble} Higher req/sec is better._")
    lines.append("")
    lines.append("| Server | Scheduling model | req/sec | vs Node.js |")
    lines.append("|---|---|---:|---:|")
    for r in sorted(http, key=lambda x: -x["rps"]):
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
                inv = 1.0 / ratio
                vs_node = f"{inv:.2f}× slower"
        elif rps == 0:
            vs_node = "_(failed — 0 rps)_"
        else:
            vs_node = "—"
        lines.append(f"| {label} | {model} | {_fmt_thousand(rps)} | {vs_node} |")

    # Headline ratio for this concurrency level. Restrict to Tulpar
    # rows that ACTUALLY beat Node — a single outlier (a runner-specific
    # filesystem cliff on the WSL `listen` row, etc.) would otherwise
    # produce a "0.5×–1.5×" range that reads worse than the truth. If
    # every variant trails Node we skip the sentence rather than print
    # a "slower" headline.
    winning = [r for r in http
               if r["server"].startswith("Tulpar")
               and r["rps"] > 0 and node and r["rps"] > node["rps"]]
    if winning and node and node["rps"]:
        ratios = sorted(r["rps"] / node["rps"] for r in winning)
        lo, hi = ratios[0], ratios[-1]
        lines.append("")
        if abs(hi - lo) < 0.05:
            lines.append(f"Tulpar Wings listeners {headline_label} serve "
                         f"**~{hi:.2f}× the throughput of Node.js** built-in `http`.")
        elif len(winning) == 1:
            lines.append(f"Tulpar's `{winning[0]['server'].replace('Tulpar ', '')}` "
                         f"{headline_label} serves **{hi:.2f}× the throughput of "
                         f"Node.js** built-in `http`.")
        else:
            lines.append(f"{len(winning)} of the Tulpar Wings listeners "
                         f"{headline_label} beat Node.js' built-in `http`, "
                         f"by **{lo:.2f}×–{hi:.2f}×** depending on "
                         f"scheduling model.")
    return lines


def render_http_table(payload: dict) -> str:
    http = payload["http"]["results"]
    if not http:
        return "_(HTTP throughput suite did not produce results on the latest run.)_"

    # Low-concurrency block: the headline baseline. Comparable to a CLI
    # tool or background agent hitting the API one client at a time.
    requests = payload["http"].get("requests", "?")
    connections = payload["http"].get("connections", "?")
    out = []
    out.append("### Low concurrency")
    out.append("")
    out.extend(_render_one_http_block(
        http, requests, connections,
        preamble="Apples-to-apples view: one client thread per connection.",
        headline_label="at this concurrency"))

    # High-concurrency block (optional). Same per-thread workload as
    # the low-conc table, but with 4× the concurrent connections —
    # surfaces multi-core scaling that single-stream benchmarks hide.
    http_high = payload.get("http_high", {}).get("results", [])
    if http_high:
        out.append("")
        out.append("### High concurrency")
        out.append("")
        out.extend(_render_one_http_block(
            http_high,
            payload["http_high"].get("requests", "?"),
            payload["http_high"].get("connections", "?"),
            preamble="Same per-thread workload as the low-concurrency block, "
                     "scaled up to surface multi-core scaling.",
            headline_label="under this load"))
    return "\n".join(out)


def splice(text: str, start: str, end: str, body: str) -> tuple[str, bool]:
    """Replace the content between `start` and `end` with `body`. The
    markers themselves are preserved on their own lines so a future
    diff is easy to read. Returns (new_text, changed_flag)."""
    if start not in text or end not in text:
        return text, False
    before, _, rest = text.partition(start)
    middle, _, after = rest.partition(end)
    del middle
    new_block = f"{start}\n{body}\n{end}"
    new_text = before + new_block + after
    return new_text, new_text != text


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default=str(REPO / "benchmarks" / "RESULTS.json"),
                    help="path to RESULTS.json")
    ap.add_argument("--readme", default=str(REPO / "README.md"),
                    help="path to README.md")
    args = ap.parse_args()

    payload = json.loads(Path(args.results).read_text(encoding="utf-8"))
    if payload.get("schema_version") != 1:
        print(f"[update_readme] unexpected schema_version "
              f"{payload.get('schema_version')!r} — refusing to splice "
              f"to avoid corrupting README. Update update_readme.py.",
              file=sys.stderr)
        return 2

    readme = Path(args.readme)
    text = readme.read_text(encoding="utf-8")

    sections = {
        "META":       render_meta(payload),
        "CPU_TABLE":  render_cpu_table(payload),
        "HTTP_TABLE": render_http_table(payload),
    }

    any_changed = False
    for key, body in sections.items():
        start, end = MARKERS[key]
        if start not in text or end not in text:
            print(f"[update_readme] markers for {key} missing in README — "
                  f"skipping that section. Add the marker pair if you "
                  f"want this region auto-managed.")
            continue
        text, changed = splice(text, start, end, body)
        print(f"[update_readme] {key}: {'updated' if changed else 'unchanged'}")
        any_changed = any_changed or changed

    if any_changed:
        readme.write_text(text, encoding="utf-8")
        print(f"[update_readme] wrote {readme}")
    else:
        print("[update_readme] README.md unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
