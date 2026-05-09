# Generates Inno Setup branding assets from tulpar-logo.png:
#   * tulpar.ico        — multi-res icon (16, 24, 32, 48, 64, 128, 256)
#   * wizard-image.bmp  — 164x314 vertical strip (Welcome/Finish pages)
#   * wizard-small.bmp  — 55x58 corner glyph (every wizard page)
#
# Run from repo root:  pwsh installer/assets/build_assets.ps1
# Re-run whenever installer/assets/tulpar-logo.png changes.
#
# Pure PowerShell + System.Drawing — no ImageMagick dep so the installer
# rebuild works on any Windows box that already builds the project.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$here = Split-Path -Parent $PSCommandPath
$src  = Join-Path $here 'tulpar-logo.png'
if (-not (Test-Path $src)) { throw "Missing $src" }

# Brand teal pulled from the logo glyph itself (~$2BB6B6). The wizard
# panels paint on top of this so the pegasus blends with the chrome
# instead of sitting on a stark-white rectangle.
$teal       = [System.Drawing.Color]::FromArgb(43, 182, 182)
$tealDark   = [System.Drawing.Color]::FromArgb(28, 138, 138)
$ink        = [System.Drawing.Color]::FromArgb(255, 255, 255)
$inkSubtle  = [System.Drawing.Color]::FromArgb(220, 245, 245)

function New-ResizedBitmap {
    param([System.Drawing.Image]$Image, [int]$Width, [int]$Height)
    $bmp = New-Object System.Drawing.Bitmap $Width, $Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.DrawImage($Image, (New-Object System.Drawing.Rectangle 0, 0, $Width, $Height))
    $g.Dispose()
    return $bmp
}

# The source logo is teal pixels on transparent. Painted on the wizard's
# teal panel it nearly disappears — so we recolour every visible pixel to
# white while preserving the existing alpha. Cheap per-pixel walk on a
# 1024x1024 source is plenty fast.
function ConvertTo-WhiteGlyph {
    param([System.Drawing.Bitmap]$Bitmap)
    $w = $Bitmap.Width
    $h = $Bitmap.Height
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $Bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $bytes  = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $i = $row + ($x * 4)
            # BGRA layout. Leave alpha alone, paint colour to white.
            if ($bytes[$i + 3] -ne 0) {
                $bytes[$i]     = 255  # B
                $bytes[$i + 1] = 255  # G
                $bytes[$i + 2] = 255  # R
            }
        }
    }
    [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
    $Bitmap.UnlockBits($data)
}

# ---- ICO (multi-resolution, PNG-encoded entries) -----------------------
# Modern Windows accepts PNG payloads inside ICO entries. Embedding PNG
# instead of BMP keeps file size sane (the 256x256 alone would be ~256 KB
# uncompressed) and preserves the alpha channel cleanly.
function Write-Ico {
    param([string]$OutPath, [int[]]$Sizes, [System.Drawing.Image]$SourceImage)

    $entries = @()
    foreach ($size in $Sizes) {
        $bmp = New-ResizedBitmap -Image $SourceImage -Width $size -Height $size
        $ms  = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $entries += [pscustomobject]@{ Size = $size; Bytes = $ms.ToArray() }
    }

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    # ICONDIR
    $bw.Write([uint16]0)            # reserved
    $bw.Write([uint16]1)            # type = icon
    $bw.Write([uint16]$entries.Count)

    # Reserve directory entries; write them after we know offsets.
    $dirEntrySize = 16
    $dataOffset   = 6 + ($entries.Count * $dirEntrySize)
    foreach ($e in $entries) {
        $w = if ($e.Size -ge 256) { 0 } else { [byte]$e.Size }
        $h = $w
        $bw.Write([byte]$w)
        $bw.Write([byte]$h)
        $bw.Write([byte]0)          # palette
        $bw.Write([byte]0)          # reserved
        $bw.Write([uint16]1)        # planes
        $bw.Write([uint16]32)       # bpp
        $bw.Write([uint32]$e.Bytes.Length)
        $bw.Write([uint32]$dataOffset)
        $dataOffset += $e.Bytes.Length
    }
    foreach ($e in $entries) { $bw.Write($e.Bytes) }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($OutPath, $ms.ToArray())
    $bw.Dispose()
}

