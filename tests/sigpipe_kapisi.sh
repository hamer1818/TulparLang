#!/usr/bin/env bash
# SIGPIPE kapisi: karsi tarafin RST ile kapattigi bir sokete yazmak AG
# KULLANAN bir Tulpar surecini oldurmemeli (socket_send -1 donmeli), ama ag
# kullanmayan bir programda `| head` eskisi gibi sureci bitirmeli.
#
# Neden var: 2026-11-25'e kadar SIGPIPE hic ele alinmiyordu. smtp_smoke'un
# bir sabotaj kosusunda istemci cikis kodu 141 (SIGPIPE) ile oldu; ayni sey
# kapanmis bir istemciye iki kez yazan her Wings sunucusunu da oldururdu.
# Duzeltme: src/common/platform_sockets.h tulpar_net_ignore_sigpipe().
#
# Ters kontrol: TULPAR_SIGPIPE_SABOTAJ=1 ile bekleneni tersine cevirir —
# duzeltme geri alinirsa (ya da cagri yerleri silinirse) bu betik kirmizi.
set -u
cd "$(dirname "$0")/.."
TULPAR="${TULPAR_EXE:-./tulpar}"
[ -x "$TULPAR" ] || { echo "FAIL: $TULPAR yok"; exit 1; }
command -v python3 >/dev/null || { echo "SKIP: python3 yok"; exit 0; }
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) echo "SKIP: Windows'ta SIGPIPE yok"; exit 0 ;; esac

T=$(mktemp -d)
trap 'rm -rf "$T"; [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null' EXIT

# RST ile kapatan sunucu: kabul et, SO_LINGER 0, kapat.
cat > "$T/rst.py" <<'EOF'
import socket, struct, sys
ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(("127.0.0.1", 0)); ls.listen(4)
print(ls.getsockname()[1], flush=True)
while True:
    c, _ = ls.accept()
    c.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    c.close()
EOF
python3 "$T/rst.py" > "$T/port" &
SRV=$!
for _ in $(seq 50); do [ -s "$T/port" ] && break; sleep 0.1; done
PORT=$(cat "$T/port")

cat > "$T/yaz.tpr" <<EOF
int fd = socket_client("127.0.0.1", $PORT);
sleep(200);
int i = 0;
int last = 0;
while (i < 50) {
    last = socket_send(fd, "veri veri veri veri\r\n");
    i = i + 1;
}
print("hayatta son=" + toString(last));
EOF
OUT=$(DISPLAY= "$TULPAR" "$T/yaz.tpr" 2>&1); RC=$?

cat > "$T/bas.tpr" <<'EOF'
int i = 0;
while (i < 2000000) {
    print("satir");
    i = i + 1;
}
EOF
"$TULPAR" build "$T/bas.tpr" "$T/bas" >/dev/null 2>&1 || { echo "FAIL: bas.tpr derlenmedi"; exit 1; }
# Ag kullanmayan program: head 1 satir okuyup kapatinca SIGPIPE ile hemen bitmeli.
set -o pipefail
timeout 20 "$T/bas" | head -1 >/dev/null
PIPE_RC=$?
set +o pipefail

ok=1
if [ "$RC" -ne 0 ] || [[ "$OUT" != *"hayatta son=-1"* ]]; then
    echo "  - ag yazan program SIGPIPE'tan sag cikmadi (rc=$RC, cikti: $OUT)"; ok=0
fi
# 141 = 128+SIGPIPE (bas), pipefail onu tasir; 124 = timeout (SIGPIPE yok sayilmis).
if [ "$PIPE_RC" -ne 141 ]; then
    echo "  - ag kullanmayan program '| head' ile SIGPIPE'ta bitmedi (rc=$PIPE_RC)"; ok=0
fi
if [ "${TULPAR_SIGPIPE_SABOTAJ:-0}" = "1" ]; then
    [ "$ok" = 1 ] && { echo "FAIL: sabotaj kosusu yesil kaldi"; exit 1; }
    echo "PASS: sabotaj kirmiziya dondu"; exit 0
fi
[ "$ok" = 1 ] || { echo "FAIL: sigpipe_kapisi"; exit 1; }
echo "PASS: sigpipe_kapisi — RST'li sokete 50 yazma: surec sag, socket_send -1; ag'siz program | head ile 141"
