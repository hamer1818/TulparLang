#!/usr/bin/env bash
# WINDOWS KAYNAK IZI — CI adimi kosarken her 5 s'de bos fiziksel bellegi ve
# calisma kumesi en buyuk 4 sureci bir dosyaya yazar.
#
# NIYE (2026-10-05): Windows isinin "Ornekler (build.sh test)" adimi uc kez
# runner kaybetti — sonuncusu 10 dk sinirina ragmen 37 dk asili kaldi ve
# force-cancel gerekti (#467, kosum 37316074588). Hangi surecin bellegi ya da
# CPU'yu tukettigi hic olculmuyordu; runner olunce adimin kendi gunlugu da
# gidiyor. Iz ayri bir dosyada ve ayri bir `if: always()` adimi onu basiyor.
#
#   tools/win_kaynak_izle.sh <log> &   (adim sonunda kill)
log=${1:?log dosyasi}
: > "$log"
while :; do
    powershell -NoProfile -NonInteractive -Command '
      $t = Get-Date -Format HH:mm:ss
      $os = Get-CimInstance Win32_OperatingSystem
      $bos = [int]($os.FreePhysicalMemory / 1024)
      $ust = (Get-Process | Sort-Object WS -Descending | Select-Object -First 4 |
              ForEach-Object { "{0}({1})={2}MB" -f $_.Name, $_.Id, [int]($_.WS / 1MB) }) -join " "
      $n = (Get-Process -Name tulpar,clang*,ld* -ErrorAction SilentlyContinue | Measure-Object).Count
      Write-Output "$t bos=${bos}MB derleyici_surec=$n $ust"' >> "$log" 2>&1
    sleep 5
done
