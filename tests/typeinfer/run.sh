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
# Every fail fixture MUST carry one or more
#   // EXPECT: <substring>
# lines. Each substring must appear in the output. Without them a fixture
# only proves that SOMETHING was rejected, not that the right thing was —
# and that is not hypothetical: two deliberate breakages of the
# function-reference diagnostic were caught by neither fixture, because a
# different (misleading) error still made the exit code non-zero.
# Until 2026-09-27 EXPECT was optional and 8 of 19 fail fixtures had none
# (a wrong-reason rejection kept them green); a fixture without EXPECT is
# now itself a failure, so the gap cannot silently reopen.
#
# Run from repo root: `./tests/typeinfer/run.sh`. The runner expects
# `./tulpar` (or `./tulpar.exe` on Git Bash) to exist and be built.

set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# DİLİ SABİTLE. Tanı mesajları `tr_en` üzerinden geçiyor ve dil LC_ALL /
# LC_MESSAGES / LANG'a bakıyor; sabitlemezsek EXPECT satırları geliştiricinin
# yerel ayarına göre tutar ya da tutmaz. Bu varsayım değil ölçüm: Türkçe
# yerel ayarlı bir makinede yeşil olan bir fixture CI'da (İngilizce)
# kırmızıya döndü.
export LC_ALL=C

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
    if ! grep -q '^// *EXPECT: *[^ ]' "$f"; then
        printf "${RED}FAIL${NC} %s — '// EXPECT:' satiri YOK (yanlis sebeple reddedilse de yesil kalirdi)\n" "$f"
        failures=$((failures + 1))
        continue
    fi
    if [ $rc -ne 0 ] && echo "$out" | grep -q '\[typecheck\]'; then
        # EXPECT: satırları varsa MESAJ da denetleniyor — yalnız reddedilmiş
        # olmak, DOĞRU sebeple reddedilmiş olmak demek değil.
        missing=""
        while IFS= read -r want; do
            [ -n "$want" ] || continue
            if ! echo "$out" | grep -qF -- "$want"; then
                missing="$missing\n      beklenen: $want"
            fi
        # `tr -d '\r'`: CRLF'li fikstürde (01-03 öyle) beklenen metin sonda
        # bir CR taşır ve çıktıda HİÇ bulunamaz — fikstür doğru tanıyla
        # kırmızı kalırdı (ölçüldü 2026-09-27, EXPECT ilk eklendiğinde).
        done <<< "$(sed -n 's|^// *EXPECT: *||p' "$f" | tr -d '\r')"
        if [ -n "$missing" ]; then
            printf "${RED}FAIL${NC} %s — reddedildi ama BEKLENEN MESAJ yok%b\n" \
                   "$f" "$missing"
            echo "$out" | sed 's/^/    /'
            failures=$((failures + 1))
        else
            printf "${GREEN}PASS${NC} %s (rejected as expected)\n" "$f"
            fail_count=$((fail_count + 1))
        fi
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
