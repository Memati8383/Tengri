param([string]$Order = 'fwd', [string]$Tag = '', [int]$Passes = 1)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dir  = 'C:\Users\Public\tgengine'
New-Item -ItemType Directory -Force -Path $dir | Out-Null

# Tek UAC istemi: yonetici degilsek kendimizi bir kez yukseltip ayni scripti
# tekrar calistiriyoruz (kosu basina ayri onay penceresi cikmasin).
$id  = [Security.Principal.WindowsIdentity]::GetCurrent()
$pr  = New-Object Security.Principal.WindowsPrincipal $id
if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath,
        '-Order', $Order, '-Tag', $Tag, '-Passes', $Passes) -Wait
    exit
}

$exe = Join-Path $root 'build\probe_mft_engine.exe'
if (-not (Test-Path $exe)) { throw "probe yok: $exe" }

# Olcum matrisi: tek is parcacigi tabani + 2/4/8 dilim.
$configs = @( '1', '2', '4', '8' )

# SIRA CONTROL: 'rev' ayarli kosuyu tersten calistirir. Surucu/onbellege alma
# ilerledikce hizlanmis olabilir; tersten kosuda taban yine ~80 sn ise kazanc
# gercekten eszamanliliktandir.
if ($Order -eq 'rev') { [array]::Reverse($configs) }

$sum = Join-Path $dir ("matrix" + $(if ($Tag) { "_$Tag" } else { '' }) + ".txt")
Remove-Item -Force -ErrorAction SilentlyContinue $sum

for ($pass = 1; $pass -le $Passes; $pass++) {
    $sfx = if ($Passes -gt 1) { "_p$pass" } else { '' }
    foreach ($t in $configs) {
        $cfg = "t${t}$sfx"
        $out = Join-Path $dir ("engine_" + $cfg + $Tag + ".txt")
        Remove-Item -Force -ErrorAction SilentlyContinue $out

        $started = Get-Date
        $p = Start-Process -FilePath $exe -ArgumentList @($out, $t) -Wait -PassThru
        $secs = [int]((Get-Date) - $started).TotalSeconds

        $txt = ''
        if (Test-Path $out) { $txt = Get-Content $out -Raw }
        $sure  = ([regex]::Match($txt, 'sure=(\d+) ms')).Groups[1].Value
        $dugum = ([regex]::Match($txt, 'dugum=(\d+) dosya=(\d+) klasor=(\d+)')).Groups
        $orph  = ([regex]::Match($txt, 'orphan[^0-9]*(\d+)')).Groups[1].Value
        $line  = "{0,-8} exit={1} bekle={2}s sure={3} dugum={4} dosya={5} klasor={6} orphan={7}" -f `
                 $cfg, $p.ExitCode, $secs, $sure, $dugum[1].Value, $dugum[2].Value, $dugum[3].Value, $orph
        Write-Output $line
        Add-Content -Path $sum -Value $line
    }
}
Write-Output "ozet: $sum"
