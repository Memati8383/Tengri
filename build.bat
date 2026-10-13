@echo off
rem ==========================================================================
rem  TENGRI - derleme giris noktasi
rem
rem    build.bat              uygulama + alti test paketi   (varsayilan, CI)
rem    build.bat app          yalnizca build\TENGRI.exe
rem    build.bat tests        yalnizca alti test paketi
rem    build.bat test disk    tek paket: pure modules license write update disk
rem    build.bat clean        build\obj, build\tobj ve exe dosyalarini siler
rem    build.bat help         bu liste
rem
rem  Cikis kodu: 0 basari, 1 derleme/test hatasi, 2 hatali arguman.
rem
rem  Konsol kurallari
rem    - Metin ASCII: cmd.exe kod sayfasina (857) yazar, UTF-8'e degil. rem
rem      satirlari da ayni kurala uyar.
rem    - Renk: yalnizca Windows 10+ ve etkilesimli ortamda otomatik acilir.
rem      NO_COLOR ya da TENGRI_PLAIN tanimlaninca, ayrica CI, TF_BUILD ve
rem      GITHUB_ACTIONS varken kapanir; log dosyasinda kacis dizisi gorunmez.
rem    - Ilerleme cubugu: = biten, ok isareti calisan, bosluk bekleyen asama;
rem      her asamanin basinda ve sonunda yazilir, konsol basligina da yansir.
rem      NOTITLE=1 baslik guncellemesini kapatir.
rem    - Konsol satirlarinda unlu isaret yok: gecikmeli genisleme acikken tek
rem      unlu isareti sessizce siler ve ardindaki metni yutar.
rem    - Etiketler: [i] bilgi  [+] basari  [?] uyari  [x] hata  (renkliyse boyali)
rem
rem  Dosya duzeni
rem    1. sabitler ve test paketi tablosu
rem    2. arguman cozumleme ve asama plani
rem    3. kosum
rem    4. asama makinesi (stage, commit, fail, bar, summary)
rem    5. is yapan alt programlar
rem ==========================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0" || exit /b 1
call :colors

rem ==========================================================================
rem  1. SABITLER
rem ==========================================================================
set "IMGUI=third_party\imgui"

rem --- kaynaklar: gruplar ayri tutulur, okunmasi ve diff'i kolay -------------
set "SRC_APP=src\main.cpp src\app.cpp src\tray.cpp"
set "SRC_GUI=src\gui\theme.cpp src\gui\fx.cpp src\gui\icons.cpp src\gui\brand_icons.cpp src\gui\logo.cpp src\gui\logo_data.cpp src\gui\font_data.cpp src\gui\widgets.cpp"
set "SRC_CORE=src\core\license.cpp src\core\sysinfo.cpp src\core\sysinfo_detail.cpp src\core\sysinfo_wmi.cpp src\core\cleaner.cpp src\core\tweaks.cpp src\core\network.cpp src\core\lang.cpp src\core\ram.cpp src\core\regpack.cpp src\core\elevate.cpp src\core\backup.cpp src\core\restore.cpp src\core\startup.cpp src\core\services.cpp src\core\notify.cpp src\core\update.cpp src\core\diskanalyzer.cpp src\core\island.cpp"
set "SRC_IMGUI=%IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp %IMGUI%\backends\imgui_impl_win32.cpp %IMGUI%\backends\imgui_impl_dx11.cpp"
set "SOURCES=%SRC_APP% %SRC_GUI% %SRC_CORE% %SRC_IMGUI%"

rem --- derleyici bayraklari ---------------------------------------------------
rem CLAPP
rem   /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS: C++/WinRT
rem   (src\core\notify.cpp) experimental/coroutine basligini ceker; yeni MSVC
rem   bunu STL1011 ile HATA sayar. Test derlemesi WinRT kullanmadigi icin almaz.
rem   /guard:cf: denetim akisi korumasi; dogrulanmayan dolayli cagri loader'da
rem   durur. Test paketlerinde gerek yok: dolayli cagri siralama ve WinRT yok.
set "CLAPP=/nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 /MP /guard:cf /DNDEBUG /DUNICODE /D_UNICODE /DIMGUI_DEFINE_MATH_OPERATORS /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /DTENGRI_HAS_TWEAK_KEYS"
set "CLINC=/I src /I build\obj /I %IMGUI% /I %IMGUI%\backends"
set "CLTEST=/nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 /MP /DUNICODE /D_UNICODE /I src"

rem CLINK
rem   /DELAYLOAD bir bagin cozumlemesini surec baslangicindan ilk kullanima
rem   tasir. Gerekce bag guvenligi: statik bag surec baslamadan cozulur ve
rem   arama uygulamanin kendi klasorunden, sonra calisma klasorunden baslar;
rem   WinMain'in ilk satirindaki SetDefaultDllDirectories o anda cok gec kalir.
rem   Gecikmeli bag bayraktan SONRA cozulur ve yalnizca System32'den gelir.
rem   Liste olculerek secildi: 16 cekirdek bagindan 8'i surec baslangicinda
rem   henuz yuklenmis degildi, yani adla aranip cekilebiliyordu. Diger 8'i
rem   yuklendigi icin hic aramaya cikmadi ve statik birakildi: advapi32,
rem   kernel32, gdi32, user32, shell32, ole32, oleaut32, imm32.
set "CLINK=/link /guard:cf build\obj\tengri.res /SUBSYSTEM:WINDOWS /DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll /DELAYLOAD:dwmapi.dll /DELAYLOAD:iphlpapi.dll /DELAYLOAD:version.dll /DELAYLOAD:winhttp.dll /DELAYLOAD:bcrypt.dll"
set "CLLIB=delayimp.lib d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib user32.lib gdi32.lib advapi32.lib shell32.lib iphlpapi.lib comctl32.lib version.lib windowscodecs.lib ole32.lib windowsapp.lib propsys.lib wbemuuid.lib winhttp.lib bcrypt.lib"

