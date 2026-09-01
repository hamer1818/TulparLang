#!/bin/bash
# Donanım kaynak göstergesi — test/derleme koşumlarının NEYE MAL OLDUĞUNU
# gösterir: RAM, CPU frekansı, CPU sıcaklığı, GPU kullanımı ve sıcaklığı.
#
# Neden zamanla örnekleme: tek bir anlık okuma yanıltıcı. Testler dalgalı bir
# yük; asıl merak edilen ZİRVE (bellek tavana vurdu mu, CPU throttle etti mi).
# O yüzden arka planda örnekleniyor ve sonunda zirve + ortalama bildiriliyor.
#
# Her sonda AYRI AYRI korumalı: bu makinede olan bir arayüz (nvidia-smi,
# lm-sensors) başka makinede olmayabilir ve CI'da hiçbiri yok. Eksik olan
# sonda sessizce atlanıyor, koşum ASLA bundan dolayı düşmüyor.
#
# Kullanım:
#   tools/hwstat.sh snap                 tek satırlık anlık durum
#   tools/hwstat.sh start <dosya>        arka planda örneklemeye başla (pid basar)
#   tools/hwstat.sh stop <pid> <dosya> <etiket>   durdur + özet yaz
#
# TULPAR_NO_HWSTAT=1 ile tamamen susturulur.

set -u

# --- Sondalar ---------------------------------------------------------------
# Hepsi "değer yoksa boş string" sözleşmesine uyuyor.

hw_ram_used_mb() {
    [ -r /proc/meminfo ] || return 0
    awk '/^MemTotal:/{t=$2} /^MemAvailable:/{a=$2}
         END{ if (t>0 && a!="") printf "%d", (t-a)/1024 }' /proc/meminfo
}

hw_ram_total_mb() {
    [ -r /proc/meminfo ] || return 0
    awk '/^MemTotal:/{printf "%d", $2/1024; exit}' /proc/meminfo
}

# Bütün çekirdeklerin ORTALAMA frekansı (GHz). Tek çekirdek okumak yanıltıcı:
# boşta olan bir çekirdek düşük frekansta durur, oysa merak edilen yükün
# altındaki genel hız.
hw_cpu_ghz() {
    local sum=0 n=0 f
    for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_cur_freq; do
        [ -r "$f" ] || continue
        local v; v=$(cat "$f" 2>/dev/null) || continue
        case "$v" in ''|*[!0-9]*) continue ;; esac
        sum=$((sum + v)); n=$((n + 1))
    done
    if [ "$n" -gt 0 ]; then
        awk -v s="$sum" -v n="$n" 'BEGIN{ printf "%.2f", s/n/1000000 }'
    elif [ -r /proc/cpuinfo ]; then
        awk '/^cpu MHz/{s+=$4; n++} END{ if(n>0) printf "%.2f", s/n/1000 }' /proc/cpuinfo
    fi
}

# CPU paket sıcaklığı. Önce thermal_zone (çoğu makinede var), yoksa
# lm-sensors. AMD'de Tctl, Intel'de "Package id 0" paket sıcaklığıdır.
hw_cpu_temp() {
    local z t
    for z in /sys/class/thermal/thermal_zone*; do
        [ -r "$z/type" ] && [ -r "$z/temp" ] || continue
        case "$(cat "$z/type" 2>/dev/null)" in
            x86_pkg_temp|cpu-thermal|acpitz)
                t=$(cat "$z/temp" 2>/dev/null)
                case "$t" in ''|*[!0-9]*) continue ;; esac
                awk -v t="$t" 'BEGIN{ printf "%.1f", t/1000 }'
                return 0 ;;
        esac
    done
    command -v sensors >/dev/null 2>&1 || return 0
    sensors 2>/dev/null | awk '
        /^(Tctl|Package id 0):/ { gsub(/[+°C]/,"",$2); printf "%.1f", $2; exit }'
}

# GPU: şimdilik yalnız NVIDIA (bu makinede o var). AMD/Intel sondaları
# eklenene kadar sessizce atlanıyor — yanlış bir değer basmaktansa hiç
# basmamak doğru.
hw_gpu() {
    command -v nvidia-smi >/dev/null 2>&1 || return 0
    nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu \
               --format=csv,noheader,nounits 2>/dev/null | head -1 |
        tr -d ' '
}

