# Decodes one entry out of a classic .ico (DIB form) and writes it as a PNG, so a
# generated icon can be eyeballed without opening Explorer.
#
# .NET's Icon ctor rejects multi-size 32bpp DIB icons on this platform even when
# they are perfectly valid, so the entries are unpacked by hand instead.
param(
  [Parameter(Mandatory = $true)][string]$IconPath,
  [int]$Size = 32,
  [string]$OutPath = "$env:TEMP\icon_entry.png"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $IconPath).Path)

$reserved    = [BitConverter]::ToUInt16($bytes, 0)
$type        = [BitConverter]::ToUInt16($bytes, 2)
$count       = [BitConverter]::ToUInt16($bytes, 4)
if ($reserved -ne 0 -or $type -ne 1) { throw "not an icon file (reserved=$reserved type=$type)" }

$found = $false
for ($i = 0; $i -lt $count; $i++) {
  $e  = 6 + 16 * $i
  $w  = [int]$bytes[$e]; if ($w -eq 0) { $w = 256 }
  $h  = [int]$bytes[$e + 1]; if ($h -eq 0) { $h = 256 }
  if ($w -ne $Size) { continue }
  $found = $true

  $len   = [BitConverter]::ToUInt32($bytes, $e + 8)
  $off   = [BitConverter]::ToUInt32($bytes, $e + 12)
  $bpp   = [BitConverter]::ToUInt16($bytes, $e + 6)
  $hdr   = [BitConverter]::ToUInt32($bytes, $off)
  if ($hdr -ne 40) { throw "entry ${Size}px is not a BITMAPINFOHEADER DIB" }
  if ($bpp -ne 32)  { throw "entry ${Size}px is ${bpp}bpp, expected 32" }

  # XOR pixels are stored bottom-up; flip into a top-down RGBA byte array first,
  # then set the pixel format to the BGRA order LockBits hands back.
  $stride = $w * 4
  $top    = New-Object byte[] ($stride * $h)
  for ($y = 0; $y -lt $h; $y++) {
    [Array]::Copy($bytes, $off + 40 + ($h - 1 - $y) * $stride, $top, $y * $stride, $stride)
  }

  $bmp  = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $lock = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $w, $h),
                        [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  [System.Runtime.InteropServices.Marshal]::Copy($top, 0, $lock.Scan0, $top.Length)
  $bmp.UnlockBits($lock)

  $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
  Write-Output ("{0}: {1}px entry decoded ({2:N0} bytes) -> {3}" -f (Split-Path -Leaf $IconPath), $w, $len, $OutPath)
  break
}

if (-not $found) { throw "no ${Size}px entry in the icon" }