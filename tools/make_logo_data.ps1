# Embeds res\tengri-logo.png into a C++ source file (src\gui\logo_data.cpp).
#
# Why the bytes are embedded instead of carried as an RT_RCDATA resource: the
# resource is demonstrably present in the built exe, yet FindResourceW from inside
# the running process does not resolve it (ERROR_RESOURCE_TYPE_NOT_FOUND, 1813).
# Embedding removes that whole failure class - there is no lookup that can miss and
# no second file to keep in sync with the exe. It also mirrors how
# src\gui\brand_icons.cpp is already produced by tools\make_brand_icons.ps1.
#
# -WhiteArtwork converts the artwork to white-on-transparent. The app draws a
# monochrome dark interface, so a white mark with no plate behind it is what
# belongs there; the black rounded square in the source PNG is a social/avatar
# crop, not something to paste onto a dark card.
#
# The artwork is downscaled to MaxSize first. The mark is only ever drawn at
# 24-44 logical px, so embedding the full 500 px source would bloat the array for
# pixels nothing displays.
param(
  [string]$SourcePath = "res\tengri-logo.png",
  [string]$OutPath    = "src\gui\logo_data.cpp",
  [int]$MaxSize       = 256,
  [switch]$WhiteArtwork
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$srcPath = (Resolve-Path $SourcePath).Path

# --- kaynak resmi hazırla ----------------------------------------------------
$src = [System.Drawing.Image]::FromFile($srcPath)
try {
  $scale = [Math]::Min(1.0, [double]$MaxSize / [Math]::Max($src.Width, $src.Height))

  if ($scale -lt 1.0) {
    $outW = [int][Math]::Round($src.Width  * $scale)
    $outH = [int][Math]::Round($src.Height * $scale)
  } else {
    $outW = $src.Width; $outH = $src.Height
  }

  $bmp = New-Object System.Drawing.Bitmap $outW, $outH, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.Clear([System.Drawing.Color]::Transparent)
  $g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $outW, $outH))
  $g.Dispose()
} finally {
  $src.Dispose()
}

# --- beyaz çizim kipine çevir -------------------------------------------------
if ($WhiteArtwork) {
  # Alplamayı parlaklıktan türet: beyaz çizgi opak olur, siyah tabla şeffaflaşır.
  # SetPixel her piksel için COM çağrısı yaptığından LockBits kullanılır; 256x256
  # için bile fark ~iki saniye.
  $rect = New-Object System.Drawing.Rectangle 0, 0, $outW, $outH
  $lock = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  try {
    $n    = $outW * $outH
    $data = New-Object byte[] ($lock.Stride * $outH)
    [System.Runtime.InteropServices.Marshal]::Copy($lock.Scan0, $data, 0, $data.Length)

    for ($i = 0; $i -lt $n; $i++) {
      $o = $i * 4
      $b = $data[$o]; $gg = $data[$o + 1]; $r = $data[$o + 2]; $a = $data[$o + 3]
      # BGRA bellekte. Kaynakta yarı saydam pikseller önce RGB'siz sayılabilir;
      # alplamayı kompozit etmeden türetmek ince kenarlarda hâle bırakırdı.
      if ($a -eq 0) { $data[$o] = 255; $data[$o+1] = 255; $data[$o+2] = 255; continue }
      $lum = ($r * 299 + $gg * 587 + $b * 114) / 1000
      $na  = [int][Math]::Round(($a * $lum) / 255.0)
      $data[$o] = 255; $data[$o + 1] = 255; $data[$o + 2] = 255; $data[$o + 3] = [byte]$na
    }

    [System.Runtime.InteropServices.Marshal]::Copy($data, 0, $lock.Scan0, $data.Length)
  } finally {
    $bmp.UnlockBits($lock)
  }
}

# --- PNG olarak kaydet -------------------------------------------------------
$ms = New-Object System.IO.MemoryStream
$bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
$bytes = $ms.ToArray()
$ms.Dispose(); $bmp.Dispose()

# --- diziye çevir ------------------------------------------------------------
# Satır başına 16 bayt: derleyicinin satır uzunluğu sınırına takılmaz, dosya
# okunabilir kalır.
$lines = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $bytes.Length; $i += 16) {
  $take = [Math]::Min(16, $bytes.Length - $i)
  $chunk = for ($j = 0; $j -lt $take; $j++) { '0x{0:x2}' -f $bytes[$i + $j] }
  $lines.Add('    ' + ($chunk -join ', ') + ',')
}

$mode = if ($WhiteArtwork) { 'beyaz çizim, saydam zemin' } else { 'kaynak görünümü' }

$out = @"
#include "logo_data.hpp"

// tools\make_logo_data.ps1 tarafından üretilir; elle düzenlenmemelidir.
// Kaynak: $SourcePath - $outW x $outH - $mode - $($bytes.Length) bayt PNG

namespace logodata
{
    const unsigned int kLogoPngSize = $($bytes.Length);
    const unsigned int kLogoWidth  = $outW;
    const unsigned int kLogoHeight = $outH;

    const unsigned char kLogoPng[] =
    {
$($lines -join "`r`n")
    };
}
"@

$outDir = Split-Path -Parent $OutPath
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
$utf8 = New-Object System.Text.UTF8Encoding $false
# Join-Path mutlak bir yolun başına Get-Location'u ekleyemez ("Verilen yolun
# biçimi desteklenmiyor" hatası verir). make_tweak_table.ps1'deki gibi kök
# yolun kendisi geçerliyse olduğu gibi kullanılır.
$outPath = if ([System.IO.Path]::IsPathRooted($OutPath)) { $OutPath } else { Join-Path (Get-Location) $OutPath }
[System.IO.File]::WriteAllText($outPath, $out, $utf8)

Write-Output ("{0}: {1}x{2}, {3:N0} bayt PNG ({4})" -f $OutPath, $outW, $outH, $bytes.Length, $mode)