# --- Anlık satır ------------------------------------------------------------
hw_snap_line() {
    local ram tot ghz temp gpu out=""
    ram=$(hw_ram_used_mb); tot=$(hw_ram_total_mb)
    [ -n "$ram" ] && out="RAM ${ram}/${tot} MB"
    ghz=$(hw_cpu_ghz);  [ -n "$ghz" ]  && out="$out  CPU ${ghz} GHz"
    temp=$(hw_cpu_temp); [ -n "$temp" ] && out="$out  ${temp}°C"
    gpu=$(hw_gpu)
    if [ -n "$gpu" ]; then
        local gu gm gt gtot
        gu=$(echo "$gpu" | cut -d, -f1); gm=$(echo "$gpu" | cut -d, -f2)
        gtot=$(echo "$gpu" | cut -d, -f3); gt=$(echo "$gpu" | cut -d, -f4)
        out="$out  GPU %${gu} ${gm}/${gtot} MB ${gt}°C"
    fi
    echo "$out"
}

# --- Örnekleme --------------------------------------------------------------
# Satır biçimi: ram cpughz cputemp gpuutil gputemp  (eksikler "-")
hw_sample_row() {
    local ram ghz temp gpu gu gt
    ram=$(hw_ram_used_mb);  [ -z "$ram" ] && ram="-"
    ghz=$(hw_cpu_ghz);      [ -z "$ghz" ] && ghz="-"
    temp=$(hw_cpu_temp);    [ -z "$temp" ] && temp="-"
    gpu=$(hw_gpu)
    if [ -n "$gpu" ]; then
        gu=$(echo "$gpu" | cut -d, -f1); gt=$(echo "$gpu" | cut -d, -f4)
    else
        gu="-"; gt="-"
    fi
    echo "$ram $ghz $temp $gu $gt"
}

# --- Makine künyesi (bir kez basılır) ---------------------------------------
hw_machine_line() {
    local cpu cores ram gpu
    cpu=$(awk -F': ' '/^model name/{print $2; exit}' /proc/cpuinfo 2>/dev/null)
    [ -z "$cpu" ] && cpu=$(uname -m)
    cores=$(nproc 2>/dev/null)
    ram=$(hw_ram_total_mb)
    gpu=$(command -v nvidia-smi >/dev/null 2>&1 &&
          nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1)
    local out="  [makine] $cpu"
    [ -n "$cores" ] && out="$out (${cores} cekirdek)"
    [ -n "$ram" ] && out="$out  RAM $((ram / 1024)) GB"
    [ -n "$gpu" ] && out="$out  GPU $gpu"
    echo "$out"
}

case "${1:-snap}" in
    info)
        [ "${TULPAR_NO_HWSTAT:-0}" = "1" ] && exit 0
        hw_machine_line
        ;;
    snap)
        [ "${TULPAR_NO_HWSTAT:-0}" = "1" ] && exit 0
        hw_snap_line
        ;;
    start)
        [ "${TULPAR_NO_HWSTAT:-0}" = "1" ] && exit 0
        out="${2:?ornek dosyasi gerekli}"
        : > "$out"
        (
            while :; do
                hw_sample_row >> "$out" 2>/dev/null
                sleep 2
            done
        ) >/dev/null 2>&1 &
        echo $!
        ;;
    stop)
        [ "${TULPAR_NO_HWSTAT:-0}" = "1" ] && exit 0
        pid="${2:-}"; out="${3:-}"; label="${4:-kosum}"
        [ -n "$pid" ] && kill "$pid" 2>/dev/null
        [ -n "$out" ] && [ -s "$out" ] || exit 0
        awk -v label="$label" '
            function upd(v, i) {
                if (v == "-") return
                sum[i] += v; n[i]++
                if (max[i] == "" || v+0 > max[i]+0) max[i] = v
            }
            { upd($1,1); upd($2,2); upd($3,3); upd($4,4); upd($5,5) }
            END {
                s = "  [kaynak] " label ":"
                if (n[1]) s = s sprintf("  RAM zirve %d MB (ort %d)", max[1], sum[1]/n[1])
                if (n[2]) s = s sprintf("  CPU %.2f GHz ort", sum[2]/n[2])
                if (n[3]) s = s sprintf("  CPU zirve %.1f°C", max[3])
                if (n[4]) s = s sprintf("  GPU zirve %%%d", max[4])
                if (n[5]) s = s sprintf(" / %d°C", max[5])
                s = s sprintf("   (%d ornek)", n[1] ? n[1] : NR)
                print s
            }' "$out"
        rm -f "$out"
        ;;
    *)
        echo "kullanim: $0 {snap|start <dosya>|stop <pid> <dosya> <etiket>}" >&2
        exit 2
        ;;
esac
