# Generates docs/tweaks-registry.md -- the table of every registry location the 56
# tweaks touch.
#
# Why generated instead of hand-written: the README claims "every registry key this
# writes is visible in the source". A hand-maintained table would violate that claim
# the first time someone edits tweaks.cpp, which is exactly the failure mode the claim
# is meant to rule out.
#
# Two sources, because the 56 tweaks are stored two different ways:
#   categories 0-3 (32 tweaks) -> switch cases in tweaks.cpp, RegGet/RegPut/RegPutStr calls
#   categories 4-6 (24 tweaks) -> raw .reg bodies in regpack.cpp, [HKEY...] headers
#
# Usage:  powershell -File tools\make_tweak_table.ps1
# Output: docs/tweaks-registry.md

param(
    [string]$Tweaks  = 'src\core\tweaks.cpp',
    [string]$Regpack = 'src\core\regpack.cpp',
    [string]$Lang    = 'src\core\lang.cpp',
    [string]$Out     = 'docs\tweaks-registry.md',
    [string]$Header  = 'build\obj\tweak_keys.h'
)

$ErrorActionPreference = 'Stop'

$tweakText = [System.IO.File]::ReadAllText((Resolve-Path $Tweaks).Path)
$regText   = [System.IO.File]::ReadAllText((Resolve-Path $Regpack).Path)
$langText  = if (Test-Path $Lang) { [System.IO.File]::ReadAllText((Resolve-Path $Lang).Path) } else { '' }

# C++ kaynaginda Turkce harfler \xC3\xBC gibi byte kacislariyla saklaniyor. Bunlar tek
# bir bayt degil, UTF-8'de birden çok bayt: "\xC3\xBC" = u. \xC4\xB0 = I. Her kacis tek
# bayta indirgenip kalan baytlar biriktirilerek UTF-8 olarak cozulur.
# PowerShell betikleri fonksiyonu tanimdan ONCE cagirirsa CommandNotFound verir,
# bu yuzden tanim burada, ilk kullanimdan once duruyor.
function Resolve-Escapes([string]$s) {
    if ($s -notmatch '\\[xu]') { return $s }
    $bytes = New-Object System.Collections.Generic.List[byte]

    # Kaynaga iki tur kacis karismis olarak yaziliyor:
    #   \xC4\xB1  ->  on ekli bayt kacisi (UTF-8'deki "i")
    #   \u011F    ->  on ekli evrensel karakter kacisi, dort haneli, dogrudan kod noktasi
    #
    # Kaynakta bir zamanlar \uC4\x9F gibi IKIHANELI \u kacislari da vardi; bunlar teknik
    # olarak gecersizdir (\u dort hane ister) ve \x olarak duzeltildi. Iki haneli bicim
    # bilerek desteklenmez: yeniden gelirse uretilen tablo sessizce bozulmaktansa hata
    # versin diye \u yalnizca dort haneli kabul edilir.
    $esc = [regex]::Matches($s, '\\x([0-9A-Fa-f]{2})|\\u([0-9A-Fa-f]{4})')
    $pos = 0
    foreach ($e in $esc) {
        # Kacislar arasindaki duz metin her zaman ASCII'dir (Turkce harfler zaten kacis
        # olarak yaziliyor); Latin-1 bunu tek bayta cevirir ve 255 ustu karakterde
        # [byte] cast'inin patlamasini onler.
        $plain = $s.Substring($pos, $e.Index - $pos)
        $bytes.AddRange([System.Text.Encoding]::GetEncoding(1252).GetBytes($plain))

        if ($e.Groups[1].Success) {
            $bytes.Add([Convert]::ToByte($e.Groups[1].Value, 16))
        }
        else {
            # Dort haneli \u -- bir kod noktasi; UTF-8'e cevirip ekle.
            $cp = [Convert]::ToInt32($e.Groups[2].Value, 16)
            $bytes.AddRange([System.Text.Encoding]::UTF8.GetBytes([string][char]$cp))
        }
        $pos = $e.Index + $e.Length
    }
    $tail = $s.Substring($pos)
    $bytes.AddRange([System.Text.Encoding]::GetEncoding(1252).GetBytes($tail))

    $out = [System.Text.Encoding]::UTF8.GetString($bytes.ToArray())

    # C++ kaynaginda registry yollari iki satirda yazildigi icin  \\  ile kacisliyor
    # ("Software\\Microsoft\\..."). C++ derleyicisinde bunlar tek bisl gelir; tabloda
    # tek bisl gorunmeli, yoksa yol kopyalanip yapistirilamaz.
    return $out -replace '\\\\', '\'
}