set "BARW=28"
set "RULE=+----------------------------------------------------------------------+"

rem --- test paketi tablosu ----------------------------------------------------
rem Alanlar: LBL etiket, SRC kaynaklar, LIB kutuphaneler, KEY (1 ise
rem build\obj\tweak_keys.h gerekir), HINT hata ipucu, SHORT komut satiri kisaltmasi.
rem :package bunlari okur; paket basina ayri cl blogu yok. Yeni paket eklemek
rem icin PKGS listesine ad yazmak ve alti alani doldurmak yeter, asama plani
rem listeden hesaplanir.
rem
rem test_pure: registry'ye dokunmayan saf mantik. D3D11 ve ImGui baglanmaz,
rem   ekran gerektirmez, saniyeler icinde kosar. Registry YAZAN kodu icerir
rem   (tweaks.cpp, regpack.cpp) ama test o yollari cagirmaz, yalnizca govde
rem   metinlerini okur; advapi32 yine de baglanir.
rem test_modules: restore, startup, services ve shader temizleyicisinin
rem   salt-okunur yuzeyi ve guvenlik kapilari. Registry YAZMAZ, dosya SILMEZ,
rem   hizmet durumunu DEGISTIRMEZ; yukseltilmemis surecte de ayni sonucu verir.
rem test_license: maskeleme, sys::Hwid ve beni-hatirla kaliciligi. Validate()
rem   BILINCLI olarak test disidir. Kalicilik testi APPDATA'yi _putenv_s ile
rem   gecici klasore yonlendirir; SetEnvironmentVariableA CRT environ'unu
rem   guncellemedigi icin YANLISTIR ve gercek license.dat'i silerdi.
rem test_write_paths: cleaner ve backup'in GERCEK kodunu dosya ve varlik
rem   etkisiyle kosar, ama TEMP, LOCALAPPDATA, WINDIR, ProgramData ve
rem   ProgramFiles(x86) _putenv_s ile izole gecici agaca yonlendirilir; gercek
rem   onbellekler ya da Windows klasoru ASLA silinmez. backup yalnizca reg.exe
rem   EXPORT yapar; Restore, import ve Geri Donusum Kutusu hic cagrilmaz.
rem test_update: surum siralamasi, latest.json ayristirmasi, sunucu allowlist,
rem   SHA-256 cekirdegi, takas plani. AG YOK (CheckNow, StageNow cagrilmaz),
rem   REGISTRY YOK (LoadPrefs, SavePrefs, MarkChecked cagrilmaz). Apply yalnizca
rem   RED yoluyla denenir; kabul yolu kendi exe'sinin adini degistirirdi.
rem test_disk: MFT ayristirici, data run cozumleyici, treemap yerlesimi,
rem   onbellek bicimi. GERCEK DISK OKUNMAZ: sahte kayit tamsamalari bellekte
rem   kurulur, ham bolume dokunulmaz, tarama baslatilmaz, silme cagrilmaz. Tek
rem   dosya isi GetTempPath altinda kendi yarattigi gecici onbellek dosyasidir.
rem
rem NOT: tools\probe_game_tweaks.cpp ayri bir gidis-donus sondasidir ve BILINCLI
rem   olarak bu betigin parcasi degildir. Gercek sistem registry'sine yazar
rem   (HKLM), yonetici yetkisi ister ve test_pure.exe'in registry yazmaz sozunu
rem   ihlal eder. Elle derleme (tek satir, sonra yonetici olarak
rem   probe_game_tweaks.exe --all, kategoriler 4..9):
rem   cl /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /DTENGRI_HAS_TWEAK_KEYS /I src /I build\obj tools\probe_game_tweaks.cpp src\core\regpack.cpp src\core\tweaks.cpp src\core\backup.cpp src\core\elevate.cpp src\core\ram.cpp src\core\network.cpp src\core\startup.cpp /link advapi32.lib shell32.lib iphlpapi.lib
rem
rem Siralama sabit: en hizlisi once, takilinca geri kalani gormek icin
rem beklenmez.
set "PKGS=test_pure test_modules test_license test_write_paths test_update test_disk"

set "SHORT_test_pure=pure"
set "LBL_test_pure=Test: saf mantik"
set "SRC_test_pure=tests\test_pure.cpp src\core\lang.cpp src\core\ram.cpp src\core\regpack.cpp src\core\tweaks.cpp src\core\elevate.cpp src\core\backup.cpp src\core\network.cpp"
set "LIB_test_pure=advapi32.lib shell32.lib iphlpapi.lib"
set "KEY_test_pure=1"
set "HINT_test_pure=test_pure derlenemedi ya da takildi; i18n tablo sirasi ve tweak sozlesmeleri bkz. docs\TEST-YUKSELTME.md"

set "SHORT_test_modules=modules"
set "LBL_test_modules=Test: yeni moduller"
set "SRC_test_modules=tests\test_modules.cpp src\core\restore.cpp src\core\startup.cpp src\core\services.cpp src\core\cleaner.cpp"
set "LIB_test_modules=advapi32.lib shell32.lib version.lib ole32.lib"
set "KEY_test_modules=0"
set "HINT_test_modules=test_modules derlenemedi ya da takildi (salt-okunur yuzey)"

