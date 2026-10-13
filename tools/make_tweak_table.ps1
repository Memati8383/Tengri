# Generates docs/tweaks-registry.md -- the table of every registry location the
# tweaks touch.
#
# Why generated instead of hand-written: the README claims "every registry key this
# writes is visible in the source". A hand-maintained table would violate that claim
# the first time someone edits tweaks.cpp, which is exactly the failure mode the claim
# is meant to rule out.
#
# Two sources, because the tweaks are stored two different ways:
#   categories 0-3            -> switch cases in tweaks.cpp, RegGet/RegPut/RegPutStr calls
#   categories 4..(n-1)        -> raw .reg bodies in regpack.cpp, [HKEY...] headers
# Kategori sayisi lang.hpp'teki kTweakCatCount'ten okunur; asagida elle yazili
# kalan tek yer dizi -> kategori eslemesidir.
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

# Bir C++ dize literalinin GÖVDESini çözümle. Resolve-Escapes kaçış görmeyince
# metne hiç dokunmadan döndürüyor, yani "Control Panel\\Desktop" çift bisll
# olarak kalıyor; anahtar adları için bu hâlde kullanılamaz. Burada kaçış olsun
# ya da olmasın  \\  her zaman  \  olur.
function Unesc-Lit([string]$s) {
    if ($s -match '\\[xu]') { return Resolve-Escapes $s }
    return $s -replace '\\\\', '\'
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
# Kategori adlari tweaks.cpp'deki bolum yorumlarindan okunur. Yeni oyun
# profilleri icin basliklar "Oyun profilleri" tek basina bir baslik oldugu icin
# isim listesine elle eklendi; sira, regpack dizileriyle ayni olmak zorunda.
$catNames = @(
    'Performans', 'Oyun', 'Gizlilik', 'Görsel',
    'Oyunlar', 'FiveM', 'Gecikme',
    'Valorant', 'CS2', 'Fortnite'
)
$catNamesFromSource = @()
foreach ($cm in [regex]::Matches($tweakText, '//\s*-+\s*(.+?)\s*\r?\n')) {
    $n = $cm.Groups[1].Value.Trim()
    # "Oyun ayarları" gibi uzun basliklar, uygulama menusundeki kisa adlari da kapsar.
    $catNames += $n
}
# Diger yorumlar da (Oyunlar, FiveM ozel, Gecikme azaltma) bu desene uyar.
if ($catNames.Count -lt $catCount) {
    throw "kategori adi listesi $catCount kategoriden az ($($catNames.Count))"
}

$switchRows = @{}

# Yazma hedefleri: (kategori,sira,anahtar) -> deger adlari listesi. Yedekleme ve
# geri alma ARTIK bu listeyi kullanıyor; eskiden yalnızca anahtar adları
# biliniyordu ve denetim anahtar adının üstünde kalıyordu.
#
# Yalnızca YAZMA çağrıları sayılır (RegPut, RegPutStr, RegPutDefault). RegGet
# burada yok: okunan bir değeri geri yüklemek, uygulamanın hiç yazmadığı bir
# değere yönetici olarak dokunmak demek olurdu.
$pairs = New-Object System.Collections.Generic.List[object]

function Add-Pair([int]$cat, [int]$idx, [string]$key, [string]$name) {
    if (-not $key) { return }
    $found = $null
    foreach ($p in $pairs) {
        if ($p.cat -eq $cat -and $p.idx -eq $idx -and $p.key -eq $key) { $found = $p; break }
    }
    if (-not $found) {
        $found = [pscustomobject]@{ cat = $cat; idx = $idx; key = $key; names = @() }
        $pairs.Add($found)
    }
    if ($found.names -notcontains $name) { $found.names += $name }
}

# Dize literali ya da sabit değişken adı ayrımını iki yerde de kullanacağız.
function Get-KeyArg([string]$raw, [hashtable]$vars) {
    if ($raw -like 'L"*"') { return Unesc-Lit ($raw.Substring(2, $raw.Length - 3)) }
    if ($vars.ContainsKey($raw)) { return $vars[$raw] }
    return $null
}

# Anahtar yolları ayrica toplanir: bunlar build\obj\tweak_keys.h dosyasini uretir,
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

        # Dosya/scope düzeyindeki anahtar sabitleri:  const wchar_t* kCtx = L"Software\\...";
        # Bu çağrılar literal vermiyor, değişken veriyor; değişkeni çözmeden hedef
        # anahtar bilinemez (bir dönem RegPutDefault hiç eşleşmediği için sessizce
        # yedek kapsamı dışına çıkıyordu).
        $vars = @{}
        foreach ($vm in [regex]::Matches($tweakText, 'const\s+wchar_t\*\s*(k\w+)\s*=\s*L"((?:[^"\\]|\\.)*)"')) {
            $vars[$vm.Groups[1].Value] = Unesc-Lit $vm.Groups[2].Value
        }
        foreach ($vm in [regex]::Matches($txt, 'const\s+wchar_t\*\s*(k\w+)\s*=\s*L"((?:[^"\\]|\\.)*)"')) {
            $vars[$vm.Groups[1].Value] = Unesc-Lit $vm.Groups[2].Value
        }

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

        # Yazma hedefleri: (root, anahtar, deger adi) üçlüsü. Üçüncü argüman
        # dize adı olduğu için çağrı satır boyunca dağılabiliyor; Singleline şart.
        foreach ($w in [regex]::Matches($txt,
                        'Reg(Put|PutStr)\s*\(\s*(HKCU|HKLM)\s*,\s*(L"(?:[^"\\]|\\.)*"|k\w+)\s*,\s*L"((?:[^"\\]|\\.)*)"',
                        'Singleline')) {
            $root = if ($w.Groups[2].Value -eq 'HKCU') { 'HKCU' } else { 'HKLM' }
            $keyRaw = $w.Groups[3].Value
            $key = if ($keyRaw -like 'L*') { Unesc-Lit ($keyRaw.Substring(2, $keyRaw.Length - 3)) }
                   else { Get-KeyArg $keyRaw $vars }
            if (-not $key) { throw "$fn case $idx icinde cozulemeyen anahtar: $keyRaw" }
            Add-Pair $cat $idx ($root + '\' + $key) (Unesc-Lit $w.Groups[4].Value)
        }
        # Varsayılan değer: adı boş. RegPutDefault anahtarın "@" değerini yazar,
        # yani yedekte (anahtar, "") çifti olarak durmalı.
        foreach ($w in [regex]::Matches($txt,
                        'RegPutDefault\s*\(\s*(HKCU|HKLM)\s*,\s*(L"(?:[^"\\]|\\.)*"|k\w+)',
                        'Singleline')) {
            $root = if ($w.Groups[1].Value -eq 'HKCU') { 'HKCU' } else { 'HKLM' }
            $key = Get-KeyArg $w.Groups[2].Value $vars
            if (-not $key) { throw "$fn case $idx icinde cozulemeyen varsayilan anahtar: $($w.Groups[2].Value)" }
            Add-Pair $cat $idx ($root + '\' + $key) ''
        }
        foreach ($v in [regex]::Matches($txt, 'Reg(?:Put|Get)\w*\s*\([^,]+,\s*[^,]+,\s*L"([^"]*)"')) {
            $loc += '    deger: ' + $v.Groups[1].Value
        }
        # RunCmd araci System32'den tam yolla cagirdigi icin iki parametre alir:
        # ad ve argumanlar. Tablo ikisini birlestirip gercek komut satirini
        # gosterir; yalnizca adi yazmak (bir donem boyleydi) dokumanin tek
        # katma degerini - "aslinda ne calisiyor" sorusunun cevabini - siler.
        foreach ($v in [regex]::Matches($txt, 'RunCmd\s*\(\s*L"([^"]*)"\s*,\s*L"([^"]*)"')) {
            $loc += '    komut: ' + $v.Groups[1].Value + ' ' + ($v.Groups[2].Value -replace '\\\\', '\')
        }
        if ($loc.Count -eq 0) { $loc += '    (yok)' }
        $switchRows["$cat,$idx"] = $loc
    }
}

# --- Regpack kategorileri: parse regpack.cpp --------------------------------
# Each array element is a pair {enabled, disabled}. Both bodies must list the same
# keys, so parse the enabled one and keep the disabled one for the value column.
$regRows = @{}
$arrays = @{ 'kGames' = 4; 'kFiveM' = 5; 'kDelay' = 6
             'kValorant' = 7; 'kCS2' = 8; 'kFortnite' = 9 }
foreach ($an in $arrays.Keys) {
    $cat = $arrays[$an]
    $mi = [regex]::Match($regText, "const char\*\s+$an\[8\]\[2\]\s*=\s*\{(.*?)\n    \};", 'Singleline')
    if (-not $mi.Success) { throw "$an bulunamadi" }

    # Her eleman bir yorum satırıyla başlıyor:  "{   // 0 - oyun görevi zamanlama önceliği"
    # hemen ardından ham metin işaretçisi geliyor:  R"(Windows Registry Editor ...
    # .NET regex \R'yi tanımıyor, bu yüzden satır sonu için \r?\n yazıldı.
    # Gövde kapanışı  )",  yani  satir sonu + ) + " + ,   -- parantez kaçışı şart.
    # İki gövde birden okunuyor: kapalı gövde, açık gövdenin yazmadığı bir değeri
    # silebiliyor ("Affinity"=-); yedek o değeri de geri getirebilmeli.
    $elems = [regex]::Matches($mi.Groups[1].Value,
                '\{\s*//\s*(\d+)\s*-\s*(.*?)\r?\n\s*R"\((.*?)\r?\n\)"\s*,\s*R"\((.*?)\r?\n\)"', 'Singleline')
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

        # Yazma hedefleri: satır satır yürü, [HKEY_...] bölümünün altındaki her
        # değer satırı bir (anahtar,değer) çiftidir. Silme biçimi ("X"=-) de aynı
        # çifti verir: uygulamanın o değere dokunduğunun kanıtıdır.
        foreach ($gb in @($e.Groups[3].Value, $e.Groups[4].Value)) {
            $cur = $null
            foreach ($ln in ($gb -split "\r?\n")) {
                $t = $ln.Trim()
                if ($t.StartsWith('[')) {
                    $k2 = $t.TrimStart('[').TrimEnd(']')
                    if ($k2 -notmatch '^HKEY_(CURRENT_USER|LOCAL_MACHINE)\\') { $cur = $null; continue }
                    $cur = ($k2 -replace '^HKEY_CURRENT_USER', 'HKCU') -replace '^HKEY_LOCAL_MACHINE', 'HKLM'
                    continue
                }
                if (-not $cur) { continue }
                if ($t.StartsWith('@=')) { Add-Pair $cat $idx $cur ''; continue }
                $m2 = [regex]::Match($t, '^"([^"]+)"\s*=')
                if ($m2.Success) { Add-Pair $cat $idx $cur $m2.Groups[1].Value }
            }
        }
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
for ($c = 0; $c -lt $catCount; $c++) {
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
[void]$hdr.AppendLine('// Her ayarin DOKUNDUGU (anahtar, deger adi) ciftleri. src\core\backup.cpp')
[void]$hdr.AppendLine('// uygulamadan once bu ciftlerin mevcut degerlerini disa aktarir; geri alma')
[void]$hdr.AppendLine('// yalnizca buradaki ciftlere yazabilir. Kapsam deger adina kadar iniyor,')
[void]$hdr.AppendLine('// cunku yukseltme anahtar adinda degil deger adinda duruyor:')
[void]$hdr.AppendLine('// HKLM\SYSTEM\CurrentControlSet\Control meşru bir yedek anahtarıdır ama')
[void]$hdr.AppendLine('// icindeki Session Manager\BootExecute SYSTEM kodekosumu demekti.')
[void]$hdr.AppendLine('//')
[void]$hdr.AppendLine('// Elle tutulan bir liste olsaydi yedek kapsami ayarlarin gercekten')
[void]$hdr.AppendLine('// yazdigi yerlerden ayrilabilirdi.')
[void]$hdr.AppendLine()
[void]$hdr.AppendLine('namespace tweakkeys')
[void]$hdr.AppendLine('{')
[void]$hdr.AppendLine('    // values: nullptr ile biten deger adi listesi. Bos liste olamaz;')
[void]$hdr.AppendLine('    // varsayilan degerin adi bos dizedir, nullptr ile karistirilmaz.')
[void]$hdr.AppendLine('    struct Entry { int cat; int idx; const wchar_t* path;')
[void]$hdr.AppendLine('                   const wchar_t* const* values; };')
[void]$hdr.AppendLine()

# Deger adi listeleri: her (kategori,sira,anahtar) cifti kendi dizisini aliyor.
# (Bu degiskenin adi yukaridaki $arrays eslemesinden ayri olmali: ayni ada atamak
# regpack yorum-dizisi eslemesini sessizce ezerdi.)
$valueArrays = New-Object System.Text.StringBuilder
$lines = New-Object System.Collections.Generic.List[string]
$v = 0
for ($c = 0; $c -lt $catCount; $c++) {
    for ($i = 0; $i -lt $rowsPerCat; $i++) {
        foreach ($p in @($pairs | Where-Object { $_.cat -eq $c -and $_.idx -eq $i })) {
            $names = @($p.names)
            if ($names.Count -eq 0) { continue }
            $esc = $p.key.Replace('\', '\\')
            $list = New-Object System.Collections.Generic.List[string]
            foreach ($nm in $names) { $list += 'L"' + $nm.Replace('\', '\\') + '"' }
            [void]$valueArrays.AppendLine("    static const wchar_t* const kVals$v[] = { " + ($list -join ', ') + ', nullptr };')
            $lines.Add("        { $c, $i, L`"$esc`", kVals$v },")
            $v++
        }
    }
}
$entryCount = $v

[void]$hdr.AppendLine($valueArrays.ToString())
[void]$hdr.AppendLine('    static const Entry kEntries[] =')
[void]$hdr.AppendLine('    {')
foreach ($l in $lines) { [void]$hdr.AppendLine($l) }

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