# ---- BMP helpers -------------------------------------------------------
function Save-Bmp24 {
    param([System.Drawing.Bitmap]$Bitmap, [string]$Path)
    # Inno Setup wants a plain 24-bit BMP — re-encode through a fresh
    # 24bppRgb canvas to strip the alpha channel cleanly.
    $flat = New-Object System.Drawing.Bitmap $Bitmap.Width, $Bitmap.Height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($flat)
    $g.Clear($teal)
    $g.DrawImage($Bitmap, 0, 0)
    $g.Dispose()
    $flat.Save($Path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $flat.Dispose()
}

# ---- Wizard images -----------------------------------------------------
function New-WizardImage {
    param([System.Drawing.Bitmap]$Logo, [int]$Width, [int]$Height, [bool]$DrawWordmark)

    $bmp = New-Object System.Drawing.Bitmap $Width, $Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    # Vertical teal gradient — darker at the bottom so the wizard chrome
    # below it (white background of the page body) gets a soft visual
    # transition rather than a hard edge.
    $rect = New-Object System.Drawing.Rectangle 0, 0, $Width, $Height
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $teal, $tealDark, 90.0
    $g.FillRectangle($brush, $rect)
    $brush.Dispose()

    # Logo: scale to ~60% of width, vertically centred in the upper
    # two-thirds so the optional wordmark has room beneath it.
    $logoBox = [int]([Math]::Round($Width * 0.62))
    $logoX   = [int](($Width - $logoBox) / 2)
    if ($DrawWordmark) {
        $logoY = [int]([Math]::Round($Height * 0.18))
    } else {
        $logoY = [int](($Height - $logoBox) / 2)
    }
    $g.DrawImage($Logo, (New-Object System.Drawing.Rectangle $logoX, $logoY, $logoBox, $logoBox))

    if ($DrawWordmark) {
        $titleFont    = New-Object System.Drawing.Font 'Segoe UI', 22, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $subtitleFont = New-Object System.Drawing.Font 'Segoe UI', 11, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
        $titleBrush    = New-Object System.Drawing.SolidBrush $ink
        $subtitleBrush = New-Object System.Drawing.SolidBrush $inkSubtle
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment     = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center

        $titleY    = $logoY + $logoBox + 6
        $subtitleY = $titleY + 32
        $g.DrawString('TulparLang', $titleFont, $titleBrush, (New-Object System.Drawing.RectangleF 0, $titleY, $Width, 32), $sf)
        $g.DrawString('the python-fast script', $subtitleFont, $subtitleBrush, (New-Object System.Drawing.RectangleF 0, $subtitleY, $Width, 24), $sf)

        $titleFont.Dispose(); $subtitleFont.Dispose()
        $titleBrush.Dispose(); $subtitleBrush.Dispose()
        $sf.Dispose()
    }

    $g.Dispose()
    return $bmp
}

# ---- Driver ------------------------------------------------------------
$logo = [System.Drawing.Image]::FromFile($src)

Write-Host '-> tulpar.ico'
Write-Ico -OutPath (Join-Path $here 'tulpar.ico') -Sizes 16,24,32,48,64,128,256 -SourceImage $logo

# White recolour for the wizard panels (teal glyph on teal background is
# unreadable). The ICO above keeps the original brand colour.
$logoWhite = New-Object System.Drawing.Bitmap $logo
ConvertTo-WhiteGlyph -Bitmap $logoWhite

Write-Host '-> wizard-image.bmp (164x314)'
$wizardLarge = New-WizardImage -Logo $logoWhite -Width 164 -Height 314 -DrawWordmark $true
Save-Bmp24 -Bitmap $wizardLarge -Path (Join-Path $here 'wizard-image.bmp')
$wizardLarge.Dispose()

Write-Host '-> wizard-small.bmp (55x58)'
$wizardSmall = New-WizardImage -Logo $logoWhite -Width 55 -Height 58 -DrawWordmark $false
Save-Bmp24 -Bitmap $wizardSmall -Path (Join-Path $here 'wizard-small.bmp')
$wizardSmall.Dispose()

$logoWhite.Dispose()
$logo.Dispose()
Write-Host 'done.'