set "SHORT_test_license=license"
set "LBL_test_license=Test: lisans ve kimlik"
set "SRC_test_license=tests\test_license.cpp src\core\license.cpp src\core\sysinfo.cpp src\core\lang.cpp"
set "LIB_test_license=advapi32.lib shell32.lib dxgi.lib ole32.lib"
set "KEY_test_license=0"
set "HINT_test_license=test_license derlenemedi ya da takildi (izole APPDATA)"

set "SHORT_test_write_paths=write"
set "LBL_test_write_paths=Test: guvenli yazma yollari"
set "SRC_test_write_paths=tests\test_write_paths.cpp src\core\cleaner.cpp src\core\backup.cpp"
set "LIB_test_write_paths=shell32.lib ole32.lib advapi32.lib user32.lib"
set "KEY_test_write_paths=1"
set "HINT_test_write_paths=test_write_paths derlenemedi ya da takildi (tersinirlik kapisi)"

set "SHORT_test_update=update"
set "LBL_test_update=Test: surum denetimi"
set "SRC_test_update=tests\test_update.cpp src\core\update.cpp"
set "LIB_test_update=advapi32.lib winhttp.lib bcrypt.lib"
set "KEY_test_update=0"
set "HINT_test_update=test_update derlenemedi ya da takildi (ag ve registry yok)"

set "SHORT_test_disk=disk"
set "LBL_test_disk=Test: disk analizi"
set "SRC_test_disk=tests\test_disk.cpp src\core\diskanalyzer.cpp"
set "LIB_test_disk=shell32.lib ole32.lib bcrypt.lib advapi32.lib user32.lib propsys.lib"
set "KEY_test_disk=0"
set "HINT_test_disk=test_disk derlenemedi ya da takildi (MFT ayristirma, onbellek v3)"

rem ==========================================================================
rem  2. ARGUMAN COZUMLEME VE ASAMA PLANI
rem ==========================================================================
set "MODE=full"
set "PKG="
set "WANT="
set "RC=0"

if "%~1"=="" goto :mode_ok
if /i "%~1"=="full"   goto :mode_ok
if /i "%~1"=="app"    set "MODE=app"   & goto :mode_ok
if /i "%~1"=="tests"  set "MODE=tests" & goto :mode_ok
if /i "%~1"=="test"   set "MODE=one"   & set "WANT=%~2" & goto :mode_ok
if /i "%~1"=="only"   set "MODE=one"   & set "WANT=%~2" & goto :mode_ok
if /i "%~1"=="clean"  goto :do_clean
if /i "%~1"=="help"   goto :usage
if /i "%~1"=="-h"     goto :usage
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="/?"     goto :usage
echo !T_X! bilinmeyen arguman: %~1
echo     build.bat help
exit /b 2

:mode_ok
if not "%MODE%"=="one" goto :plan
call :resolve "%WANT%"
if defined PKG goto :plan
echo !T_X! bilinmeyen test paketi: %WANT%
echo     secenekler: pure  modules  license  write  update  disk
echo     tam ad da kabul edilir: test_pure, test_write_paths, ...
echo     hepsi icin: build.bat tests
exit /b 2

:plan
rem Asama plani listelerden hesaplanir, elle sayi tutulmaz.
rem   app   : arac seti + 7 (alt modul, surum, tablo, marka, yazi tipi, rc, cl) = 8
rem   full  : 8 + paket sayisi                                                  = 14
rem   tests : arac seti + tablo (KEY gerekirse) + paket sayisi                  = 8
rem   one   : arac seti + tablo (KEY gerekirse) + 1 paket                       = 2-3
rem Betik sonunda muhasebe kosulan asamayi plana karsi dogrular; sessizce kuyruga
rem dusen ya da planda unutulan bir asama cikis kodunu 1 yapar.
set "RUNLIST="
if "%MODE%"=="full"  set "RUNLIST=%PKGS%"
if "%MODE%"=="tests" set "RUNLIST=%PKGS%"
if "%MODE%"=="one"   set "RUNLIST=%PKG%"

set /a NEEDKEYS=0, NPKG=0
if defined RUNLIST for %%N in (%RUNLIST%) do (
    set /a NPKG+=1
    if "!KEY_%%N!"=="1" set /a NEEDKEYS=1
)

set /a TOTAL=1
if "%MODE%"=="app"   set /a TOTAL+=7
if "%MODE%"=="full"  set /a TOTAL+=7+NPKG
if "%MODE%"=="tests" set /a TOTAL+=NEEDKEYS+NPKG
if "%MODE%"=="one"   set /a TOTAL+=NEEDKEYS+NPKG
set /a DONE=0, CUR=0, FIN=0

rem ==========================================================================
rem  3. KOSUM
rem ==========================================================================
call :tick
set "STARTALL=!TICK!"
call :dirs
echo.
call :header

call :stage "MSVC arac seti"
call :find_toolset || exit /b 1

if "%MODE%"=="app"   call :app_pipeline || exit /b 1
if "%MODE%"=="full"  call :app_pipeline || exit /b 1
if "%MODE%"=="tests" call :tests_prep   || exit /b 1
if "%MODE%"=="one"   call :tests_prep   || exit /b 1

rem app kipi RUNLIST'i bos birakir, hicbir paket kosmaz.
if defined RUNLIST for %%N in (%RUNLIST%) do call :package %%N || exit /b 1

call :commit
set "ACC=0"
if not "%DONE%"=="%TOTAL%" set "ACC=1"
if not "%FIN%"=="%TOTAL%" set "ACC=1"
if "%ACC%"=="1" (
    echo.
    echo  !T_X! asama muhasebesi tutmadi: baslayan %DONE%, biten %FIN%, planlanan %TOTAL%
    echo      build.bat icindeki plani ya da asama sayisini esitle.
    set "RC=1"
)
call :summary
exit /b %RC%

