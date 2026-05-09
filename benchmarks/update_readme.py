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
    cpu_model = runner.get("cpu_model", "shared CI runner")
    return (
        f"> _Auto-updated by CI on every push to `main`. "
        f"Last run: **{run['timestamp_utc']}** UTC · "
        f"commit [`{run['git_short']}`](../../commit/{run['git_sha']}) · "
        f"runner `{runner.get('os', '?')}` · `{cpu_model}` "
        f"({runner.get('cpu_count', '?')} CPUs). "
        f"Methodology: [benchmarks/CI.md](benchmarks/CI.md)._"
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
    header = "| Benchmark | " + " | ".join(cols) + " |"
    sep = "|---|" + "---:|" * len(cols)
    lines = [header, sep]
    pretty_label = {"loopsum": "loopsum (ms)", "fib": "fib(35) (ms)"}
    for bench in ("loopsum", "fib"):
        if bench not in by_bench:
            continue
        rows = by_bench[bench]
        cells = [pretty_label.get(bench, bench)] + [_ms(rows, lang) for lang in cols]
        lines.append("| " + " | ".join(cells) + " |")

    # Ratio summary sentence — drives the headline claim in the intro,
    # so we regenerate it from the same source numbers as the table.
    aot = {b: by_bench[b].get("Tulpar AOT (LLVM)") for b in by_bench}
    c   = {b: by_bench[b].get("C (gcc -O2)")     for b in by_bench}
    ratios = []
    for b in ("loopsum", "fib"):
        if aot.get(b) and c.get(b):
            ratios.append(round(aot[b] / c[b], 2))
    if ratios:
        lines.append("")
        if len(ratios) > 1:
            lines.append(f"Tulpar AOT lands at **{min(ratios)}–{max(ratios)}× "
                         f"C (gcc -O2)** on these microbenchmarks.")
        else:
            lines.append(f"Tulpar AOT lands at **{ratios[0]}× C (gcc -O2)** "
                         f"on this microbenchmark.")
    return "\n".join(lines)


def render_http_table(payload: dict) -> str:
    http = payload["http"]["results"]
    if not http:
        return "_(HTTP throughput suite did not produce results on the latest run.)_"

    # Pick a baseline for the "× listen" column. If sync `Tulpar listen`
    # is in the table use it; otherwise fall back to the slowest entry,
    # which keeps the column meaningful when sync `listen` is missing.
    sync = next((r for r in http if r["server"] == "Tulpar listen"), None)
    base_rps = sync["rps"] if sync else min(r["rps"] for r in http)
    node = next((r for r in http if r["server"].startswith("Node")), None)

    lines = []
    lines.append("| Server | req/sec | × Tulpar `listen` | vs Node.js |")
    lines.append("|---|---:|---:|---:|")
    for r in sorted(http, key=lambda x: -x["rps"]):
        label = r["server"]
        if label.startswith("Tulpar"):
            label = f"**{label}**" if "async" in label or "pool" in label or "evented" in label \
                    else label
        rps = int(r["rps"])
        ratio_listen = round(r["rps"] / base_rps, 2) if base_rps else 1.0
        if node and node["rps"]:
            n = round(r["rps"] / node["rps"], 2)
            if r["server"].startswith("Node"):
                vs_node = "reference"
            elif n >= 1:
                vs_node = f"{n}× faster"
            else:
                vs_node = f"{n}×"
        else:
            vs_node = "—"
        lines.append(f"| {label} | {rps:,} | {ratio_listen}× | {vs_node} |".replace(",", " "))

    # Headline cross-runtime ratio for the intro paragraph. Pick the
    # FASTEST working Tulpar listener (rps > 0) — if listen_async
    # crashed on this runner, `listen_pool` or `listen_evented` is
    # still meaningful. If everything failed, we skip the sentence
    # rather than print "0.0× the throughput".
    tulpar_rows = [r for r in http
                   if r["server"].startswith("Tulpar") and r["rps"] > 0]
    headline = max(tulpar_rows, key=lambda x: x["rps"]) if tulpar_rows else None
    async_row = headline if headline and headline["server"] != "Tulpar listen" else None
    if async_row and node and node["rps"]:
        ratio = round(async_row["rps"] / node["rps"], 2)
        listener_name = async_row["server"].replace("Tulpar ", "").split(" ")[0]
        lines.append("")
        lines.append(f"Tulpar's `{listener_name}` serves **{ratio}× the throughput "
                     f"of Node.js' built-in `http`** on this localhost run.")
    return "\n".join(lines)


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