# --- Display names -----------------------------------------------------------
# The UI labels come out of lang.cpp so the table cannot drift from what the app shows.
$names = @{}
if ($langText) {
    # g_tr'nin acilisi iki satira bolunmus:  "= \n {"   -- bu yuzden \s* gerekiyor.
    $m = [regex]::Match($langText, 'g_tr\[\]\s*=\s*\{(.*?)\r?\n\s*\};', 'Singleline')
    if (-not $m.Success) { throw 'g_tr tablosu bulunamadi' }

    # Bilesik dize literal'leri var:  "G\xC3\xBC" "venli Y\xC3\xBC" "kleyici"
    # Bunlar tek bir etiket olusturur, dolayisiyla once dizeler birlestirilir, sonra
    # sayilir. Ayri sayilirsa indeksler kayar ve tablo yanlis isim gosterir.
    $merged = $m.Groups[1].Value -replace '"\s*"', ''

    $row = 0
    foreach ($mm in [regex]::Matches($merged, '"((?:[^"\\]|\\.)*)"')) {
        $names[$row] = Resolve-Escapes $mm.Groups[1].Value
        $row++
    }
}

# Tweak adlari enum'un sonunda: TweakNames = FlatEnd. Dizi icindeki sirayi bulmak icin
# enum'dan FlatEnd degerini okuyoruz; elle 115 yazmak bir sonraki enum'a gecildiginde
# sessizce yanlis tablo uretirdi.
$hdr = if ($langText) { [System.IO.File]::ReadAllText((Resolve-Path 'src\core\lang.hpp').Path) } else { '' }
$catCount = 7
$rowsPerCat = 8
if ($hdr) {
    $mc = [regex]::Match($hdr, 'kTweakCatCount\s*=\s*(\d+)')
    if ($mc.Success) { $catCount = [int]$mc.Groups[1].Value }
    $mr = [regex]::Match($hdr, 'kTweakRows\s*=\s*(\d+)')
    if ($mr.Success) { $rowsPerCat = [int]$mr.Groups[1].Value }
}

# FlatEnd: enum'daki tum sayimlayanlarin toplami. Elle yazilan bir sayi, enum'a bir
# deger eklenince sessizce yanlis indeks verir; FlatEnd sentinel'i bunu onlemek icin
# var, o yuzden sayimi burada da yapmak ayni mantigi paylasir.
$flatEnd = 0
if ($hdr) {
    # Yorumlar FlatEnd aramadan ONCE dusurulur. Duz metin uzerinde esleşen
    # kisisiz `(.*?)FlatEnd`, lang.hpp'e "FlatEnd'den hemen once olmali" diye
    # yazilmis bir yorum cumlesinde duruyor ve sonradan eklenen 44 anahtarin
    # hicbiri sayilmiyordu: tablo 44 girdi geriye kaydi, 56 tweak etiketinin
    # hepsi yanlis satirdan okundu. Derleme sessiz gecirdi cunku static_assert
    # yalnizca boyuta bakiyor.
    $me = [regex]::Match($hdr, 'enum\s+Key\s*\{(.*?)\r?\n\s*\};', 'Singleline')
    if ($me.Success) {
        $body = [regex]::Replace($me.Groups[1].Value, '//[^\r\n]*', '')
        $cut  = $body.IndexOf('FlatEnd')
        if ($cut -lt 0) { throw "lang.hpp icinde FlatEnd bulunamadi" }
        $body = $body.Substring(0, $cut)
        # FlatEnd'den onceki her tanimlayici bir enum degeridir; ilki 0'dan
        # basladigi icin toplam dogrudan FlatEnd'in sayisina esittir.
        # (?m): satir sonunda virgul olmayan son deger de sayilsin.
        $flatEnd = [regex]::Matches($body, '(?m)(?<![A-Za-z0-9_])[A-Za-z_]\w*(?=\s*(,|$))').Count
    }
}
if ($flatEnd -le 0) { throw "FlatEnd sayilamadi ($flatEnd)" }

