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
  [int]$SettleMs = 1500
)

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
    @{ File = '01-dashboard';   Y = 128 }
    @{ File = '02-cleaner';     Y = 172 }
    @{ File = '03-tweaks';      Y = 216 }
    @{ File = '04-network';     Y = 260 }
    @{ File = '05-system-info'; Y = 304 }
    @{ File = '06-settings';    Y = 348 }
    @{ File = '07-about';       Y = 392 }
)

foreach ($page in $pages) {
    Click-At 100 $page.Y
    Start-Sleep -Milliseconds $SettleMs
    # İmleci içerik alanına bırak: parçacık alanı ve üst ışık son tıklamanın
    # tepkisinde değil, temsili bir durumda olsun.
    Hover-At 600 400
    Start-Sleep -Milliseconds 900
    Save-Shot "$OutDir\$($page.File).png"
    Write-Output ("  {0}" -f $page.File)
}

Write-Output "done"