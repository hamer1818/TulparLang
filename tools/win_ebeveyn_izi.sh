#!/usr/bin/env bash
# WINDOWS EBEVEYN ZINCIRI — verilen MSYS2 surecinden (varsayilan: cagiran
# kabuk) yukari dogru her Windows atasini basar ve ilk OLU (ya da PID'i
# baska bir surece gecmis) ebeveyni isaretler.
#
# NIYE (2026-10-06): Windows CI "Ornekler" adimi bir kez 3840 ile kesildi
# (PR #470, is 111891244750). 3840 = 0x0F00: MSYS2 bir surec SINYALLE olunce
# Cygwin-disi ebeveyne bildirdigi kodu baytlarini cevirerek yaziyor
# (msys2-runtime pinfo::exit) — 0x0F = sinyal 15 (SIGTERM; cozum her kosumda
# tools/win_sinyal_kodu.ps1 ile olculuyor). Adimda SIGTERM gonderen yer
# build.sh'in smoke `kill -TERM`'u; msys2-runtime o sinyali yerel .exe'ye
# exit_process_tree() ile iletiyor ve "cocuklari" th32ParentProcessID ile
# topluyor. Bu alan ebeveyn olunce BAYATLAR (PID yeniden kullanilir, yaratilma
# zamani karsilastirilmiyor): bayat ebeveynli bir ata, o PID'i yeniden almis
# smoke ikilisinin "cocugu" sayilir ve onun altindaki MSYS2 grup lideri
# (adimin dis kabugu) SIGTERM yer. Bu zincir o onkosulu olcer. build.sh smoke'u
# artik Windows PID'iyle durduruyor (smoke_durdur), yani onkosul tek basina
# zararsiz.
#
#   tools/win_ebeveyn_izi.sh [msys_pid]
pid=${1:-$PPID}
wp=$(cat "/proc/$pid/winpid" 2>/dev/null) || { echo "[ebeveyn izi] /proc/$pid/winpid yok"; exit 0; }
powershell -NoProfile -NonInteractive -Command "
  \$id = $wp; \$out = @(); \$durum = 'kok'
  for (\$k = 0; \$k -lt 10; \$k++) {
    \$c = Get-CimInstance Win32_Process -Filter \"ProcessId=\$id\"
    if (-not \$c) { \$durum = 'zincir koptu'; break }
    \$out += ('' + \$c.Name + '(' + \$id + ')')
    \$pp = \$c.ParentProcessId
    \$e = Get-CimInstance Win32_Process -Filter \"ProcessId=\$pp\"
    if (-not \$e) { \$durum = 'ebeveyn ' + \$pp + ' OLU (PID bos)'; break }
    if (\$e.CreationDate -gt \$c.CreationDate) { \$durum = 'ebeveyn ' + \$pp + ' OLU, PID yeniden kullanildi: ' + \$e.Name; break }
    \$id = \$pp
  }
  Write-Output ('[ebeveyn izi] msys pid $pid: ' + (\$out -join ' <- ') + ' <- ' + \$durum)
" 2>&1
