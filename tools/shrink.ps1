param([string]$InPath, [string]$OutName, [int]$Width = 1200)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $InPath).Path)
$newH = [int]($src.Height * ($Width / [double]$src.Width))
$dst = New-Object System.Drawing.Bitmap $Width, $newH
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($src, 0, 0, $Width, $newH)
$g.Dispose()
$dst.Save((Join-Path $env:TEMP $OutName), [System.Drawing.Imaging.ImageFormat]::Png)
$dst.Dispose()
$src.Dispose()
Write-Output ("wrote {0} ({1}x{2})" -f $OutName, $Width, $newH)