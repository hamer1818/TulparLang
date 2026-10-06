# MSYS2 CIKIS KODU COZUMU — POZITIF KONTROL (2026-10-06).
# Cygwin-disi bir ebeveynin (Actions runner'i, cmd, PowerShell) gordugu
# Windows cikis kodu, MSYS2 sureci normal cikinca durum kodunun KENDISI,
# sinyalle olunce `sinyal << 8` (msys2-runtime pinfo::exit baytlari ceviriyor).
# Windows "Ornekler" adiminin 3840'i (0x0F00) bu cozumle SIGTERM okundu;
# bu betik cozumu her kosumda olcer: beklenen 3 / 3840; SIGKILL yalniz bilgi.
#   powershell -NoProfile -File tools/win_sinyal_kodu.ps1 <bash.exe Windows yolu>
$b = $args[0]
& $b -c 'exit 3'
$normal = $LASTEXITCODE
& $b -c 'kill -TERM $$'
$term = $LASTEXITCODE
& $b -c 'kill -KILL $$'
$kill = $LASTEXITCODE
$ok = ($normal -eq 3) -and ($term -eq 3840)
Write-Output "[sinyal kodu] exit 3 -> $normal, SIGTERM -> $term, SIGKILL -> $kill (beklenen 3 / 3840): $(if ($ok) { 'UYUMLU' } else { 'UYUMSUZ' })"
if (-not $ok) { exit 1 }
