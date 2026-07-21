#!/usr/bin/env bash
# PTY smoke for the TUI builtins (screen_open/render/close, read_key,
# read_key_timeout, term_width/height). These need a real terminal, so the
# jest-style tests/*.test.tpr runner can't cover them — this harness gives
# the compiled probe a PTY via script(1) and asserts on the captured ANSI.
#
# Manual / not wired into CI (same status as wings_tls_smoke.py).
# Run from the repo root:  bash tests/tui_pty_smoke.sh
set -u
cd "$(dirname "$0")/.."

command -v script >/dev/null || { echo "SKIP: script(1) not available"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/probe.tpr" <<'EOF'
screen_open();
int w = term_width();
int h = term_height();
screen_render("KARE " + toString(w) + "x" + toString(h));
str k = read_key_timeout(200);
screen_close();
print("BOYUT " + toString(w) + "x" + toString(h) + " ILKTUS [" + k + "]");
str k2 = read_key();
print("OKTUSU [" + k2 + "]");
float t0 = clock_ms();
str kt = read_key_timeout(300);
float t1 = clock_ms();
print("TIMEOUT [" + kt + "] " + toString(toInt(t1 - t0)) + "ms");
EOF

./tulpar build "$TMP/probe.tpr" "$TMP/probe" >/dev/null 2>&1 || {
    echo "FAIL: probe did not compile"; exit 1; }

# Feed 'a' (picked up by the first read_key_timeout) and an up-arrow escape
# (decoded by read_key), then silence — the final read_key_timeout must
# actually wait its 300 ms. `sleep 2` keeps the PTY's stdin open so the
# timeout path is exercised instead of the EOF early-return.
{ printf 'a\033[A'; sleep 2; } | script -qec "$TMP/probe" "$TMP/out.txt" >/dev/null 2>&1

fails=0
check() {  # check <label> <grep-pattern>
    if grep -q "$2" "$TMP/out.txt"; then echo "  PASS $1"; else
        echo "  FAIL $1 (pattern: $2)"; fails=1; fi
}

echo "=== TUI PTY smoke ==="
check "alt-screen enter escape emitted"   $'\x1b\[?1049h'
check "cursor hidden"                     $'\x1b\[?25l'
check "synchronized-output frame"         $'\x1b\[?2026h'
check "alt-screen restored"               $'\x1b\[?1049l'
check "cursor restored"                   $'\x1b\[?25h'
check "PTY size seen (80x24)"             'BOYUT 80x24'
check "queued key picked up"              'ILKTUS \[a\]'
check "arrow key decoded"                 'OKTUSU \[up\]'
check "timeout returns empty"             'TIMEOUT \[\]'
# elapsed must be ~300ms (>=250 allows scheduler slack)
ms=$(grep -o 'TIMEOUT \[\] [0-9]*ms' "$TMP/out.txt" | grep -o '[0-9]*ms' | tr -d 'ms')
if [ -n "${ms:-}" ] && [ "$ms" -ge 250 ]; then
    echo "  PASS timeout actually waited (${ms}ms)"
else
    echo "  FAIL timeout waited only ${ms:-?}ms"; fails=1
fi

[ "$fails" = "0" ] && echo "ALL PASS" || echo "FAILURES PRESENT"
exit $fails
