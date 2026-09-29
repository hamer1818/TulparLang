#!/usr/bin/env bash
# Wings <-> Node.js HTTP karsilastirmasi — TUR ESLI, ayni makinede ayni anda.
#
# NEDEN: STATUS.md "Node.js'i 1.7-2.1x geciyor" diyordu ama o sayinin kaynagi
# bayat (2026-05/06 tablolari, WSL2); tek HTTP tablosu benchmarks/RESULTS.md
# kendisi "BU TABLO GUVENILIR DEGIL" diyor. Olgunluk olcutu 2 ("HTTP Node'un
# >2 kati") guncel ikiliyle hic olculmemisti (K220).
#
# ADIL ESLEME: ayni isi yapan iki cift, her biri ayni eszamanlilik modeli:
#   evented (tek is parcacigi, poll)   <->  node http (tek is parcacigi, libuv)
#   pool    (CPU basina accept-isci)   <->  node cluster (CPU basina isci)
# Ayni yanitlar: /ping -> "pong" (text/plain), /users -> 3 kayitli JSON.
# Olcum araci her iki tarafta ayni: benchmarks/loadtest.c (keep-alive).
#
# TUR ESLEME (Tuzaklar 1n): iki kol AYRI zamanlarda olculurse oran copur.
# Her tur iki kolu arka arkaya olcer, R tur yapilir, ORTANCA raporlanir.
#
# Istemci (loadtest) AYNI makinede kosuyor ve cekirdekleri sunucuyla
# PAYLASIYOR — mutlak RPS makineye ve istemci yukune bagli; oran iki
# sunucuya ayni kosulda bakar. Sonuc satirlari tarih + makine + surumlerle
# basilir; STATUS/README'ye yalniz boyle yazilir.
#
# Kullanim: benchmarks/http_vs_node.sh [tur=3] [sure_sn=5] [conc=50]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
ROUNDS="${1:-3}"; DUR="${2:-5}"; CONC="${3:-50}"
TPORT=8585; NPORT=8586
command -v node >/dev/null || { echo "node yok — olcum yapilamaz"; exit 2; }
[ -x "$ROOT/tulpar" ] || { echo "./tulpar yok — once ./build.sh"; exit 2; }
TMP=$(mktemp -d)
cc -O2 -pthread -o "$TMP/loadtest" "$HERE/loadtest.c" || exit 2

cat > "$TMP/node_server.js" <<'JS'
const http = require('http');
const cluster = require('cluster');
const os = require('os');
const users = JSON.stringify({users: [{id: 1, name: "Ada"}, {id: 2, name: "Linus"},
                                      {id: 3, name: "Grace"}], count: 3});
const port = +process.argv[2], mode = process.argv[3];
// Content-Length ACIKCA: writeHead + end ile Node chunked kodlama seciyor;
// loadtest.c (ve Wings) Content-Length ile cerceveliyor — ilk olcumde
// Node'un her cevabi bu yuzden "err" sayildi (olculdu 2026-09-28).
const PONG = Buffer.from('pong'), USERS = Buffer.from(users);
function serve() {
  http.createServer((req, res) => {
    if (req.url === '/ping') { res.writeHead(200, {'Content-Type': 'text/plain', 'Content-Length': PONG.length}); res.end(PONG); }
    else if (req.url === '/users') { res.writeHead(200, {'Content-Type': 'application/json', 'Content-Length': USERS.length}); res.end(USERS); }
    else { res.writeHead(404, {'Content-Length': 0}); res.end(); }
  }).listen(port, '127.0.0.1');
}
if (mode === 'cluster' && cluster.isPrimary) { for (let i = 0; i < os.cpus().length; i++) cluster.fork(); }
else serve();
JS

PIDS=()
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; fuser -k "$TPORT/tcp" "$NPORT/tcp" >/dev/null 2>&1; rm -rf "$TMP"; }
trap cleanup EXIT

wait_port() { for i in $(seq 1 120); do ss -ltn 2>/dev/null | grep -q ":$1 " && return 0; sleep 0.5; done; return 1; }

