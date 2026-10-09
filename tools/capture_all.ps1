# Regenerates every screenshot in docs\screenshots.
#
# One pass, in one run: the pages are captured from a single app instance so the
# font, the palette and the layout in all of them provably match. Running the
# older per-page scripts separately allowed the app state to drift between
# captures, which is how a set of screenshots ends up showing two different UIs.
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
  [int]$LatencySettleSec = 40
)

# build.bat only produces build\TENGRI.exe; the alternate name exists so the capture
# never touches a running user session. Copying it here means one command does the
# whole job, and the copy is refreshed on every run so the images always show the
# current build instead of whatever was staged weeks ago.
if (-not (Test-Path "build\TENGRI.exe")) {
    throw "build\TENGRI.exe yok. Once build.bat calistir."
}
Copy-Item "build\TENGRI.exe" "build\TENGRI_shot.exe" -Force

. .\tools\shoot.ps1

$p = Start-App
Start-Sleep -Seconds 7

# --- 00: login ---------------------------------------------------------------
Save-Shot "$OutDir\00-login.png"

# Lisans ekraninda "Lisansı etkinleştir" düğmesi. Demo kipinde her anahtar kabul
# edildiği için içeriğe tıklamak yeterli; düğmenin ortası arayüzün ortasıdır.
Click-At 490 439
Start-Sleep -Seconds 16

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

Write-Output "done"