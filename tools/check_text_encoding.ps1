# Metin kodlama kapisi: depodaki her metin dosyasi gecerli UTF-8 olmali ve
# "iki kez kodlanmis UTF-8" tasiymamali.
#
# Bu bozulma gercek bir hataydi: BOM'suz UTF-8 bir .ps1'i Windows PowerShell 5.1
# ANSI (CP1254) okur; betigin Turkce satir sonu yazilari bozuk dogar ve urettigi
# font_data.cpp / logo_data.cpp yorumlari bozuk hâliyle depoya girer. .ps1
# dosyalari bu yuzden BOM ister.
#
# Kullanim: powershell -NoProfile -File tools\check_text_encoding.ps1
# Cikis:    0 = temiz, 1 = en az bir dosya kusurlu.
#
# Denetlenen isaretler kod noktasi olarak yazildi: bozuk karakterleri bu dosyaya
# koymak kapinin kendini de kizil bayraga cevirmesi olurdu, ayrica betigin kendi
# kodlamasi da ayni denetimden geciyor.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$textExt = @('.cpp', '.hpp', '.h', '.rc', '.md', '.ps1', '.bat', '.txt', '.yml')

$exclude = @(
    (Join-Path $root 'third_party'),
    (Join-Path $root 'build'),
    (Join-Path $root 'build_cmake'),
    (Join-Path $root '.git')
)

function Test-Excluded {
    param($path)
    foreach ($e in $exclude) {
        if ($path -eq $e -or $path.StartsWith($e + [IO.Path]::DirectorySeparatorChar)) { return $true }
    }
    return $false
}

# Iki kez kodlanmis UTF-8'in imza karakterleri: bir UTF-8 bayt cifti Latin-1 ya
# da CP1252 gecercesine okunup yeniden UTF-8'e cevrilirse bunlar uretir. Duz
# Turkce metinde gecmezler.
$marks = [char[]]@([char]0x00C3, [char]0x00C2, [char]0x00C5, [char]0x0178)
$pair  = [string][char]0x00C4 + [string][char]0x00B1

$bad = @()
$scanned = 0

foreach ($file in (Get-ChildItem -LiteralPath $root -Recurse -File)) {
    $ext = $file.Extension.ToLower()
    if ($textExt -notcontains $ext) { continue }
    if (Test-Excluded $file.FullName) { continue }
    $scanned++

    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)

    if ($ext -eq '.ps1' -and -not $hasBom) {
        $bad += ($file.FullName + " : .ps1 BOM'suz (PS 5.1 bunu ANSI okur)")
        continue
    }

    $body = $bytes
    if ($hasBom) { $body = $bytes[3..($bytes.Length - 1)] }

    $strict = New-Object System.Text.UTF8Encoding($false, $true)
    try { $text = $strict.GetString($body) }
    catch {
        $bad += ($file.FullName + ' : gecerli UTF-8 degil')
        continue
    }

    if ($text.IndexOfAny($marks) -ge 0 -or $text.Contains($pair)) {
        $bad += ($file.FullName + ' : iki kez kodlanmis UTF-8 izi')
    }
}

Write-Host ("kodlama kontrolu: {0} metin dosyasi tarandi" -f $scanned)
if ($bad.Count -gt 0) {
    Write-Host "kusurlu dosyalar:" -ForegroundColor Red
    $bad | ForEach-Object { Write-Host ("  " + $_) }
    exit 1
}
Write-Host "temiz: her metin dosyasi UTF-8, iki kez kodlanmis karakter yok."
exit 0