rem ==========================================================================
rem  Kipler
rem ==========================================================================
:app_pipeline
call :stage "Alt modul: Dear ImGui"
call :ensure_imgui || exit /b 1

call :stage "Uretici: surum basligi"
rem Tek kaynak src\brand.hpp, cikti build\obj\version.h. .rc dosyasi bunu include
rem eder, boylece VERSIONINFO kaynak koddan ayrilamaz.
call :ps make_version.ps1 "" "tools\make_version.ps1 basarisiz (src\brand.hpp surum sabitleri?)" || exit /b 1

call :gen_table || exit /b 1

call :stage "Uretici: marka varliklari"
rem make_icon_from_png.ps1 uretir: res\tengri.ico (pencere, gorev cubugu, bildirim)
rem make_logo_data.ps1 uretir:     src\gui\logo_data.cpp (arayuzde cizilen doku)
rem Artwork tek dosyada durur (res\tengri-logo.png); resim degisince iki cikti
rem elle yenilenmez.
call :ps make_icon_from_png.ps1 "" "tools\make_icon_from_png.ps1 basarisiz (res\tengri-logo.png?)" || exit /b 1
call :ps make_logo_data.ps1 "-WhiteArtwork" "tools\make_logo_data.ps1 basarisiz" || exit /b 1

call :stage "Uretici: gomulu yazi tipi"
rem make_font_data.ps1 res\fonts altindaki alt kumeleri src\gui\font_data.cpp
rem icine gomer. Alt kume uretimi (fontTools, Python) burada degil; derleme
rem yalnizca PowerShell ister. Bu adim birakilirsa bir onceki surumden kalan
rem font_data.cpp derlenir ve degisiklikler sessizce yoksayilir.
call :ps make_font_data.ps1 "" "tools\make_font_data.ps1 basarisiz (res\fonts alt kumeleri?)" || exit /b 1

call :stage "Kaynak: res\tengri.rc"
rem rc.exe ayri adim olmak zorunda: cl bir .rc argumanini kaynak dosya adi
rem sanir, nesne dosyasinin var oldugunu varsayar ve atlar; boylece ikonu ve
rem manifesti olmayan bir exe sessizce baglanir. .rc ikon, manifest (yetki,
rem DPI, isletim sistemi uyumlulugu) ve VERSIONINFO tasir.
rc /nologo /DTENGRI_HAS_VERSION_H /I build\obj /fo build\obj\tengri.res res\tengri.rc
call :check "rc.exe basarisiz: res\tengri.rc, build\obj\version.h ve res\tengri-logo.png yerinde mi?" || exit /b 1

call :stage "Derleme: TENGRI.exe"
call :build_app || exit /b 1
exit /b 0

:tests_prep
rem App tarafi kosmayan kiplerde tweak_keys.h yine de gerekir: test_pure ve
rem test_write_paths onu include eder. NEEDKEYS plan asamasinda hesaplandi.
if "%NEEDKEYS%"=="0" exit /b 0
call :gen_table || exit /b 1
exit /b 0

:gen_table
call :stage "Uretici: registry tablosu"
rem tweaks.cpp ve regpack.cpp degisince docs\tweaks-registry.md ve
rem build\obj\tweak_keys.h yenilenir. Eski kalmasi derlemeyi bozmaz ama
rem tutarsizlik gorunur olsun diye her kosuda uretilir.
call :ps make_tweak_table.ps1 "" "tools\make_tweak_table.ps1 basarisiz (src\core\tweaks.cpp, src\core\regpack.cpp okunamadi)" || exit /b 1
exit /b 0

rem ==========================================================================
rem  4. ASAMA MAKINESI
rem ==========================================================================
:stage
rem %1 = etiket. Onceki asamayi bitirir (sure, durum, tamam satiri), yenisini
rem baslatir ve calisan asamayi gosteren cubugu yazar. Betik ilk hatada durdugu
rem icin bir sonraki cubuk satiri, onceki asamanin gectiginin kanitidir.
if not "%CUR%"=="0" call :commit
set /a CUR+=1, DONE+=1
set "NAME_%CUR%=%~1"
set "ST_%CUR%= .. "
call :tick
set "TSTART=!TICK!"
echo.
call :paint run "%~1"
exit /b 0

:close_stage
rem Gecerli asamanin suresini E_n degiskenine ve FV'ye yazar.
call :tick
set /a D=TICK-TSTART
if %D% lss 0 set /a D+=8640000
call :fmt %D% FV
set "E_%CUR%=%FV%"
exit /b 0

:commit
rem Gecerli asama basarili bitti: sure, durum ok, FIN artar, tamam satiri yazilir.
call :close_stage
set "ST_%CUR%= ok "
set /a FIN+=1
call :paint ok "!NAME_%CUR%!  (%FV%)"
exit /b 0

:check
rem Hemen onceki komutun cikis kodunu devralir. Sifir degilse asamayi FAIL
rem isaretler, ozeti yazdirir ve 1 doner. Kullanim: call :check "ipucu" ardindan exit /b 1
set "ERR=%errorlevel%"
if "%ERR%"=="0" exit /b 0
set "RC=%ERR%"
call :fail "%~1"
exit /b 1

:fail
rem %1 = ipucu (bos olabilir). FAIL cubugu, hata satiri, asama tablosu, cikis 1.
rem Tablo hata yolunda da yazilir: hangi asamanin ne kadar kostugu, takilanin
rem hangisi oldugu gorunsun.
if "%RC%"=="0" set "RC=1"
call :close_stage
set "ST_%CUR%=FAIL"
call :paint FAIL "!NAME_%CUR%!  (%FV%)"
echo.
echo   !T_X! BASARISIZ: !NAME_%CUR%!   (cikis kodu %RC%, %FV%)
if not "%~1"=="" echo       %~1
call :summary
exit /b 1

