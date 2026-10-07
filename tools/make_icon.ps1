# Generates res\tengri.ico from the same diamond mark the app draws at runtime
# (DrawLogo): an outer rotated square with a filled inner diamond.
#
# Entries are written as classic 32bpp BMP/DIB with an AND mask rather than as PNG.
# The shell reads PNG entries on Vista+, but GDI+ (and anything layered on it) rejects
# them, so the DIB form is the one that decodes everywhere.
param(
  [string]$OutPath = "res\tengri.ico",
  [int[]]$Sizes = @(16,20,24,32,40,48,64,128,256)
)

Add-Type -AssemblyName System.Drawing

function New-Mark([int]$S) {
  $bmp = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)

  # Just under half the canvas, so the shape keeps a transparent margin the way the
  # runtime logo insets its diamond inside the sidebar.
  $c = $S / 2.0
  $r = $S * 0.34

  $outer = @(
    [System.Drawing.PointF]::new($c,     $c - $r),
    [System.Drawing.PointF]::new($c + $r, $c),
    [System.Drawing.PointF]::new($c,     $c + $r),
    [System.Drawing.PointF]::new($c - $r, $c)
  )
  $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([Math]::Max(1.0, $S * 0.055))
  $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
  $g.DrawPolygon($pen, $outer)

  # Filled inner diamond, matching the 0.40 ratio the runtime logo uses.
  $ri = $r * 0.40
  $inner = @(
    [System.Drawing.PointF]::new($c,     $c - $ri),
    [System.Drawing.PointF]::new($c + $ri, $c),
    [System.Drawing.PointF]::new($c,     $c + $ri),
    [System.Drawing.PointF]::new($c - $ri, $c)
  )
  $g.FillPolygon([System.Drawing.SolidBrush]::new([System.Drawing.Color]::White), $inner)

  $pen.Dispose(); $g.Dispose()
  return $bmp
}

# Leading comma keeps this a byte[]; without it PowerShell unrolls the array into
# Object[] and List[byte].AddRange() rejects it.
function New-U32($v) { return ,([byte[]]@(($v -band 0xFF), (($v -shr 8) -band 0xFF), (($v -shr 16) -band 0xFF), (($v -shr 24) -band 0xFF))) }
function New-U16($v) { return ,([byte[]]@(($v -band 0xFF), (($v -shr 8) -band 0xFF))) }

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
  # biPlanes ve biBitCount WORD'dur, DWORD degil. DWORD yazmak basliga dort bayt
  # fazlalik katar ve sonraki alanlar kayar; sonuc biBitCount=0,
  # biCompression=32 olur ve Windows girdiyi reddeder.
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

Write-Output ("{0}: {1} sizes ({2}), {3:N0} bytes" -f $OutPath,
    (($entries | ForEach-Object { $_.Size }) -join '/'), $entries.Count, $bytes.Count)
