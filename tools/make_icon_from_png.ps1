# Generates res\tengri.ico from an existing PNG artwork (res\tengri-logo.png)
# instead of the procedural diamond mark make_icon.ps1 draws.
#
# The entry encoding is identical to make_icon.ps1 on purpose: classic 32bpp
# BMP/DIB with an AND mask, not PNG entries. The shell reads PNG entries on
# Vista+, but GDI+ (and anything layered on it) rejects them, so the DIB form is
# the one that decodes everywhere.
#
# The source art is alpha-composited onto an opaque rounded square of the given
# backdrop colour, because an icon with a transparent interior turns into a black
# hole at 16px in the taskbar and the shell tray.
param(
  [string]$SourcePath = "res\tengri-logo.png",
  [string]$OutPath    = "res\tengri.ico",
  [int[]]$Sizes       = @(16,20,24,32,40,48,64,128,256),
  [string]$Backdrop   = "#101014"     # matches the app's clear colour
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = (Resolve-Path $SourcePath).Path

function New-U32($v) { return ,([byte[]]@(($v -band 0xFF), (($v -shr 8) -band 0xFF), (($v -shr 16) -band 0xFF), (($v -shr 24) -band 0xFF))) }
function New-U16($v) { return ,([byte[]]@(($v -band 0xFF), (($v -shr 8) -band 0xFF))) }

# Downscales the artwork into one icon size on the opaque backdrop.
function New-Mark([int]$S) {
  $bmp = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

  $bg = [System.Drawing.ColorTranslator]::FromHtml($Backdrop)
  $g.Clear($bg)

  # Inset leaves room for the rounded corners so the artwork is never clipped by
  # them at the largest sizes.
  $pad = [Math]::Max(1, [int]($S * 0.06))
  $box = New-Object System.Drawing.Rectangle $pad, $pad, ($S - 2 * $pad), ($S - 2 * $pad)

  $clip = New-Object System.Drawing.Drawing2D.GraphicsPath
  $r = $S * 0.22
  $clip.AddArc($box.X, $box.Y, $r, $r, 180, 90)
  $clip.AddArc($box.Right - $r, $box.Y, $r, $r, 270, 90)
  $clip.AddArc($box.Right - $r, $box.Bottom - $r, $r, $r, 0, 90)
  $clip.AddArc($box.X, $box.Bottom - $r, $r, $r, 90, 90)
  $clip.CloseFigure()
  $g.SetClip($clip)

  $art = [System.Drawing.Image]::FromFile($src)
  $g.DrawImage($art, $box)
  $art.Dispose()

  $clip.Dispose(); $g.Dispose()
  return $bmp
}

# Builds one DIB entry: BITMAPINFOHEADER + bottom-up BGRA XOR + bottom-up 1bpp AND mask.
function Get-Dib([int]$S) {
  $bmp = New-Mark $S
  $rect   = New-Object System.Drawing.Rectangle 0,0,$S,$S
  $data   = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $data.Stride
  $scan  = New-Object byte[] ($stride * $S)
  [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $scan, 0, $scan.Length)
  $bmp.UnlockBits($data)
  $bmp.Dispose()

  # The backdrop is opaque, so alpha is forced to 255 for every pixel; leaving the
  # source alpha here would punch the rounded corners back through.
  for ($i = 3; $i -lt $scan.Length; $i += 4) { $scan[$i] = 255 }

  # XOR: DIB stores rows bottom-up, the bitmap hands them top-down.
  $rowBytes = $S * 4
  $xor = New-Object byte[] ($rowBytes * $S)
  for ($y = 0; $y -lt $S; $y++) {
    [Array]::Copy($scan, $y * $stride, $xor, ($S - 1 - $y) * $rowBytes, $rowBytes)
  }

  # AND mask: 1bpp, rows padded to 4 bytes, bottom-up. The 32bpp alpha already carries
  # transparency, so this is all zeros; it is present because the format requires it.
  $maskStride = [int](([Math]::Floor(($S + 31) / 32)) * 4)
  $and = New-Object byte[] ($maskStride * $S)

  $header = New-U32 40          # biSize
  $header = $header + (New-U32 $S)   # biWidth
  $header = $header + (New-U32 ($S * 2))  # biHeight = XOR + AND
  # biPlanes ve biBitCount WORD'dur, DWORD degil. DWORD yazildiginda basliga dort
  # bayt fazlalik girer, sonraki tum alanlar dort bayt kayar ve baslikta
  # biBitCount=0, biCompression=32 (BI_BITFIELDS) okunur. Dosya ayri bir hata
  # vermez, pikseller yerinde durur; ama Windows bu girdiyi reddedip simgeyi
  # reddeder ve kabuk JENERIK bir simge gosterir. Gorev cubugu ve dosya
  # gezginindeki belirti buydu.
  $header = $header + (New-U16 1)   # biPlanes   (WORD)
  $header = $header + (New-U16 32)  # biBitCount (WORD)
  $header = $header + (New-U32 0)   # biCompression = BI_RGB
  $header = $header + (New-U32 ($xor.Length + $and.Length))  # biSizeImage
  $header = $header + (New-U32 0)   # biXPelsPerMeter
  $header = $header + (New-U32 0)   # biYPelsPerMeter
  $header = $header + (New-U32 0)   # biClrUsed
  $header = $header + (New-U32 0)   # biClrImportant

  $out = New-Object byte[] ($header.Length + $xor.Length + $and.Length)
  [Array]::Copy($header, 0, $out, 0, $header.Length)
  [Array]::Copy($xor, 0, $out, $header.Length, $xor.Length)
  [Array]::Copy($and, 0, $out, $header.Length + $xor.Length, $and.Length)
  return ,$out
}

$entries = foreach ($s in $Sizes) {
  [pscustomobject]@{ Size = $s; Data = (Get-Dib $s) }
}

# ICONDIR + one ICONDIRENTRY per size + payload.
# ICONDIR is three WORDs (reserved, type, count) = 6 bytes. Writing them as DWORDs
# shifts every entry offset by six bytes and the file stops being a valid icon.
$offset    = 6 + 16 * $entries.Count
$directory = New-Object System.Collections.Generic.List[byte]
$directory.AddRange((New-U16 0))                  # reserved
$directory.AddRange((New-U16 1))                  # type = icon
$directory.AddRange((New-U16 $entries.Count))     # image count

foreach ($e in $entries) {
  $d = if ($e.Size -ge 256) { 0 } else { $e.Size }   # 0 encodes 256
  $directory.Add($d); $directory.Add($d)             # width, height
  $directory.Add(0)                                  # colour count
  $directory.Add(0)                                  # reserved
  $directory.AddRange((New-U16 0))                   # planes
  $directory.AddRange((New-U16 32))                  # bits per pixel
  $directory.AddRange((New-U32 $e.Data.Length))
  $directory.AddRange((New-U32 $offset))
  $offset += $e.Data.Length
}

$bytes = New-Object System.Collections.Generic.List[byte]
$bytes.AddRange($directory.ToArray())
foreach ($e in $entries) { $bytes.AddRange($e.Data) }

$outDir = Split-Path -Parent $OutPath
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
[System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $OutPath), $bytes.ToArray())

Write-Output ("{0}: {1} sizes ({2}) from {3}, {4:N0} bytes" -f $OutPath,
    (($entries | ForEach-Object { $_.Size }) -join '/'), $entries.Count, (Split-Path -Leaf $src), $bytes.Count)