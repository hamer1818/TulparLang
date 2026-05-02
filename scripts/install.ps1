# TulparLang installer — Windows
#
# Usage (one-liner):
#   iwr -useb https://raw.githubusercontent.com/hamer1818/TulparLang/main/scripts/install.ps1 | iex
#
# What this does:
#   1. Queries the latest GitHub release.
#   2. Downloads tulpar-windows-x64.exe to %LOCALAPPDATA%\Programs\Tulpar\tulpar.exe.
#   3. Adds that directory to the user-level PATH (no admin required).
#
# Re-running this script upgrades the installed tulpar to the latest release.

#Requires -Version 5
$ErrorActionPreference = 'Stop'

# Force UTF-8 on the console so the Turkish characters and box-drawing
# glyphs in this script (`→`, `✓`, `ü`, `İ`, ...) render correctly. When
# the script is run via `iwr | iex`, PowerShell inherits the parent
# console's output encoding — which on a fresh Windows is the legacy OEM
# code page (CP437/CP850), turning every multi-byte UTF-8 character into
# `?`. Setting these here is per-process and does not leak to the user's
# shell after the installer exits.
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    [Console]::InputEncoding  = [System.Text.Encoding]::UTF8
    $OutputEncoding           = [System.Text.Encoding]::UTF8
} catch {
    # Old Windows / sandboxed hosts may refuse — fall through, the script
    # will still work, only the diacritics in messages will be munged.
}

$Repo       = 'hamer1818/TulparLang'
$AssetName  = 'tulpar-windows-x64.exe'
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\Tulpar'
$BinaryPath = Join-Path $InstallDir 'tulpar.exe'

function Write-Step($msg)    { Write-Host "→ $msg" -ForegroundColor Cyan }
function Write-Success($msg) { Write-Host "✓ $msg" -ForegroundColor Green }
function Write-Note($msg)    { Write-Host "  $msg" -ForegroundColor DarkGray }
function Write-Warn($msg)    { Write-Host "! $msg" -ForegroundColor Yellow }

Write-Host ""
Write-Host "TulparLang installer" -ForegroundColor Cyan
Write-Host "===================="

# 1. Find the latest release.
Write-Step "GitHub'dan son sürüm sorgulanıyor..."
$apiUrl = "https://api.github.com/repos/$Repo/releases/latest"
try {
    $release = Invoke-RestMethod -Uri $apiUrl -Headers @{ 'User-Agent' = 'tulpar-installer' }
} catch {
    throw "Sürüm bilgisi alınamadı ($apiUrl): $_"
}
$tag = $release.tag_name
$asset = $release.assets | Where-Object { $_.name -eq $AssetName } | Select-Object -First 1
if (-not $asset) {
    throw "Release '$tag' içinde '$AssetName' bulunamadı."
}
Write-Note "Son sürüm: $tag"

# 2. Download the binary. Windows can't overwrite a running .exe, so if a
#    previous install is on-disk we rename it out of the way first; that
#    rename works even while the process is running, and the .old file is
#    removed on success.
Write-Step "İndiriliyor: $BinaryPath"
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$oldPath = "$BinaryPath.old"
if (Test-Path $oldPath) { Remove-Item $oldPath -Force -ErrorAction SilentlyContinue }
if (Test-Path $BinaryPath) {
    try { Move-Item -Path $BinaryPath -Destination $oldPath -Force }
    catch {
        throw "Mevcut tulpar.exe taşınamadı (başka bir process tarafından kilitleniyor olabilir): $_"
    }
}
try {
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $BinaryPath -UseBasicParsing
} catch {
    # Restore previous on download failure so the user isn't left without a tulpar.
    if (Test-Path $oldPath) { Move-Item -Path $oldPath -Destination $BinaryPath -Force }
    throw "İndirme başarısız: $_"
}
if (Test-Path $oldPath) { Remove-Item $oldPath -Force -ErrorAction SilentlyContinue }

# 3. Wire the install dir into the user PATH if it isn't already there.
#    We deliberately use the User scope (no admin needed) and update the
#    persistent registry value AND the current-session $env:Path so the
#    user can immediately run `tulpar` without restarting their terminal.
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$entries  = if ($userPath) { $userPath -split ';' | Where-Object { $_ } } else { @() }
if ($entries -notcontains $InstallDir) {
    Write-Step "PATH güncelleniyor: $InstallDir"
    $newPath = if ($userPath) { "$userPath;$InstallDir" } else { $InstallDir }
    [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
    $env:Path = "$env:Path;$InstallDir"
    Write-Note "Yeni terminallerde otomatik geçerli olur."
} else {
    Write-Step "PATH zaten ayarlı."
}

# 4. Smoke test.
$version = & $BinaryPath --version 2>$null
if (-not $version) { $version = $tag }

Write-Host ""
Write-Success "TulparLang $tag kuruldu → $BinaryPath"
Write-Host ""
Write-Host "Deneme:" -ForegroundColor Cyan
Write-Host "  tulpar --version"
Write-Host "  tulpar --repl"
Write-Host ""
Write-Host "Güncellemek için:" -ForegroundColor Cyan
Write-Host "  tulpar update            # built-in (varsa)"
Write-Host "  veya bu installer'ı yeniden çalıştır."
