# Regenerates every screenshot in docs\screenshots.
#
# One pass, in one run: the pages are captured from a single app instance so the
# font, the palette and the layout in all of them provably match. Running the
# older per-page scripts separately allowed the app state to drift between
# captures, which is how a set of screenshots ends up showing two different UIs.
#
# The island is a separate window with preferences that are read once at startup,
# so the run has two phases: the ten app pages are shot with the island HIDDEN
# (nothing overlaps the window, and no media session is read), then a second
# instance is started with the island shown for its own frame.
#
#   .\tools\capture_all.ps1
#
# Requires a built TENGRI.exe (build.bat). shoot.ps1 drives build\TENGRI_shot.exe
# so a real user session is never disturbed.
param(
  [string]$OutDir = "docs\screenshots",
  # Sayfa basina bekleme; animasyonlar bitmeden goruntu alinirsa yarim kalmis
  # gecisler kaydedilir.
  [int]$SettleMs = 1500,
  # Gecikme grafigi 2 saniyede bir örnek alır. Varsayilan bekleme yeterli olmadigi
  # icin o sayfa ayrica bekletilir: yalniz bir ornek dolu bir grafik, dokuz bos
  # kutuyla yenilenmis hatanin ayni goruntusunu uretirdi.
  [int]$LatencySettleSec = 40,
  # Ada kartinin varlik denetimi: karede bulunmasi gereken doygun yesil aksani
  # ornegi. Olculdu: ada karti 210, saf masaustu/dashboard ~0. Esik iki dagilimin
  # arasinda, gurultuye karsi pay birakarak secildi.
  [int]$IslandMinGreen = 40
)

# build.bat only produces build\TENGRI.exe; the alternate name exists so the capture
# never touches a running user session. Copying it here means one command does the
# whole job, and the copy is refreshed on every run so the images always show the
# current build instead of whatever was staged weeks ago.
if (-not (Test-Path "build\TENGRI.exe")) {
    throw "build\TENGRI.exe yok. Once build.bat calistir."
}
Copy-Item "build\TENGRI.exe" "build\TENGRI_shot.exe" -Force

# Makine bilgisi ekranlarinda gercek degerler yerine sabit ornekler cizilir:
# HWID, bilgisayar/oturum adi, islemci/ekran karti/ana kart/BIOS, RAM hizi,
# ag cardinin adi, yerel IP, baslatma listesi ve lisans maskesi. Start-Process
# ortam degiskenini cocuk surece aktardigi icin tek satir yeter.
$env:TENGRI_SHOT = "1"