# CAPRAZ DENETIM. $flatEnd enum metninden sayildi; ayni deger lang.cpp'deki
# "---- TweakNames ----" ayracina kadar olan dize literallerinden de bulunabilir.
# Ikisi farkliysa bir yerde siralama degismistir ve tablo kaymis olurdu - 1.2.0'da
# tam olarak bu oldu: lang.hpp'e yazilmis bir yorum cumlesi "FlatEnd" sozcugunu
# iceriyor, kisisiz eslesme orada kaliyor ve sonradan eklenen 44 anahtar sayilmiyordu.
# Uygulama dogru ciziyordu (enum'u derleyici cozer), yanlis olan uretilen tabloydu.
if ($langText) {
    $mk = [regex]::Match($langText, 'g_tr\[\].*?//\s*-{4}\s*TweakNames', 'Singleline')
    if (-not $mk.Success) { throw "lang.cpp'de TweakNames ayraci bulunamadi" }
    $mkMerged = $mk.Value -replace '"\s*"', ''
    $before = [regex]::Matches($mkMerged, '"((?:[^"\\]|\\.)*)"').Count
    if ($before -ne $flatEnd) {
        throw "FlatEnd cakismasi: enum $flatEnd diyor, lang.cpp ayraci $before. Tweak tablosu kaymis olurdu."
    }
}

function Get-Name([int]$cat, [int]$idx) {
    $k = $flatEnd + ($cat * $rowsPerCat) + $idx
    if ($names.ContainsKey($k) -and $names[$k]) { return $names[$k] }
    return "$cat.$idx"
}

# --- Category 0-3: parse tweaks.cpp -----------------------------------------
# Split into "case N: ... return" chunks inside each ApplyXxx function so an index
# maps to its own calls. A chunk can call RegPut several times (MouseSpeed writes
# three values), and all of them belong to that one tweak.
# Kategori adlari tweaks.cpp'deki bölüm yorumlarindan okunur:  "// ---- Oyun ayarları"
# i18n tablosunda ayri bir kategori bloklari yok, tek basina bir dizeler gecer; okunacak
# yer burasi. Elle liste tutmak, ad degistiginde sessizce eski adi gosterirdi.
$catNames = @()
foreach ($cm in [regex]::Matches($tweakText, '//\s*-+\s*(.+?)\s*\r?\n')) {
    $n = $cm.Groups[1].Value.Trim()
    # "Oyun ayarları" gibi uzun basliklar, uygulama menusundeki kisa adlari da kapsar.
    $catNames += $n
}
# Diger yorumlar da (Oyunlar, FiveM ozel, Gecikme azaltma) bu desene uyar.
if ($catNames.Count -lt $catCount) {
    throw "tweaks.cpp icinden $catCount kategori adi okunamadi ($($catNames.Count))"
}
$catNames = $catNames[0..($catCount - 1)]

$switchRows = @{}

# Anahtar yollari ayrica toplanir: bunlar build\obj\tweak_keys.h dosyasini uretir,
# yani uygulama uygulamadan once hangi anahtarlari yedekleyecegini buradan ogrenir.
$keyRows = @{}

