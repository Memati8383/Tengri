# Normalises every literal font size in the ui::Text / TextSize / Label family
# onto the type scale declared in src\gui\theme.hpp.
#
# One-time migration helper. It is deliberately narrow: it only rewrites a
# numeric literal that is the *size* argument of one of those helpers, matched
# before any parenthesis is reached, so px() paddings, Gray() values and ImGui's
# own TextUnformatted calls are left alone.
param(
  [string[]]$Files = @("src\app.cpp", "src\gui\widgets.cpp")
)

$ErrorActionPreference = 'Stop'

# Kaynak punto -> theme::size:: jetonu. Karar: yarım punto adımlarini en yakin
# role yuvarla; hicbir metin rolu kaybolmaz, sadece tek bir deger olur.
$map = @{
    '8.5'  = 'Micro'
    '10.0' = 'Meta';    '10.5' = 'Meta';    '11.0' = 'Meta';   '11.5' = 'Meta'
    '12.0' = 'Caption'
    '12.5' = 'Body';    '13.0' = 'Body';    '13.5' = 'Body'
    '14.0' = 'Label';   '14.5' = 'Label'
    '15.0' = 'Title';   '16.0' = 'Title'
    '18.0' = 'Heading'
    '22.0' = 'PageTitle'; '24.0' = 'PageTitle'
    '26.0' = 'Display';  '28.0' = 'Display'
}

# TextCentered onceye yazilir; Text'ten once gelmesi lazim.
# Sondaki virgul ayrica yakalanir: atlanirsa "size::Body ImVec2(...)" olur ve
# derleme "syntax error" verir - sessizce degil, en azindan gurultulu.
$rx = [regex]'(TextCentered|TextSpaced|TextSize|SpacedSize|Label|Text)\(([^;()]*?,)\s*(\d+(?:\.\d+)?)f(\s*,)'

$total = 0
foreach ($f in $Files) {
    $path = (Resolve-Path $f).Path
    $text = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
    $count = 0

    $out = $rx.Replace($text, {
        param($m)
        $key = $m.Groups[3].Value
        if (-not $map.ContainsKey($key)) { return $m.Value }
        $script:count++
        return $m.Groups[1].Value + '(' + $m.Groups[2].Value + 'theme::size::' + $map[$key] + $m.Groups[4].Value
    })

    if ($count -gt 0) {
        $utf8 = New-Object System.Text.UTF8Encoding $false
        [System.IO.File]::WriteAllText($path, $out, $utf8)
        Write-Output ("{0}: {1} nokta -> olcek jetonuna baglandi" -f $f, $count)
        $total += $count
    } else {
        Write-Output ("{0}: degisiklik yok" -f $f)
    }
}

Write-Output ("toplam: {0}" -f $total)