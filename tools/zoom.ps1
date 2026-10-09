param([string]$InPath, [string]$OutName, [int]$X, [int]$Y, [int]$W, [int]$H, [int]$Zoom = 8)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $InPath).Path)
$crop = $src.Clone((New-Object System.Drawing.Rectangle $X, $Y, $W, $H), $src.PixelFormat)
$dst = New-Object System.Drawing.Bitmap ($W * $Zoom), ($H * $Zoom)
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($crop, 0, 0, ($W * $Zoom), ($H * $Zoom))
$g.Dispose()
$dst.Save((Join-Path $env:TEMP $OutName), [System.Drawing.Imaging.ImageFormat]::Png)
$dst.Dispose(); $crop.Dispose(); $src.Dispose()
Write-Output ("wrote {0} ({1}x{2} from {3},{4} {5}x{6} @{7}x)" -f $OutName, ($W*$Zoom), ($H*$Zoom), $X, $Y, $W, $H, $Zoom)