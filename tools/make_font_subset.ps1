# Regenerates the Inter subsets in res\fonts from the upstream release.
#
# Run this ONCE, when upgrading Inter or adding a language. It is deliberately not
# part of build.bat: it needs Python + fontTools, while building needs only
# PowerShell. tools\make_font_data.ps1 then embeds whatever subsets exist.
#
#   python -m pip install fonttools
#   .\tools\make_font_subset.ps1 -Archive <path-to-Inter-*.zip>
#
# Why subsets exist at all: the upstream TTFs are ~415 KB each and carry scripts
# this app never draws. Latin + Turkish + punctuation is 25 KB per weight, so the
# three faces cost 75 KB in the exe instead of 1.2 MB.
#
# Why those ranges: they are derived from the characters actually present in
# src\**\*.cpp and *.hpp - Basic Latin plus the Turkish letters in Latin Extended-A
# (U+011E G breve, U+0130 I with dot, U+015F s with cedilla) plus General
# Punctuation for dashes, quotes and the middot. Widen the ranges only if you add a
# language; a subset outside the used set is wasted bytes, a subset missing a
# character renders a box.
param(
  [string]$Archive = "$env:TEMP\inter_dl\inter.zip",
  [string]$WorkDir = "$env:TEMP\inter_dl\x",
  [string]$OutDir  = "res\fonts",
  [string]$Weights = "Regular,SemiBold,Bold"
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Archive)) { throw "arsiv bulunamadi: $Archive" }

if (-not (Test-Path "$WorkDir\extras\ttf")) {
  Write-Host "[*] Arsiv aciliyor: $Archive"
  if (Test-Path $WorkDir) { Remove-Item $WorkDir -Recurse -Force }
  Expand-Archive $Archive -DestinationPath $WorkDir -Force
}
$src = "$WorkDir\extras\ttf"

# Ayni kume degisiklik yapmaz; pyftsubset deterministik.
$ranges = "U+0020-007E,U+00A0-00FF,U+0100-017F,U+2000-206F,U+2013-2014"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

foreach ($w in $Weights.Split(',')) {
  $in  = Join-Path $src "Inter-$w.ttf"
  $out = Join-Path $OutDir "Inter-$w.subset.ttf"
  if (-not (Test-Path $in)) { throw "kaynak font yok: $in" }

  # --no-hinting: stb_truetype ipuclari kullanmiyor, tablo gereksiz yer tutuyor.
  # --layout-features='': stb_truetype GPOS uygulamiyor; Inter'in kerning'i orada,
  #   tasimak yer bosluk olurdu.
  & python -m fontTools.subset $in --unicodes=$ranges --no-hinting `
            --layout-features='' --drop-tables+=DSIG --output-file=$out
  if ($LASTEXITCODE -ne 0) { throw "alt kumeleme basarisiz: $w" }

  Write-Host ("[+] Inter-{0,-9} {1,8:N0} bayt" -f $w, (Get-Item $out).Length)
}

# Lisansi da tazele: OFL metni Inter surumuyle birlikte gelir.
$license = Get-ChildItem $WorkDir -Recurse -Filter 'LICENSE.txt' -ErrorAction SilentlyContinue |
           Select-Object -First 1
if ($license) {
  Copy-Item $license.FullName (Join-Path $OutDir 'Inter-OFL.txt') -Force
  Write-Host "[+] lisans kopyalandi"
}

Write-Host "[*] Simdi .\tools\make_font_data.ps1 calistirip yeniden derle."