# rps <port> <path> -> RPS (tamsayi). YANIT DENETIMI: 2xx disi ya da hata
# varsa sayi YERINE "HATALI(...)" doner — hizli ama yanlis cevap veren bir
# sunucu kiyasta "kazanmasin" (6i: hizli yol ile yavas yol ayni isi yapmali).
rps() {
  local out
  out=$("$TMP/loadtest" 127.0.0.1 "$1" "$CONC" "$DUR" GET "$2" "" keepalive 2>/dev/null)
  local other err
  other=$(echo "$out" | grep -oE 'other=[0-9]+' | cut -d= -f2)
  err=$(echo "$out" | grep -oE 'err=[0-9]+' | cut -d= -f2)
  if [ "${other:-1}" != "0" ] || [ "${err:-1}" != "0" ]; then
    echo "HATALI(other=$other,err=$err)"; return
  fi
  echo "$out" | grep -E 'RPS' | grep -oE '[0-9]+' | head -1
}

median() {
  if printf '%s\n' "$@" | grep -q HATALI; then printf '%s\n' "$@" | grep HATALI | head -1; return; fi
  printf '%s\n' "$@" | sort -n | awk '{a[NR]=$1} END{print (NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2}'
}

echo "== makine: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //') · $(nproc) CPU · $(date -u +%F)"
echo "== node $(node --version) · tulpar $("$ROOT/tulpar" version | head -1) · $ROUNDS tur x ${DUR}s · conc=$CONC keep-alive"

for pair in "evented:single" "pool:cluster"; do
  tmode="${pair%%:*}"; nmode="${pair##*:}"
  ( cd "$ROOT" && TULPAR_WINGS_HOST=127.0.0.1 TULPAR_WINGS_NODOCS=1 WINGS_MODE="$tmode" \
      DISPLAY= ./tulpar benchmarks/stress_server.tpr < /dev/null > "$TMP/t_$tmode.log" 2>&1 ) &
  PIDS+=($!)
  node "$TMP/node_server.js" "$NPORT" "$nmode" < /dev/null > "$TMP/n.log" 2>&1 &
  PIDS+=($!)
  wait_port "$TPORT" || { echo "tulpar sunucusu acilmadi"; tail -5 "$TMP/t_$tmode.log"; exit 1; }
  wait_port "$NPORT" || { echo "node sunucusu acilmadi"; exit 1; }
  sleep 1
  # ON DENETIM: iki sunucu da ayni cevabi veriyor mu? Ilk yazimda node arka
  # planda SIGTTIN ile DURMUSTU (stdin bir TTY) ve olculen 54k "RPS"nin
  # tamami baglanti hatasiydi — oran bos bir sayiydi (olculdu 2026-09-28).
  # Artik stdin /dev/null ve olcumden once govde karsilastiriliyor.
  for path in /ping /users; do
    bt=$(curl -s --max-time 3 "http://127.0.0.1:$TPORT$path"); bn=$(curl -s --max-time 3 "http://127.0.0.1:$NPORT$path")
    case "$path" in
      /ping) ok_t=$([ "$bt" = "pong" ] && echo 1); ok_n=$([ "$bn" = "pong" ] && echo 1) ;;
      *) ok_t=$(echo "$bt" | grep -q '"Grace"' && echo 1); ok_n=$(echo "$bn" | grep -q '"Grace"' && echo 1) ;;
    esac
    if [ -z "$ok_t" ] || [ -z "$ok_n" ]; then
      echo "ON DENETIM DUSTU $path: tulpar='${bt:0:60}' node='${bn:0:60}'"; exit 1
    fi
  done
  for path in /ping /users; do
    T=(); N=()
    for r in $(seq 1 "$ROUNDS"); do
      T+=("$(rps "$TPORT" "$path")"); N+=("$(rps "$NPORT" "$path")")
    done
    mt=$(median "${T[@]}"); mn=$(median "${N[@]}")
    if echo "$mt$mn" | grep -q HATALI || [ -z "$mt" ] || [ -z "$mn" ]; then
      ratio="GECERSIZ"
    else
      ratio=$(awk -v a="$mt" -v b="$mn" 'BEGIN{ printf "%.2f", a/b }')
    fi
    printf '%-8s <-> node %-8s %-7s  tulpar %9s RPS  node %9s RPS  oran %sx  (turlar T:%s N:%s)\n' \
      "$tmode" "$nmode" "$path" "$mt" "$mn" "$ratio" "${T[*]}" "${N[*]}"
  done
  fuser -k "$TPORT/tcp" "$NPORT/tcp" >/dev/null 2>&1; pkill -f "node $TMP/node_server.js" 2>/dev/null; sleep 1
done