$funcs = @{
    'ApplyPerf' = 0; 'ApplyGame' = 1; 'ApplyPriv' = 2; 'ApplyVis' = 3
}
foreach ($fn in $funcs.Keys) {
    $mi = [regex]::Match($tweakText, "bool\s+$fn\s*\(\s*int\s+i\s*,\s*bool\s+on\s*\)\s*\{(.*?)\n        \}", 'Singleline')
    if (-not $mi.Success) { throw "$fn bulunamadi" }
    $body = $mi.Groups[1].Value

    $cat = $funcs[$fn]
    $cases = [regex]::Matches($body, 'case\s+(\d+)\s*:(.*?)(?=case\s+\d+\s*:|$)', 'Singleline')
    foreach ($c in $cases) {
        $idx = [int]$c.Groups[1].Value
        $txt = $c.Groups[2].Value

        $loc = @()
        $keys = @()
        foreach ($r in [regex]::Matches($txt, 'Reg(?:Put|Get|GetStrIs)\s*\(\s*(HKCU|HKLM)\s*,\s*(k\w+|L"([^"]*)")')) {
            $root = if ($r.Groups[1].Value -eq 'HKCU') { 'HKCU' } else { 'HKLM' }
            $key = if ($r.Groups[2].Value -like 'L*') { $r.Groups[3].Value } else { $r.Groups[2].Value }
            # C++ genis dize literal'inde  \\  tek bisl demektir.
            $full = ($root + '\' + ($key -replace '\\\\', '\'))
            $loc += $full
            $keys += $full
        }
        if ($keys.Count -gt 0) { $keyRows["$cat,$idx"] = $keys }
        foreach ($v in [regex]::Matches($txt, 'Reg(?:Put|Get)\w*\s*\([^,]+,\s*[^,]+,\s*L"([^"]*)"')) {
            $loc += '    deger: ' + $v.Groups[1].Value
        }
        if ([regex]::IsMatch($txt, 'RunCmd\s*\(\s*L"([^"]*)"')) {
            foreach ($v in [regex]::Matches($txt, 'RunCmd\s*\(\s*L"([^"]*)"')) {
                $loc += '    komut: ' + $v.Groups[1].Value
            }
        }
        if ($loc.Count -eq 0) { $loc += '    (yok)' }
        $switchRows["$cat,$idx"] = $loc
    }
}

# --- Category 4-6: parse regpack.cpp ----------------------------------------
# Each array element is a pair {enabled, disabled}. Both bodies must list the same
# keys, so parse the enabled one and keep the disabled one for the value column.
$regRows = @{}
$arrays = @{ 'kGames' = 4; 'kFiveM' = 5; 'kDelay' = 6 }
foreach ($an in $arrays.Keys) {
    $cat = $arrays[$an]
    $mi = [regex]::Match($regText, "const char\*\s+$an\[8\]\[2\]\s*=\s*\{(.*?)\n    \};", 'Singleline')
    if (-not $mi.Success) { throw "$an bulunamadi" }

    # Her eleman bir yorum satırıyla başlıyor:  "{   // 0 - oyun görevi zamanlama önceliği"
    # hemen ardından ham metin işaretçisi geliyor:  R"(Windows Registry Editor ...
    # .NET regex \R'yi tanımıyor, bu yüzden satır sonu için \r?\n yazıldı.
    # Gövde kapanışı  )",  yani  satir sonu + ) + " + ,   -- parantez kaçışı şart.
    $elems = [regex]::Matches($mi.Groups[1].Value,
                '\{\s*//\s*(\d+)\s*-\s*(.*?)\r?\n\s*R"\((.*?)\r?\n\)"\s*,', 'Singleline')
    foreach ($e in $elems) {
        $idx = [int]$e.Groups[1].Value
        $body = $e.Groups[3].Value

        $loc = @()
        $keys = @()
        foreach ($k in [regex]::Matches($body, '(?m)^\[(HKEY_\w+[^\]]*)\]')) {
            $p = $k.Groups[1].Value
            $p = $p -replace '^HKEY_CURRENT_USER', 'HKCU'
            $p = $p -replace '^HKEY_LOCAL_MACHINE', 'HKLM'
            $loc += $p
            $keys += $p
        }
        if ($keys.Count -gt 0) { $keyRows["$cat,$idx"] = $keys }
        foreach ($v in [regex]::Matches($body, '(?m)^"([^"]+)"\s*=')) {
            $loc += '    deger: ' + $v.Groups[1].Value
        }
        if ($loc.Count -eq 0) { $loc += '    (yok)' }
        $regRows["$cat,$idx"] = $loc
    }
}

# --- Emit --------------------------------------------------------------------
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('# İnce ayarların dokunduğu registry konumları')
[void]$sb.AppendLine()
[void]$sb.AppendLine('Bu dosya `tools\make_tweak_table.ps1` ile üretilir — elle düzenleme.')
[void]$sb.AppendLine('Kaynak: `src\core\tweaks.cpp` ve `src\core\regpack.cpp`. Kod değişirse tablo')
[void]$sb.AppendLine('yeniden üretilir, böylece buradaki liste kodun yazdığı şeyden ayrılamaz.')
[void]$sb.AppendLine()
[void]$sb.AppendLine('| # | Ayar | Kategori | Registry konumu |')
[void]$sb.AppendLine('|---|------|----------|-----------------|')

$n = 0
$missing = 0
for ($c = 0; $c -lt 7; $c++) {
    for ($i = 0; $i -lt 8; $i++) {
        $key = "$c,$i"
        $rows = if ($switchRows.ContainsKey($key)) { $switchRows[$key] } elseif ($regRows.ContainsKey($key)) { $regRows[$key] } else { $missing++; continue }
        $n++
        $cells = @()
        foreach ($r in $rows) {
            $cells += $r.Replace('|', '\|').Trim()
        }
        $where = ($cells -join '<br>')
        [void]$sb.AppendLine("| $n | $(Get-Name $c $i) | $($catNames[$c]) | $where |")
    }
}

$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }
$outDir = Split-Path -Parent $outPath
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
[System.IO.File]::WriteAllText($outPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))

