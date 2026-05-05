#!/bin/bash
# Typeinfer regression runner — drives `tulpar --strict <file>` over
# pass/ and fail/ fixtures and asserts the expected exit code.
#
# Layout:
#   tests/typeinfer/pass/*.tpr   — must exit 0 under --strict
#   tests/typeinfer/fail/*.tpr   — must exit 1 under --strict, and
#                                   produce at least one `[typecheck]`
#                                   line on stderr.
#
# Run from repo root: `./tests/typeinfer/run.sh`. The runner expects
# `./tulpar` (or `./tulpar.exe` on Git Bash) to exist and be built.

set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

TULPAR="./tulpar"
[ -x "$TULPAR" ] || TULPAR="./tulpar.exe"
if [ ! -x "$TULPAR" ]; then
    echo "ERROR: ./tulpar not built. Run ./build.sh first." >&2
    exit 1
fi

failures=0
pass_count=0
fail_count=0

for f in tests/typeinfer/pass/*.tpr; do
    [ -f "$f" ] || continue
    out=$("$TULPAR" --strict "$f" 2>&1)
    rc=$?
    if [ $rc -eq 0 ]; then
        printf "${GREEN}PASS${NC} %s\n" "$f"
        pass_count=$((pass_count + 1))
    else
        printf "${RED}FAIL${NC} %s — expected exit 0, got %d\n" "$f" "$rc"
        echo "$out" | sed 's/^/    /'
        failures=$((failures + 1))
    fi
done

for f in tests/typeinfer/fail/*.tpr; do
    [ -f "$f" ] || continue
    out=$("$TULPAR" --strict "$f" 2>&1)
    rc=$?
    if [ $rc -ne 0 ] && echo "$out" | grep -q '\[typecheck\]'; then
        printf "${GREEN}PASS${NC} %s (rejected as expected)\n" "$f"
        fail_count=$((fail_count + 1))
    else
        printf "${RED}FAIL${NC} %s — expected exit !=0 with [typecheck], got rc=%d\n" "$f" "$rc"
        echo "$out" | sed 's/^/    /'
        failures=$((failures + 1))
    fi
done

echo ""
if [ $failures -ne 0 ]; then
    printf "${RED}%d typeinfer test(s) failed.${NC}\n" "$failures"
    exit 1
fi
printf "${GREEN}All typeinfer tests passed${NC} (%d pass, %d fail-fixtures rejected)\n" \
       "$pass_count" "$fail_count"