rem --- ilerleme cubugu --------------------------------------------------------
rem Cubuk uc yerde gorunur, boylece uzun cl ya da test ciktisi onu gozden
rem cikarmaz:
rem   1. her asamanin basinda : calisan asama isaretli, etiket run
rem   2. her asamanin sonunda : tamam satiri, sure ile, etiket ok ya da FAIL
rem   3. konsol baslik cubugu : title komutu, kaydirmadan bagimsiz hep ekranda
rem      (NOTITLE=1 ile kapanir; CI'da yonlendirilmis konsolda zararsiz)
rem Dolu kisim biten asamalari (FIN), isaret calisani, nokta bekleyenleri
rem gosterir. Yuzde yalnizca biten asamalardan hesaplanir: 100 ancak son asama
rem bitince gorunur. Sure tahmini yok: asama sureleri birbirinden cok farkli.

:bar_build
rem Girdi: FIN, TOTAL, BARW, BARRUN (1 ise calisan asama isaretlenir).
rem Cikti: BARTXT (renkli cubuk), BARPLAIN (duz cubuk, title icin), CNT (N/T,
rem 5 genislik), P3 (yuzde, 3 genislik).
set /a PCT=0, FILL=0
if %TOTAL% gtr 0 set /a PCT=FIN*100/TOTAL, FILL=FIN*BARW/TOTAL
if %FILL% gtr %BARW% set /a FILL=BARW
if %PCT% gtr 100 set /a PCT=100
set /a EMPTY=BARW-FILL
set "MK="
if not "%BARRUN%"=="1" goto :bar_fill
if %EMPTY% leq 0 goto :bar_fill
set "MK=>"
set /a EMPTY-=1
:bar_fill
set "FB="
for /l %%X in (1,1,%FILL%) do set "FB=!FB!="
set "EB="
for /l %%X in (1,1,%EMPTY%) do set "EB=!EB! "
set "P3=   %PCT%"
set "P3=!P3:~-3!"
set "CNT=%FIN%/%TOTAL%     "
set "CNT=!CNT:~0,5!"
set "BARPLAIN=[!FB!!MK!!EB!]"
set "BARTXT=!C_DIM![!C_OK!!FB!!C_RUN!!MK!!C_DIM!!EB!]!C_OFF!"
exit /b 0

:paint
rem %1 = etiket (run, ok, FAIL), %2 = metin. Cubuk satirini yazar ve basligi
rem gunceller. Isaret ve renk kodlari degiskenin icinde: gecikmeli genisleme
rem yonlendirme ayrisimindan sonra calistigi icin echo ve title icinde guvenlidir.
rem Satir: etiket, cubuk, yuzde, N/T, metin. Etiketler: run, ok, xx (hata).
set "BARRUN=0"
if /i "%~1"=="run" set "BARRUN=1"
call :bar_build
set "TAGTXT=!C_OK!ok!C_OFF!"
set "TXTC="
if /i "%~1"=="run"  set "TAGTXT=!C_RUN!>>!C_OFF!" & set "TXTC=!C_HI!"
if /i "%~1"=="FAIL" set "TAGTXT=!C_ERR!xx!C_OFF!" & set "TXTC=!C_ERR!"
echo  !TAGTXT! !BARTXT! !P3!%%  !C_DIM!!CNT!!C_OFF!  !TXTC!%~2!C_OFF!
if not defined NOTITLE title TENGRI  !BARPLAIN!  !P3!%%  !CNT!  %~2
exit /b 0

:boxline
rem %1 = metin, %2 = renk degiskeninin ADI (C_OK gibi, bos olabilir). Cerceve
rem icine soldan bosluklu, saga dolgulu tek satir yazar.
set "BC="
if not "%~2"=="" set "BC=!%~2!"
set "BL=  %~1                                                                              "
echo  !C_DIM!^|!C_OFF!!BC!!BL:~0,70!!C_OFF!!C_DIM!^|!C_OFF!
exit /b 0

:header
if "%MODE%"=="full"  set "MDESC=uygulama + %NPKG% test paketi"
if "%MODE%"=="app"   set "MDESC=yalnizca build\TENGRI.exe"
if "%MODE%"=="tests" set "MDESC=%NPKG% test paketi, exe derlenmez"
if "%MODE%"=="one"   set "MDESC=tek paket: %PKG%"
echo  !C_DIM!!RULE!!C_OFF!
call :boxline "T E N G R I   -   D E R L E M E" C_HI
echo  !C_DIM!!RULE!!C_OFF!
call :boxline "kip      %MODE%  -  %MDESC%"
call :boxline "plan     %TOTAL% asama"
call :boxline "cikti    build\TENGRI.exe   build\test_*.exe"
call :boxline "yardim   build.bat help"
echo  !C_DIM!!RULE!!C_OFF!
exit /b 0

:summary
rem Asama tablosu, son cubuk ve sonuc kutusu. Test paketi rakamlari betigin
rem uydurdugu degil, exenin kendi son satiridir: not sutununa oldugu gibi
rem yazilir. Cubuk ve sonuc en sonda: ciktinin son satirlari hep durumu gosterir.
call :tick
set /a TT=TICK-STARTALL
if %TT% lss 0 set /a TT+=8640000
call :fmt %TT% TALLH
echo.
echo  !C_DIM!!RULE!!C_OFF!
call :boxline "S O N U C      kip: %MODE%" C_HI
echo  !C_DIM!!RULE!!C_OFF!
echo.
echo !C_DIM!   ##  asama                           durum     sure  not!C_OFF!
echo !C_DIM!   --  ------------------------------  ----- --------  ----------------------!C_OFF!
for /l %%I in (1,1,%DONE%) do call :row %%I
echo !C_DIM!   --  ------------------------------  ----- --------  ----------------------!C_OFF!
set "BARRUN=0"
call :bar_build
echo.
echo   !BARTXT!  !P3!%%  !C_DIM!!CNT!!C_OFF!
echo.
if "%RC%"=="0" (set "BTXT=BASARILI" & set "BCOL=C_OK") else (set "BTXT=BASARISIZ  -  yukaridaki FAIL satirina bak" & set "BCOL=C_ERR")
echo  !C_DIM!!RULE!!C_OFF!
call :boxline "!BTXT!" !BCOL!
call :boxline "%FIN%/%TOTAL% asama tamamlandi   toplam sure %TALLH%"
echo  !C_DIM!!RULE!!C_OFF!
if "%RC%"=="0" (set "FTITLE=TENGRI - BASARILI") else (set "FTITLE=TENGRI - BASARISIZ")
if not defined NOTITLE title !FTITLE!  !P3!%%
exit /b 0

:row
set /a IDX=%~1
set "PAD="
if %IDX% lss 10 set "PAD=0"
set "NM=!NAME_%IDX%!                              "
set "STT=!ST_%IDX%!"
set "TM=        !E_%IDX%!"
set "TM=!TM:~-8!"
set "SC="
if "!STT!"==" ok " set "SC=!C_OK!"
if "!STT!"=="FAIL" set "SC=!C_ERR!"
if "!STT!"==" .. " set "SC=!C_DIM!"
set "NOTE="
if defined NOTE_%IDX% set "NOTE=  !NOTE_%IDX%!"
echo   !C_DIM!!PAD!!IDX!!C_OFF!  !NM:~0,30!  !SC!!STT! !C_OFF!!TM!!C_DIM!!NOTE!!C_OFF!
exit /b 0

:colors
rem Renk yalnizca Windows 10 ve uzeri, etkilesimli ortamda acilir. Kapatmak
rem icin: NO_COLOR ya da TENGRI_PLAIN tanimla; CI, TF_BUILD ve GITHUB_ACTIONS
rem tanimliysa zaten kapali (log dosyasinda kacis dizisi gorunmesin). Kapaliyken
rem butun renk degiskenleri bos kalir, cikti duz ASCII olur.
set "C_OFF=" & set "C_DIM=" & set "C_OK=" & set "C_ERR=" & set "C_WARN=" & set "C_RUN=" & set "C_HI="
set "ESC="
if defined NO_COLOR goto :colors_tags
if defined TENGRI_PLAIN goto :colors_tags
if defined CI goto :colors_tags
if defined TF_BUILD goto :colors_tags
if defined GITHUB_ACTIONS goto :colors_tags
set "WINMAJ="
for /f "tokens=4 delims=. " %%i in ('ver') do set "WINMAJ=%%i"
if not "!WINMAJ!"=="10" goto :colors_tags
for /F "tokens=1,2 delims=#" %%a in ('"prompt #$H#$E# & echo on & for %%b in (1) do rem"') do set "ESC=%%b"
if not defined ESC goto :colors_tags
set "C_OFF=!ESC![0m"
set "C_DIM=!ESC![90m"
set "C_OK=!ESC![92m"
set "C_ERR=!ESC![91m"
set "C_WARN=!ESC![93m"
set "C_RUN=!ESC![96m"
set "C_HI=!ESC![97m"
:colors_tags
set "T_I=!C_RUN![i]!C_OFF!"
set "T_OK=!C_OK![+]!C_OFF!"
set "T_W=!C_WARN![?]!C_OFF!"
set "T_X=!C_ERR![x]!C_OFF!"
exit /b 0

rem ==========================================================================
rem  5. IS YAPAN ALT PROGRAMLAR
rem ==========================================================================
:usage
echo.
echo  build.bat [kip]
echo.
echo    (bos)        uygulama + alti test paketi   - CI bunu kullaniyor
echo    app          yalnizca build\TENGRI.exe
echo    tests        yalnizca alti test paketi
echo    test ^<ad^>    tek paket: pure  modules  license  write  update  disk
echo    clean        build\obj, build\tobj ve exe dosyalarini siler
echo    help         bu liste
echo.
echo  Her kip ilerleme cubugu ve asama tablosu basar; ilk takildigi asamada
echo  cikis kodu sifirdan farkli biter. Test paketi rakamlari exenin kendi
echo  son satirindan gelir, betik toplam uydurmaz.
echo.
exit /b 0

:do_clean
echo   !T_I! build klasoru temizleniyor
if exist build\obj  rmdir /s /q build\obj
if exist build\tobj rmdir /s /q build\tobj
if exist build\TENGRI.exe del /q build\TENGRI.exe
del /q build\test_*.exe 2>nul
echo   !T_OK! temiz (uretilen kaynaklar src altinda durur, silinmedi)
exit /b 0

:dirs
if not exist build      mkdir build
if not exist build\obj  mkdir build\obj
if not exist build\tobj mkdir build\tobj
exit /b 0

:resolve
rem %1 = kullanicinin yazdigi kisaltma ya da tam ad. PKG'ye cozer: pure,
rem test_pure, write, test_write_paths gibi hepsi kabul edilir.
set "PKG="
set "W=%~1"
if not defined W exit /b 0
for %%N in (%PKGS%) do (
    if /i "%%N"=="!W!" set "PKG=%%N"
    if /i "!SHORT_%%N!"=="!W!" set "PKG=%%N"
)
exit /b 0

:ps
rem %1 = betik adi (tools\ altinda), %2 = ek arguman (bos olabilir), %3 = ipucu.
if exist "tools\%~1" goto :ps_run
set "RC=1"
call :fail "tools\%~1 bulunamadi"
exit /b 1
:ps_run
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\%~1" %~2
call :check "%~3" || exit /b 1
exit /b 0

:need_files
rem %1 = bosluklu dosya listesi. Eksik olanlari tek tek yazar, hata verir.
set "MISS=0"
for %%F in (%~1) do if not exist "%%F" (
    echo   !T_X! eksik dosya: %%F
    set "MISS=1"
)
if "%MISS%"=="0" exit /b 0
set "RC=1"
call :fail "yukaridaki kaynak dosyalar yok: dosya adini ya da build.bat listesini kontrol et"
exit /b 1

:tick
rem %time% tek basina guvenilir degil: saat bosluklu hizalanir, ayrac olarak
rem iki nokta yaninda nokta ya da virgul gelebilir, bazi yerel ayarlarda
rem santisaniye yoktur. Ayraclari atip rakamlari birlestiriyoruz. Ikinci tuzak:
rem 08 gibi degerler set /a icinde SEKIZLI sayilir ve kirilir; basa 1 koyup
rem yuze gore mod almak bunu onler. Gece yarisi devrinde fark negatif olur,
rem cagiran 8640000 ekler. TICK santisaniye cinsindendir.
set "T=%time: =0%"
set "T=!T::=!"
set "T=!T:.=!"
set "T=!T:,=!"
set "T=!T:/=!"
set "T=!T:~0,8!"
set "CSX=!T:~6,2!"
set /a CS=0
if defined CSX set /a CS=(1!CSX!)%%100
set /a TICK=((1!T:~0,2!%%100)*60+(1!T:~2,2!%%100))*60*100+(1!T:~4,2!%%100)*100+CS
exit /b 0

:fmt
rem %1 = santisaniye, %2 = cikti degiskeni. Ornekler: 12.4s, 02m 41s, 1h 05m.
set /a IN=%~1
set /a SEC=IN/100, T10=(IN%%100)/10
set /a MIN=SEC/60, SEC=SEC%%60
set /a HOU=MIN/60, MIN=MIN%%60
set "S2=  %SEC%"
set "M2=  %MIN%"
set "S2=!S2:~-2!"
set "M2=!M2:~-2!"
set "FV="
if %HOU% gtr 0 set "FV=%HOU%h %M2%m"
if %HOU% equ 0 if %MIN% gtr 0 set "FV=%M2%m %S2%s"
if %HOU% equ 0 if %MIN% equ 0 set "FV=%SEC%.%T10%s"
set "%~2=%FV%"
exit /b 0

:human
rem %1 = dosya yolu. HOUT = "2.137.088 B  [2 MB, 13.10.2026 00:45]". Binlik
rem ayirici nokta (Turkce gruplama); ASCII oldugu icin kod sayfasinda sorunsuz.
set "HOUT="
if not exist "%~1" exit /b 0
for %%F in ("%~1") do set "SZF=%%~zF"
for %%F in ("%~1") do set "TSF=%%~tF"
set "N=%SZF%"
set "S="
:human_loop
if not defined N goto :human_done
if defined S (set "S=!N:~-3!.!S!") else (set "S=!N:~-3!")
set "N=!N:~0,-3!"
goto :human_loop
:human_done
set "HOUT=!S! B  [!TSF!]"
set /a MB=SZF/1048576
if %MB% geq 1 set "HOUT=!S! B  [!MB! MB, !TSF!]"
exit /b 0

:find_toolset
rem Zaten x64 gelistirici ortaminda (vcvars calismis) isek tekrar calistirmayiz.
where cl >nul 2>&1
if errorlevel 1 goto :toolset_locate
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" goto :toolset_locate
echo   !T_I! Arac seti     : hazir x64 ortam
goto :toolset_info

:toolset_locate
set "VSPATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
rem Once vswhere; Build Tools'ta -products * ile -requires birlikte bazen bos
rem doner ve calisabilecek bir derlemeyi bozardi. Dizin taramasi yedektir.
if exist "%VSWHERE%" call :vs_vswhere
if not defined VSPATH call :vs_scan
if defined VSPATH goto :toolset_call
set "RC=1"
call :fail "MSVC arac seti bulunamadi: Visual Studio'da Desktop development with C++ is yukunu kur (https://visualstudio.microsoft.com/downloads/)"
exit /b 1

:toolset_call
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 goto :toolset_bad
where cl >nul 2>&1
if errorlevel 1 goto :toolset_bad
echo   !T_I! Visual Studio : %VSPATH%
goto :toolset_info

:toolset_bad
set "RC=1"
call :fail "vcvars64.bat calisti ama cl yolda bulunamadi: kurulumda C++ derleyici eksik"
exit /b 1

:toolset_info
if defined VCToolsVersion echo   [i] MSVC arac seti: %VCToolsVersion%
exit /b 0

:vs_vswhere
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
exit /b 0

:vs_scan
rem Surum klasoru adina bagimli degil: Microsoft Visual Studio altindaki her
rem surum ve sku klasoru denenir, sonuncu bulunan kazanir.
for /d %%Y in ("%ProgramFiles%\Microsoft Visual Studio\*" "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do for /d %%D in ("%%~fY\*") do if exist "%%~fD\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%%~fD"
exit /b 0

:ensure_imgui
rem Git submodule olarak, .gitmodules icinde sabit bir commit'e bagli. Bu,
rem HEAD'in bugun hangi surumde oldugundan bagimsiz olmayi saglar: iki build
rem ayni kaynak, ayni kutuphaneyle derlenir.
if exist "third_party\imgui\imgui.h" goto :imgui_check
echo   !T_I! third_party\imgui bos, alt modul indiriliyor
git --version >nul 2>&1
if errorlevel 1 goto :imgui_nogit
git submodule update --init --recursive
call :check "git submodule update --init --recursive basarisiz" || exit /b 1
:imgui_check
if not exist "third_party\imgui\imgui.h" goto :imgui_missing
rem style.FontSizeBase ve FontScaleDpi icin 1.92 ya da uzeri gerekir. Sayisal
rem karsilastirma: sabit metin aramasinin aksine 1.92.97 gibi WIP basliklari
rem gecer, 1.91 gibi eskiler reddedilir.
set "IGV="
set "IGS="
for /f "tokens=3" %%V in ('findstr /b /c:"#define IMGUI_VERSION_NUM" third_party\imgui\imgui.h') do set "IGV=%%V"
for /f "tokens=3" %%V in ('findstr /b /c:"#define IMGUI_VERSION " third_party\imgui\imgui.h') do set "IGS=%%~V"
if not defined IGV goto :imgui_old
set /a IGVN=%IGV%
if %IGVN% geq 19200 goto :imgui_ok
:imgui_old
set "RC=1"
call :fail "third_party\imgui surumu 1.92 ya da uzeri olmali (IMGUI_VERSION_NUM okunamadi ya da eski)"
exit /b 1
:imgui_missing
set "RC=1"
call :fail "third_party\imgui icerigi alinamadi: git submodule update --init --recursive"
exit /b 1
:imgui_nogit
set "RC=1"
call :fail "git bulunamadi: Dear ImGui alt modulunu elle third_party\imgui altina koy"
exit /b 1
:imgui_ok
echo   !T_I! Dear ImGui    : !IGS!  (IMGUI_VERSION_NUM !IGV!)
exit /b 0

:check_running
rem Link adimi calisan exe'nin uzerine yazamaz (LNK1104). Kendimiz oldurmeyiz:
rem kullanicinin acik isi kaybolmasin. Yalnizca uyari; link yine de takilirsa
rem FAIL satiri ipucuyla birlikte gorunur.
tasklist /fi "imagename eq TENGRI.exe" /nh 2>nul | findstr /i /c:"TENGRI.exe" >nul || exit /b 0
echo   !T_W! build\TENGRI.exe su anda calisiyor, link adimi yazamayabilir.
echo       Kapatmak icin: powershell -NoProfile -ExecutionPolicy Bypass -File tools\_close_tengri.ps1
exit /b 0

:build_app
rem /MP cekirdek sayisinda paralel bilesim. .res baglantisi rc adimindan gelir.
call :need_files "%SOURCES% build\obj\tengri.res" || exit /b 1
call :check_running
cl %CLAPP% %CLINC% ^
   %SOURCES% ^
   /Fobuild\obj\ /Febuild\TENGRI.exe ^
   %CLINK% %CLLIB%
call :check "cl basarisiz: /W4 uyarisi hata sayilmaz, yukaridaki ilk C2xxx ya da LNKxxxx satirina bak" || exit /b 1
call :human "build\TENGRI.exe"
if not defined HOUT goto :build_app_missing
echo   !T_OK! build\TENGRI.exe   !HOUT!
set "NOTE_%CUR%=!HOUT!"
exit /b 0
:build_app_missing
set "RC=1"
call :fail "cl cikis kodu sifir ama build\TENGRI.exe yok: /Febuild yazimi ya da disk dolulugu olabilir"
exit /b 1

:package
rem %1 = paket kimligi. Tablodan okur, derler, kosturur, son satiri nota alir.
set "PID=%~1"
set "PLABEL=!LBL_%PID%!"
set "PSRC=!SRC_%PID%!"
set "PLIB=!LIB_%PID%!"
set "PKEY=!KEY_%PID%!"
set "PHINT=!HINT_%PID%!"
set "PFLAGS="
if "!PKEY!"=="1" set "PFLAGS=/DTENGRI_HAS_TWEAK_KEYS /I build\obj"
call :stage "!PLABEL!"
call :need_files "!PSRC!" || exit /b 1
cl %CLTEST% !PFLAGS! ^
   !PSRC! ^
   /Fobuild\tobj\ /Febuild\!PID!.exe ^
   /link !PLIB!
call :check "!PHINT!" || exit /b 1
rem Test ciktisi once loga yazilir, sonra oldugu gibi ekrana dokulur: boylece
rem cikis kodu yonlendirmeyle kaybolmaz ve tabloya exenin kendi ozet satiri girer.
set "PLOG=build\tobj\!PID!.log"
build\!PID!.exe > "!PLOG!" 2>&1
set "RCX=!errorlevel!"
call :stage_log "!PLOG!" "!PHINT!"
exit /b %errorlevel%

:stage_log
rem %1 = log dosyasi, %2 = hata ipucu. RCX paketin cikis kodudur ve type
rem calismadan once okundu. Son dolu satir tabloya NOTE olarak gider; unlem
rem iceren satirlar bozulmasin diye okuma sirasinda gecikmeli genisleme kapali.
type "%~1"
setlocal DisableDelayedExpansion
set "PLAST="
for /f "usebackq delims=" %%L in ("%~1") do set "PLAST=%%L"
endlocal & set "NOTE_%CUR%=%PLAST%"
if "%RCX%"=="0" exit /b 0
set "RC=%RCX%"
call :fail "%~2"
exit /b 1