Write-Output ("    kaynak tablo yazildi: {0}  ({1} ayar, {2} eksik)" -f $outPath, $n, $missing)

# --- Anahtar basligi (build\obj\tweak_keys.h) ---------------------------------
# Uygulama bir ayari uygulamadan once dokunacagi registry anahtarlarini disa aktarir
# (yedek). Bu anahtarlarin listesi tweaks.cpp ve regpack.cpp'de yazili; ayni kaynaktan
# uretiliyor ki yedek kapsami ile ayarlarin yazdigi yerler ayrisabilsin.
#
# Dize literal'leri C++ genisl dize bicimine cevirilir: ters bisl ikiye kacirilir.
$hdr = New-Object System.Text.StringBuilder
[void]$hdr.AppendLine('// tools/make_tweak_table.ps1 tarafindan uretilir -- elle duzenleme.')
[void]$hdr.AppendLine('//')
[void]$hdr.AppendLine('// Her ayarin dokundugu registry anahtarlari. src\core\backup.cpp bunu okuyarak')
[void]$hdr.AppendLine('// uygulama oncesi yedek alir; elle tutulan bir liste olsaydi yedek kapsami')
[void]$hdr.AppendLine('// ayarlarin gercekten yazdigi yerlerden ayrilabilirdi.')
[void]$hdr.AppendLine()
[void]$hdr.AppendLine('namespace tweakkeys')
[void]$hdr.AppendLine('{')
[void]$hdr.AppendLine('    struct Entry { int cat; int idx; const wchar_t* path; };')
[void]$hdr.AppendLine()
[void]$hdr.AppendLine('    static const Entry kEntries[] =')
[void]$hdr.AppendLine('    {')

$uniq = New-Object System.Collections.Generic.HashSet[string]
$entryCount = 0
for ($c = 0; $c -lt $catCount; $c++) {
    for ($i = 0; $i -lt $rowsPerCat; $i++) {
        $key = "$c,$i"
        if (-not $keyRows.ContainsKey($key)) { continue }
        foreach ($p in $keyRows[$key]) {
            # Ayni anahtar birden fazla ayarda geciyorsa bir kez yeter; yedek tekrarli olmaz.
            if (-not $uniq.Add($p)) { continue }
            $esc = $p.Replace('\', '\\')
            [void]$hdr.AppendLine("        { $c, $i, L`"$esc`" },")
            $entryCount++
        }
    }
}

[void]$hdr.AppendLine('    };')
[void]$hdr.AppendLine()
[void]$hdr.AppendLine("    static const int kCount = $entryCount;")
[void]$hdr.AppendLine('}')
# rc.exe'nin on ekli on ekli dosya sonu satiri bekliyor (RC1004).
[void]$hdr.AppendLine('')

$hdrOut = if ([System.IO.Path]::IsPathRooted($Header)) { $Header } else { Join-Path (Get-Location) $Header }
$hdrDir = Split-Path -Parent $hdrOut
if ($hdrDir -and -not (Test-Path $hdrDir)) { New-Item -ItemType Directory -Path $hdrDir -Force | Out-Null }
[System.IO.File]::WriteAllText($hdrOut, $hdr.ToString(), (New-Object System.Text.UTF8Encoding $false))

Write-Output ("    anahtar basligi yazildi: {0}  ({1} anahtar)" -f $hdrOut, $entryCount)