# Embeds the Inter font subsets in res\fonts into a C++ source file
# (src\gui\font_data.cpp).
#
# Why the bytes are embedded rather than loaded from disk: the app ships as a
# single exe, so a font next to it is a font that goes missing. It is the same
# reason src\gui\logo_data.cpp carries the mark instead of reading a PNG.
#
# Why subsets: the full Inter TTFs are ~415 KB each and carry scripts this app
# never draws. res\fonts holds Latin + Turkish + punctuation only, which is 25 KB
# per weight. The subsetting is a deliberate, recorded step - regenerating it
# needs fontTools, but *building* does not, so the toolchain stays PowerShell.
#
# Run tools\make_font_subset.ps1 after replacing a .ttf; this tool then just
# embeds whatever is in res\fonts.
param(
  [string]$FontsDir = "res\fonts",
  [string]$OutPath  = "src\gui\font_data.cpp"
)

$ErrorActionPreference = 'Stop'

# Ağırlık -> (dosya, üretilen sembol). Sıra theme::Fonts alanlarıyla eşleşir.
$faces = [ordered]@{
    'Inter-Regular.subset.ttf'   = 'kFontRegular'
    'Inter-SemiBold.subset.ttf'  = 'kFontMedium'
    'Inter-Bold.subset.ttf'      = 'kFontBold'
}

# Satır başına 16 bayt: derleyicinin satır uzunluğu sınırına takılmaz, dosya
# okunabilir kalır.
function Format-ByteArray([byte[]]$bytes, [string]$indent) {
  $lines = New-Object System.Collections.Generic.List[string]
  for ($i = 0; $i -lt $bytes.Length; $i += 16) {
    $take = [Math]::Min(16, $bytes.Length - $i)
    $chunk = for ($j = 0; $j -lt $take; $j++) { '0x{0:x2}' -f $bytes[$i + $j] }
    $lines.Add($indent + ($chunk -join ', ') + ',')
  }
  return ($lines -join "`r`n")
}

$out = New-Object System.Collections.Generic.List[string]
$out.Add('#include "font_data.hpp"')
$out.Add('')
$out.Add('// tools\make_font_data.ps1 tarafından üretilir; elle düzenlenmemelidir.')
$out.Add('//')
$out.Add('// Inter (SIL Open Font License 1.1) - bkz res\fonts\Inter-OFL.txt')
$out.Add('// Yalnızca Latin + Turkce + noktalama alt kumesi gömülür.')
$out.Add('')
$out.Add('namespace fontdata')
$out.Add('{')
$out.Add('    // Uygulamanin cizebilecegi tum kod noktalari; atlas bu esikten kucukse')
$out.Add('    // karakter yok sayilir ve kutu olarak cizilir.')
$out.Add('    // Turkce icin gerekenler Latin-1 (U+00A7 section, U+00B1 plus-minus ...)')
$out.Add('    // ve Latin Extended-A (U+011E G breve, U+0130 I noktali, U+015F s cedil)')
$out.Add('    // araliginda; her ikisi de asagida tamamen.')
$out.Add('    const ImWchar kRanges[] =')
$out.Add('    {')
$out.Add('        0x0020, 0x007E,   // Basic Latin')
$out.Add('        0x00A0, 0x00FF,   // Latin-1 Supplement')
$out.Add('        0x0100, 0x017F,   // Latin Extended-A (Turkce)')
$out.Add('        0x2000, 0x206F,   // General Punctuation (tire, tirnak, orta nokta)')
$out.Add('        0x2013, 0x2014,   // en/em dash')
$out.Add('        0,')
$out.Add('    };')
$out.Add('')

$total = 0
foreach ($file in $faces.Keys) {
  $path = Join-Path (Get-Location) (Join-Path $FontsDir $file)
  if (-not (Test-Path $path)) { throw "font bulunamadi: $path" }

  $bytes = [System.IO.File]::ReadAllBytes($path)
  $total += $bytes.Length
  $sym = $faces[$file]

  $out.Add("    // $file - $('{0:N0}' -f $bytes.Length) bayt")
  $out.Add("    const unsigned int ${sym}Size = $($bytes.Length);")
  $out.Add("    const unsigned char ${sym}[] =")
  $out.Add('    {')
  $out.Add((Format-ByteArray $bytes '        '))
  $out.Add('    };')
  $out.Add('')
}

$out.Add('}')

$outDir = Split-Path -Parent $OutPath
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText((Join-Path (Get-Location) $OutPath), ($out -join "`r`n"), $utf8)

Write-Output ("{0}: {1} yuz, {2:N0} bayt gomuldu" -f $OutPath, $faces.Count, $total)