# Profizola: APPDATA ve LOCALAPPDATA cocuk surece gecici bir altina yonlendirilir.
# Iki sebebi var ve ikisi de gercek kullanici verisiyle ilgilidir:
#   1. Ada tercihleri %APPDATA%\TENGRI\island.dat icinde duruyor. Yakalama iki
#      fazda calisiyor (once adanin gizli oldugu on sayfa, sonra adanin acik
#      oldugu kare) ve fazlar arasinda bu dosya yaziliyor. Gerçek dosyaya
#      dokunmak, kullanıcının kendi tema/gorunurluk secimini bozardi.
#   2. Uygulama açılışta uyumsuz disk önbelleğini siliyor (TGDS surumu != 3).
#      Bu davranis kasıtlı ve belgelenmiş; ama bir yakalama oturumunun
#      kullanıcının son taramasını silmesi baska bir sey.
# Ada ise TENGRI_SHOT=1 altında gerçek medya oturumunu hiç okumuyor ve
# lrclib.net'e istek atmiyor (bkz. src/core/island.cpp, Worker).
$shotRoot = Join-Path $env:TEMP ("TENGRI-capture-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
$realAppData = [Environment]::GetFolderPath('ApplicationData')   # asagida eziliyor
$env:APPDATA     = Join-Path $shotRoot "Roaming"
$env:LOCALAPPDATA = Join-Path $shotRoot "Local"
New-Item -ItemType Directory -Force -Path ($env:APPDATA + "\TENGRI") | Out-Null
New-Item -ItemType Directory -Force -Path $env:LOCALAPPDATA | Out-Null

# island.dat bes bayt ASCII rakam: [etkin, yalniz masaustunde, sozler, tema, gorunur]
# Tema baydi ASCII rakam olarak okunur (Save() da oyle yaziyor): '0' Black, '1' Spotify.
function Set-IslandPrefs([bool]$Shown, [string]$ThemeChar) {
    $text = "1" + "0" + "1" + $ThemeChar + $(if ($Shown) { "1" } else { "0" })
    [IO.File]::WriteAllBytes((Join-Path $env:APPDATA "TENGRI\island.dat"),
                             [Text.Encoding]::ASCII.GetBytes($text))
}
Set-IslandPrefs -Shown $false -ThemeChar '0'

. .\tools\shoot.ps1

try {

$p = Start-App
Start-Sleep -Seconds 7

# --- 00: login ---------------------------------------------------------------
Save-Shot "$OutDir\00-login.png"
# Giriş ekranının içeriğinin kaba izi diskteki dosyadan alınır: sonraki denetim
# canlı kareyi bu izle karşılaştırıyor. Eşiğin parlak piksel sayısıyla kurulması
# yanlış çıktı — o sayı menüde hangi satırın seçili olduğuna göre değişiyor.
$loginSig = Get-ImageSignature "$OutDir\00-login.png"

# Lisans ekraninda "Lisansı etkinleştir" düğmesi. Demo kipinde her anahtar kabul
# edilir AMA bos anahtar reddedilir (app.cpp TryActivate: `if (!g_key[0])`). Bu
# yuzden once alana tiklanip bir anahtar yazilir, sonra dugmeye basilir.
#
# Profil izole oldugu icin (asagidaki gecici APPDATA) kayitli lisans yok; eski
# yakalamalar gercek profildeki "beni hatirla" degerine guveniyordu ve yalinciz
# tiklama yeterliydi. Izolasyon geldikten sonra alana yazmadan gecilemiyor.
function Enter-License {
    Click-At 490 326                 # lisans girisi alanina odaklan
    Start-Sleep -Milliseconds 700
    Type-Text "LIFE-2026-ABCD-WXYZ"  # demo anahtari; CharsUppercase buyuk harfe cevirir
    Start-Sleep -Milliseconds 700
    Click-At 490 439                 # Lisansı etkinleştir
}

Enter-License
Start-Sleep -Seconds 14

# Giris ekrani gercekten gecildi mi? Bos anahtarla reddedilen bir etkinlestirme
# karti sarsiyor ve hata satiri ciziyor; bu, kaba izi giristen >6 ornek ayiriyor
# ama bir sayfa degisikligi degil. Gercek bir sayfaya gecis olcuilen 40+ fark
# uretiyor, o yuzden esik 22. Basarisiz olursa anahtar bastan yazilir.
$deneme = 0
while (Test-SameAsSignature $loginSig 22) {
    if ($deneme -ge 5) {
        throw "giris ekrani $deneme denemede gecilemedi; alan odaklanmiyor ya da tiklama hedefi kaymis olabilir"
    }
    $deneme++
    Write-Output ("  giris ekrani hala acik - anahtar yeniden yaziliyor ({0}/5)" -f $deneme)
    Enter-License
    Start-Sleep -Seconds 12
}

# --- sidebar satirlari -------------------------------------------------------
# Nav listesi y=128'den baslar, her oge 44px. Y degerleri capture_about.ps1 ile
# ayni kayan noktaya dayanir; kaydirma yok.
$pages = @(
    @{ File = '01-dashboard';   Y = 128; WaitMs = $SettleMs }
    @{ File = '02-cleaner';     Y = 172; WaitMs = $SettleMs }
    @{ File = '03-tweaks';      Y = 216; WaitMs = $SettleMs }
    # Gecikme sayfası: ölçüm 2 saniyede bir örnek alıyor, varsayılan bekleme
    # tek örnekle yetiyor ve grafik boş görünüyor.
    @{ File = '04-network';     Y = 260; WaitMs = $LatencySettleSec * 1000 }
    @{ File = '05-system-info'; Y = 304; WaitMs = $SettleMs }
    @{ File = '06-settings';    Y = 348; WaitMs = $SettleMs }
    @{ File = '07-about';       Y = 392; WaitMs = $SettleMs }
    @{ File = '08-startup';     Y = 436; WaitMs = $SettleMs }
    @{ File = '09-services';    Y = 480; WaitMs = $SettleMs }
)

foreach ($page in $pages) {
    Click-At 100 $page.Y
    Start-Sleep -Milliseconds $page.WaitMs
    # İmleci içerik alanına bırak: parçacık alanı ve üst ışık son tıklamanın
    # tepkisinde değil, temsili bir durumda olsun.
    Hover-At 600 400
    Start-Sleep -Milliseconds 900
    Save-Shot "$OutDir\$($page.File).png"
    Write-Output ("  {0}" -f $page.File)
}

# --- kaydedilen dosyalar gerçekten farklı sayfalar mı --------------------------
# Yanlış pencereyi yakalamak tek sessiz hata biçimi değil: bir de aynı sayfanın
# üst üste yazılması var. Denetim, ekranın kendisinden değil diskteki dosyalardan
# yapılıyor; yani yayına giden şeyin ta kendisi ölçülüyor.
#
# Kapsam bilinçli olarak sayfa listesiyle sınırlı: *.png taraması yapılırsa
# klasörde kalmış eski bir dosya denetime karışır ve ardışık fark sıralaması
# bozulur. Ada karesi ayrı bir yüzey olduğu için aşağıda kendi ölçümü var.
$sig = @{}
foreach ($page in $pages) {
    $f = Join-Path $OutDir "$($page.File).png"
    if (-not (Test-Path $f)) { throw "$f yazilmadi; yakalama yarim kalmis" }
    $sig[$page.File] = Get-ImageSignature $f
}
$adlar = @($sig.Keys | Sort-Object)
$enKucuk = 999
for ($i = 1; $i -lt $adlar.Count; $i++) {
    $d = Compare-Signature $sig[$adlar[$i - 1]] $sig[$adlar[$i]]
    if ($d -lt $enKucuk) { $enKucuk = $d }
    if ($d -lt 6) {
        throw ("{0} ile {1} aynı sayfayı gösteriyor (fark {2} örnek); tiklama hedefini bulamamis olabilir" -f `
                $adlar[$i - 1], $adlar[$i], $d)
    }
}
Write-Output ("dogrulandi: {0} goruntu, ardizik en kucuk fark {1} ornek" -f $adlar.Count, $enKucuk)

# --- faz B: ada -------------------------------------------------------------------
# Ada ayri bir pencere ve tercihleri diskten bir kez, açılışta okunuyor; bu yüzden
# on sayfa adanin GIZLI oldugu bir oturumda çekildi (üst üste binmesin, gerçek medya
# oturumu okunmasin) ve kare ikinci bir oturumda aliniyor.
Stop-App
Set-IslandPrefs -Shown $true -ThemeChar '1'   # gorunur + Spotify teması
$p = Start-App
Start-Sleep -Seconds 8

$isl = Get-IslandHwnd $p.Id "_Island"
$r = Get-Rect $isl
# Pencere 460x340 birimlik bir alan; kart x=20, y=10, açılınca 420x196. DPI 100'de
# birebir, degilse pencere genisliginden olceklenir.
$s = $r.Width / 460.0
$cx = $r.Left + [int](20 * $s)
$cy = $r.Top  + [int](10 * $s)
$cw = [int](420 * $s)
$chh = [int](196 * $s)

# Kart hover ile açılıyor: önce katlanmış çubuğun üstüne gel, sonra animasyonun
# bitmesi için bekle. Tıklama yok - adanin dügmeleri gerçek oynatıcıya komut
# gönderir; TENGRI_SHOT altında Push() zaten kapali, ama görselde de basil durum
# istemiyoruz.
Hover-Abs ($r.Left + [int]($r.Width / 2)) ($cy + [int](17 * $s))
Start-Sleep -Milliseconds 2200
Hover-Abs ($r.Left + [int]($r.Width / 2)) ($cy + [int](17 * $s))
Start-Sleep -Milliseconds 800
Save-RectShot $cx $cy $cw $chh "$OutDir\10-island.png"

# Kare gercekten adayı mı gösteriyor? Bos bir masaüstü de ayni dikdörtgeni
# doldururdu; Save-Shot'un one-getirme denetimi burada geçerli degil.
#
# Eskinin esigi parlaklik dagilimiydi (ada > bos masaustu). O varsayim yanlismis:
# kart duz, koyu yesil bir panel oldugu icin dagilimi hareketli bir masaustunden
# DAHA dar cikabiliyor (olculdu: ada 45 / bos 70) ve saglam bir kareyi hata sayiyordu.
# Denetimi artik temanin kendine has doygun yesil aksaniyesi uzerine kuruyoruz:
# kartta ilerleme cubugu/ses dolusu/halka yesili var, saf masaustunde yok. Iki
# dagilim da yazdirilir ki esigin hâlâ ayrim yaptigi her calismada gorulsun.
$islGreen = Get-GreenAccentCount "$OutDir\10-island.png"
$islSpread = Get-FrameSpread "$OutDir\10-island.png"
Stop-App
Start-Sleep -Milliseconds 1500
$empty = Join-Path $env:TEMP "tengri-island-empty.png"
Save-RectShot $cx $cy $cw $chh $empty
$emptyGreen = Get-GreenAccentCount $empty
$emptySpread = Get-FrameSpread $empty
Remove-Item $empty -ErrorAction SilentlyContinue
Write-Output ("ada yesil aksani: ada {0} / bos {1}   (genislik: ada {2} / bos {3})" -f `
        $islGreen, $emptyGreen, $islSpread, $emptySpread)
if ($islGreen -lt $IslandMinGreen) {
    throw ("ada karesi bos gorunuyor (yesil $islGreen < $IslandMinGreen); kart acilmamis olabilir")
}
if ($islGreen -le $emptyGreen) {
    throw ("ada karesi bos masaustunden ayismiyor (yesil $islGreen <= $emptyGreen)")
}

Write-Output "done"

} finally {
    Stop-App
    # Bildirim kısayolu %APPDATA% degil SHGetFolderPath ile bulunuyor, yani gercek
    # profilin icine yaziliyor. AUMID ve simge ayni kaldigi icil degismez; bu kareyi
    # TENGRI_shot.exe'ye çevirmis olursa gerçek uygulama onu düzeltmez ve toast
    # düğmeleri yagma kopyasini baslatirdi. Bizi bırakıp gerçek exe kendi yazsin.
    try {
        $lnkPath = Join-Path $realAppData "Microsoft\Windows\Start Menu\Programs\TENGRI.lnk"
        if (Test-Path $lnkPath) {
            $sh = New-Object -ComObject WScript.Shell
            $target = $sh.CreateShortcut($lnkPath).TargetPath
            if ($target -match 'TENGRI_shot\.exe$') {
                Remove-Item $lnkPath -Force
                Write-Output "  bildirim kisayoli temizlendi (hedef: $target)"
            }
        }
    } catch { Write-Output "  kisayol denetimi atlandi: $($_.Exception.Message)" }

    # Izole profil burada birakilmaz: icinde adanin tercihleri ve ilk calistirma
    # kayitlari duruyor, hicbiri yayina gitmiyor.
    if (Test-Path $shotRoot) { Remove-Item $shotRoot -Recurse -Force -ErrorAction SilentlyContinue }
}