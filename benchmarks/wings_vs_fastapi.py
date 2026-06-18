#!/usr/bin/env python3
"""Apples-to-apples micro-benchmark: Wings (Tulpar AOT) vs FastAPI (uvicorn).

Same CRUD API (tests/compare_{wings,fastapi}_users_api), same in-process load
generator hitting both, so the relative numbers are fair even though the
dependency-free threaded client is itself a soft ceiling on absolute RPS.

Metrics: requests/sec, p50/p99 latency, peak RSS (RAM), and binary/footprint.
The FastAPI side needs a venv with fastapi+uvicorn; pass its python via
--fastapi-python (default: /tmp/fastapi_bench_venv/bin/python).

    python3 benchmarks/wings_vs_fastapi.py

Run from the repo root (it shells out to ./tulpar to build the Wings binary).
"""
import argparse, http.client, multiprocessing, os, signal, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WINGS_SRC = os.path.join(REPO, "tests", "compare_wings_users_api.tpr")
FASTAPI_MOD = "tests.compare_fastapi_users_api:app"


def wait_ready(port, timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            c = http.client.HTTPConnection("127.0.0.1", port, timeout=1)
            c.request("GET", "/users/1")
            c.getresponse().read()
            c.close()
            return True
        except Exception:
            time.sleep(0.1)
    return False


def peak_rss_kb(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmHWM:"):  # peak resident set size
                    return int(line.split()[1])
    except Exception:
        pass
    return 0


def worker(port, path, stop_at, lats):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    n = 0
    while time.time() < stop_at:
        t0 = time.perf_counter()
        try:
            c.request("GET", path)
            c.getresponse().read()
        except Exception:
            c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            continue
        lats.append((time.perf_counter() - t0) * 1000.0)
        n += 1
    c.close()
    return n


def _proc_load(args):
    """One OS process: `threads` keepalive workers hammering until stop_at.
    Returns (count, sampled_latencies). Run in a separate process so the
    GIL doesn't cap how fast we can drive a sub-millisecond native server."""
    port, path, threads, stop_at = args
    buckets = [[] for _ in range(threads)]
    with ThreadPoolExecutor(max_workers=threads) as ex:
        futs = [ex.submit(worker, port, path, stop_at, buckets[i])
                for i in range(threads)]
        count = sum(f.result() for f in futs)
    lats = [x for b in buckets for x in b]
    # Downsample to keep IPC cheap; percentiles stay representative.
    return count, lats[::max(1, len(lats) // 5000)]


def run_load(port, path, threads, duration, procs):
    stop_at = time.time() + duration
    per = max(1, threads // procs)
    with multiprocessing.Pool(procs) as pool:
        results = pool.map(_proc_load,
                           [(port, path, per, stop_at)] * procs)
    total = sum(c for c, _ in results)
    lats = sorted(x for _, l in results for x in l)
    def pct(p):
        if not lats:
            return 0.0
        return lats[min(len(lats) - 1, int(len(lats) * p))]
    return {"rps": total / duration, "p50": pct(0.50),
            "p99": pct(0.99), "count": total}


def bench(name, start_cmd, port, pid_of, threads, duration, warmup, procs):
    proc = start_cmd()
    pid = pid_of(proc)
    try:
        if not wait_ready(port):
            print(f"  {name}: server did not become ready", file=sys.stderr)
            return None
        run_load(port, "/users/1", threads, warmup, procs)  # warmup
        res = run_load(port, "/users/1", threads, duration, procs)
        res["rss_kb"] = peak_rss_kb(pid)
        return res
    finally:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except Exception:
            proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duration", type=float, default=5.0)
    ap.add_argument("--warmup", type=float, default=1.5)
    ap.add_argument("--threads", type=int, default=64)
    ap.add_argument("--procs", type=int, default=8)
    ap.add_argument("--wings-port", type=int, default=8201)
    ap.add_argument("--fastapi-port", type=int, default=8202)
    ap.add_argument("--fastapi-python",
                    default="/tmp/fastapi_bench_venv/bin/python")
    args = ap.parse_args()

    # Build the Wings binary.
    wings_bin = "/tmp/bench_wings"
    src = open(WINGS_SRC).read().replace("serve()", f"serve({args.wings_port})")
    open("/tmp/bench_wings.tpr", "w").write(src)
    print("Building Wings binary...")
    r = subprocess.run([os.path.join(REPO, "tulpar"), "build",
                        "/tmp/bench_wings.tpr", wings_bin],
                       capture_output=True, text=True)
    if not os.path.exists(wings_bin):
        print("Wings build failed:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)
    wings_size = os.path.getsize(wings_bin)

    env = dict(os.environ, TULPAR_HTTP_QUIET="1", TULPAR_WINGS_NODOCS="1")

    def start_wings():
        return subprocess.Popen([wings_bin], env=env, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, start_new_session=True)

    def start_fastapi():
        return subprocess.Popen(
            [args.fastapi_python, "-m", "uvicorn", FASTAPI_MOD,
             "--port", str(args.fastapi_port), "--log-level", "warning"],
            cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True)

    print(f"Load: {args.procs} procs x {args.threads//args.procs} threads, {args.duration}s, GET /users/1\n")
    wings = bench("Wings", start_wings, args.wings_port, lambda p: p.pid,
                  args.threads, args.duration, args.warmup, args.procs)

    fa = None
    if os.path.exists(args.fastapi_python):
        # uvicorn forks a worker; sample the parent's tree RSS via its pid.
        fa = bench("FastAPI", start_fastapi, args.fastapi_port, lambda p: p.pid,
                   args.threads, args.duration, args.warmup, args.procs)
    else:
        print(f"(skipping FastAPI — no python at {args.fastapi_python})")

    def row(name, r, size=None):
        if not r:
            return f"| {name} | — | — | — | — | — |"
        sz = f"{size/1e6:.1f} MB" if size else "—"
        return (f"| {name} | {r['rps']:.0f} | {r['p50']:.2f} | {r['p99']:.2f} "
                f"| {r['rss_kb']/1024:.1f} | {sz} |")

    print("\n## Results (GET /users/1)\n")
    print("| Server | RPS | p50 ms | p99 ms | peak RSS MB | binary |")
    print("|---|---|---|---|---|---|")
    print(row("Wings (Tulpar AOT)", wings, wings_size))
    print(row("FastAPI (uvicorn)", fa, None))
    if wings and fa and fa["rps"]:
        print(f"\nWings RPS / FastAPI RPS = {wings['rps']/fa['rps']:.2f}×")
        print(f"FastAPI RSS / Wings RSS = {fa['rss_kb']/max(1,wings['rss_kb']):.1f}×")


if __name__ == "__main__":
    main()
