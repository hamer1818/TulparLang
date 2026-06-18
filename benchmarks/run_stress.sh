#!/usr/bin/env bash
# Wings stress harness. Launches the stress server in one mode, then:
#   1) keep-alive scaling sweep on /ping (per-connection throughput + plateau)
#   2) per-endpoint runs at capacity (keep-alive)
#   3) Connection: close at high concurrency (many short-lived web clients)
# Reports RPS + latency percentiles + server RSS for each.
#
# Usage: ./run_stress.sh <serve|pool> [port]
set -u
MODE="${1:-serve}"
PORT="${2:-8585}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DUR=6
NCPU="$(nproc)"
# Capacity = how many connections the mode can serve concurrently.
if [ "$MODE" = "pool" ]; then CAP="$NCPU"; else CAP=1; fi

cleanup() { fuser -k "${PORT}/tcp" >/dev/null 2>&1; sleep 0.5; }
trap cleanup EXIT
cleanup

( cd "$ROOT" && TULPAR_WINGS_HOST=127.0.0.1 TULPAR_WINGS_NODOCS=1 \
    WINGS_MODE="$MODE" ./tulpar benchmarks/stress_server.tpr >/tmp/wings_srv.log 2>&1 ) &
for i in $(seq 1 60); do ss -ltn 2>/dev/null | grep -q ":${PORT} " && break; sleep 0.5; done
if ! ss -ltn 2>/dev/null | grep -q ":${PORT} "; then echo "SERVER FAILED TO BIND"; tail -5 /tmp/wings_srv.log; exit 1; fi
SRV_PID="$(ss -ltnp 2>/dev/null | grep ":${PORT} " | grep -oE 'pid=[0-9]+' | grep -oE '[0-9]+' | head -1)"
rss() { [ -n "$SRV_PID" ] && awk '/VmRSS/{print $2}' /proc/$SRV_PID/status 2>/dev/null; }

echo "=================================================================="
echo " Wings stress — mode=$MODE  port=$PORT  pid=$SRV_PID  cap=$CAP"
echo " ${NCPU} CPU / $(free -m | awk '/Mem:/{print $2}') MB RAM  | ${DUR}s/level | idle RSS=$(rss) KB"
echo "=================================================================="

run() {  # label conc method path body connmode
  local label="$1" conc="$2" method="$3" path="$4" body="${5:-}" cm="${6:-keepalive}"
  printf -- '-- %-22s conc=%-4s %s %s [%s]\n' "$label" "$conc" "$method" "$path" "$cm"
  "$HERE/loadtest" 127.0.0.1 "$PORT" "$conc" "$DUR" "$method" "$path" "$body" "$cm"
  echo "   RSS=$(rss) KB"
}

"$HERE/loadtest" 127.0.0.1 "$PORT" "$CAP" 3 GET /ping "" keepalive >/dev/null 2>&1  # warmup

echo; echo "### 1) keep-alive scaling — GET /ping (routing ceiling per conn)"
for c in 1 2 4 8 14 28 56; do run "scale" $c GET /ping "" keepalive; done

echo; echo "### 2) per-endpoint at capacity (conc=$CAP, keep-alive)"
run "GET /users"     "$CAP" GET  /users      ""                  keepalive
run "GET /users/:id" "$CAP" GET  /users/42   ""                  keepalive
run "POST /users"    "$CAP" POST /users      '{"name":"Tulpar"}' keepalive
run "GET /big(50)"   "$CAP" GET  /big        ""                  keepalive

echo; echo "### 3) Connection: close — many short-lived clients"
for c in 50 200 500; do run "close /ping" $c GET /ping "" close; done
run "close /big" 200 GET /big "" close

echo; echo "peak RSS=$(rss) KB"
echo "DONE mode=$MODE"
