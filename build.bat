@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem --- C++ arac setini bul ----------------------------------------------------
rem Once vswhere denenir, ama yalnizca Build Tools kurulu bir makinede sonuc bos
rem doner (-products * ile -requires birlikte saglanan bir kurulum yoktur) ve bu,
rem sorunsuz calisabilecek bir derlemeyi bozardi. Asagidaki dizin taramasi yedektir.
set "VSPATH="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)

if not defined VSPATH (
    for /d %%D in ("%ProgramFiles(x86)%\Microsoft Visual Studio\2022\*" "%ProgramFiles%\Microsoft Visual Studio\2022\*") do (
        if exist "%%~fD\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%%~fD"
    )
)

if not defined VSPATH (
    echo [!] No MSVC toolset found. Install "Desktop development with C++" from
    echo     https://visualstudio.microsoft.com/downloads/
    exit /b 1
)

echo [*] Toolset: %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo [!] vcvars64 failed & exit /b 1)

rem --- arayuz kutuphanesini hazirla -------------------------------------------
rem Git submodule olarak, klonlanmis depoda .gitmodules icinde sabit bir commit'e
rem baglanmis durumda. Bu, "HEAD"in bugun hangi surumde olduguna bagli olmayi ortadan
rem kaldiriyor: iki build ayni kaynak ayni kutuphaneyle derlenir.
if not exist "third_party\imgui\imgui.h" (
    echo [*] Initialising submodule ^(third_party\imgui^)...
    git submodule update --init --recursive || exit /b 1
)
if not exist "third_party\imgui\imgui.h" (
    echo [!] third_party\imgui icerigi alinamadi.
    echo     Depoyu submodule ile klonladiysan:  git submodule update --init --recursive
    exit /b 1
)
rem style.FontSizeBase / FontScaleDpi icin 1.92 veya uzeri gerekiyor.
findstr /C:"#define IMGUI_VERSION_NUM   192" third_party\imgui\imgui.h >nul || (
    findstr /C:"#define IMGUI_VERSION_NUM   193" third_party\imgui\imgui.h >nul || (
        echo [!] third_party\imgui 1.92+ olmali ^(farkli bir surum bulundu^).
        exit /b 1
    )
)

if not exist build mkdir build
if not exist build\obj mkdir build\obj

set IMGUI=third_party\imgui
set SOURCES=src\main.cpp src\app.cpp src\gui\theme.cpp src\gui\fx.cpp src\gui\icons.cpp src\gui\brand_icons.cpp src\gui\logo.cpp src\gui\logo_data.cpp src\gui\font_data.cpp src\gui\widgets.cpp ^
 src\core\license.cpp src\core\sysinfo.cpp src\core\sysinfo_detail.cpp src\core\cleaner.cpp src\core\tweaks.cpp src\core\network.cpp src\core\lang.cpp src\core\ram.cpp src\core\regpack.cpp src\core\elevate.cpp src\core\backup.cpp src\core\restore.cpp src\core\startup.cpp src\core\services.cpp src\core\notify.cpp src\core\sysinfo_wmi.cpp src\core\update.cpp src\tray.cpp ^
 %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp ^
 %IMGUI%\backends\imgui_impl_win32.cpp %IMGUI%\backends\imgui_impl_dx11.cpp

rem Surum basligini uret: tek kaynak src\brand.hpp, cikti build\obj\version.h.
rem .rc dosyasi bunu include eder, boylece VERSIONINFO kaynak koddan ayrilamaz.
echo [*] Generating version header ...
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_version.ps1 || exit /b 1

rem Registry tablosunu yenile: tweaks.cpp ve regpack.cpp degistiyse docs\tweaks-registry.md
rem eski kalmasin. Derleme ciktisi degil, yanlisi hali derlemeyi bozmaz; yine de
rem tutarsizlik gorunur olsun.
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_tweak_table.ps1 || exit /b 1

rem Marka varliklarini yeniden uret:
rem   - make_icon_from_png.ps1 -> res\tengri.ico (pencere, gorev cubugu, bildirim)
rem   - make_logo_data.ps1     -> src\gui\logo_data.cpp (arayuzde cizilen doku)
rem Artwork tek dosyada durur (res\tengri-logo.png); resim degistiginde iki cikti
rem da elle yenilenmez.
echo [*] Generating brand assets ...
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon_from_png.ps1 || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_logo_data.ps1 -WhiteArtwork || exit /b 1

rem Yazi tipini res\fonts altindaki alt kumelerden gom:
rem   - make_font_data.ps1 -> src\gui\font_data.cpp
rem Alt kume uretimi (fontTools, Python) burada degil; sadece gomme isini yapar,
rem boylece derleme yalnizca PowerShell ister. Onemli: bu adim birakilsa bir
onceki surumden kalan font_data.cpp derlenir ve degisiklikler sessizce yoksayilir.
echo [*] Embedding fonts ...
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_font_data.ps1 || exit /b 1

rem res\tengri.rc ikonu, uygulama manifestini (yetki + DPI + isletim sistemi uyumlulugu)
rem ve VERSIONINFO blogunu tasiyor. rc.exe ayri bir adim olarak calismak zorunda: cl bir
rem .rc argumanini kaynak dosya adi sanir, nesne dosyasinin var oldugunu varsayar ve
rem atlar; boylece ikonu ve manifesti olmayan bir exe sessizce baglanir.
if not exist build\obj mkdir build\obj
rc /nologo /DTENGRI_HAS_VERSION_H /I build\obj /fo build\obj\tengri.res res\tengri.rc
if errorlevel 1 (
    echo [!] Resource derlemesi basarisiz.
    exit /b 1
)

echo [*] Building TENGRI.exe ...

:: _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS: C++/WinRT (src\core\notify.cpp)
:: <experimental/coroutine> basligini cekiyor. Yeni MSVC surumlerinde bu kullanim
:: STL1011 ile HATA olarak reddediliyor; release isi bu yuzden kurulumda kirildi.
:: Uygulama C++20 <coroutine> kullanmiyor, dolayisiyla bu yalnizca bir gecislik
:: uyarisidir ve susturulmasinda kayip yok. Test derlemesi WinRT kullanmadigi
:: icin bayraagi almaz.
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 /MP /D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS ^
   /DNDEBUG /DUNICODE /D_UNICODE /DIMGUI_DEFINE_MATH_OPERATORS ^
   /DTENGRI_HAS_TWEAK_KEYS ^
   /I src /I build\obj /I %IMGUI% /I %IMGUI%\backends ^
   %SOURCES% ^
   /Fobuild\obj\ /Febuild\TENGRI.exe ^
   /link build\obj\tengri.res /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib user32.lib gdi32.lib advapi32.lib shell32.lib iphlpapi.lib comctl32.lib version.lib windowscodecs.lib ole32.lib windowsapp.lib propsys.lib wbemuuid.lib winhttp.lib bcrypt.lib
if errorlevel 1 (
    echo [!] Build failed.
    exit /b 1
)
echo [+] Done: build\TENGRI.exe

rem --- testler ----------------------------------------------------------------
rem Registry'ye dokunmayan saf mantik testleri. D3D11 ve ImGui baglanmaz; bu yuzden
rem arayuz kutuphanesi ve ekran gerektirmez, saniyeler icinde kosar.
rem
rem Test kaynaklari registry YAZAN kodu icerir (tweaks.cpp, regpack.cpp) ama testler
rem o yollari cagirmaz; yalnizca govde metinlerini okur. advapi32 yine de baglanir.
rem
rem NOT: tools\probe_game_tweaks.cpp ayri bir gidis-donus sondasidir ve BILINCLI
rem olarak bu dosyanin parcasi degildir. Gercek sistem registry'sine yazar (HKLM),
rem yonetici yetkisi ister ve test_pure.exe'in "registry yazmaz" sozunu ihlal eder.
rem Elle derlemek icin:
rem   cl /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /DTENGRI_HAS_TWEAK_KEYS ^
rem      /I src /I build\obj tools\probe_game_tweaks.cpp src\core\regpack.cpp ^
rem      src\core\tweaks.cpp src\core\backup.cpp src\core\elevate.cpp src\core\ram.cpp ^
rem      src\core\network.cpp src\core\startup.cpp /link advapi32.lib shell32.lib iphlpapi.lib
rem Sonra:  probe_game_tweaks.exe --all      (yonetici olarak; 4..9)
if not exist build\tobj mkdir build\tobj
echo [*] Building tests ...
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 ^
   /DUNICODE /D_UNICODE /I src ^
   tests\test_pure.cpp src\core\lang.cpp src\core\ram.cpp src\core\regpack.cpp src\core\tweaks.cpp src\core\elevate.cpp src\core\backup.cpp src\core\network.cpp ^
   /Fobuild\tobj\ /Febuild\test_pure.exe ^
   /DTENGRI_HAS_TWEAK_KEYS /I build\obj ^
   /link advapi32.lib shell32.lib iphlpapi.lib
if errorlevel 1 (
    echo [!] Test derlemesi basarisiz.
    exit /b 1
)

echo [*] Running tests ...
build\test_pure.exe
if errorlevel 1 (
    echo [!] Testler basarisiz.
    exit /b 1
)
echo [+] Tests passed

rem --- yeni modul testleri ----------------------------------------------------
rem restore / startup / services / shader temizleyicisinin salt-okunur yuzeyi ve
rem guvenlik kapisilari. Bu test de registry YAZMAZ, dosya SILMEZ, hizmet
rem durumunu DEGISTIRMEZ: yalnizca okuma ve beyaz-liste-disi reddi denenir. Yetki
rem gerektirmez, yukseltilmemis surecte de ayni sonucu verir.
echo [*] Building module tests ...
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 ^
   /DUNICODE /D_UNICODE /I src ^
   tests\test_modules.cpp src\core\restore.cpp src\core\startup.cpp src\core\services.cpp src\core\cleaner.cpp ^
   /Fobuild\tobj\ /Febuild\test_modules.exe ^
   /link advapi32.lib shell32.lib version.lib ole32.lib
if errorlevel 1 (
    echo [!] Modul testi derlemesi basarisiz.
    exit /b 1
)

echo [*] Running module tests ...
build\test_modules.exe
if errorlevel 1 (
    echo [!] Modul testleri basarisiz.
    exit /b 1
)
echo [+] Module tests passed

rem --- lisans / donanim kimligi testleri --------------------------------------
rem Mask (anahtar maskeleme), sys::Hwid (deterministik ozet) ve "beni hatirla"
rem kaliciligi. Validate() BILINCLI olarak test disidir: gecici bir demo stub'i
rem sabitlemek anlamsiz olurdu. Kalicilik testi APPDATA'yi _putenv_s ile gecici bir
rem klasore yonlendirir; SetEnvironmentVariableA CRT environ'ini guncellemedigi icin
rem YANLISTIR ve gercek %APPDATA%\TENGRI\license.dat'i silerdi. Test bu yonlendirme-
rem yi dogruluyor, dolayisiyla gercek kullanici verisine dokunmaz.
echo [*] Building license tests ...
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 ^
   /DUNICODE /D_UNICODE /I src ^
   tests\test_license.cpp src\core\license.cpp src\core\sysinfo.cpp src\core\lang.cpp ^
   /Fobuild\tobj\ /Febuild\test_license.exe ^
   /link advapi32.lib shell32.lib dxgi.lib ole32.lib
if errorlevel 1 (
    echo [!] Lisans testi derlemesi basarisiz.
    exit /b 1
)

echo [*] Running license tests ...
build\test_license.exe
if errorlevel 1 (
    echo [!] Lisans testleri basarisiz.
    exit /b 1
)
echo [+] License tests passed

rem --- guvenli yazma-yolu testleri --------------------------------------------
rem cleaner (shader + TEMP) ve backup'in GERCEK kodunu dosya/varin-etkiyle calistirir
rem ama ortam degiskenlerini (TEMP/LOCALAPPDATA/WINDIR/ProgramData/ProgramFiles(x86))
rem _putenv_s ile izole bir gecici agaca yonlendirerek. Boylece gercek onbellekler
rem ya da Windows klasoru ASLA silinmez; "once yaz sonra sil -> tersinir" garantisi
rem dogrulanir. backup yalnizca reg.exe EXPORT (salt-okuma) yapar; Restore/import ve
rem Geri Donusum Kutusu (kategori 3) HIC cagrilmaz. tweak_keys.h (build\obj) gerekir;
rem onceki adimlarda uretilir.
echo [*] Building write-path tests ...
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 ^
   /DUNICODE /D_UNICODE /DTENGRI_HAS_TWEAK_KEYS /I src /I build\obj ^
   tests\test_write_paths.cpp src\core\cleaner.cpp src\core\backup.cpp ^
   /Fobuild\tobj\ /Febuild\test_write_paths.exe ^
   /link shell32.lib ole32.lib advapi32.lib
if errorlevel 1 (
    echo [!] Yazma-yolu testi derlemesi basarisiz.
    exit /b 1
)

echo [*] Running write-path tests ...
build\test_write_paths.exe
if errorlevel 1 (
    echo [!] Yazma-yolu testleri basarisiz.
    exit /b 1
)
echo [+] Write-path tests passed

rem --- surum denetimi testleri ------------------------------------------------
rem Guncellemenin dogrulama kismi: surum siralamasi, latest.json ayristirmasi,
rem sunucu allowlist'i, SHA-256 cekirdegi, takas plani ve 24 saat hesabı.
rem
rem AG YOK: CheckNow/StageNow hic cagrilmaz, dolayisiyla test hiçbir sunucuya
rem istek atmaz (loopback dahil). REGISTRY YOK: LoadPrefs/SavePrefs/MarkChecked
rem cagrilmaz, HKCU\Software\TENGRI\Update anahtari testte hiç açılmaz. Apply
rem yalnizca RED yoluyla denenir (indirme yokken hazirlik kontrolünde döner);
rem kabul yolu kendi exe'sinin adini degiştirirdi. Tek dosya işi kendi yarattığı
rem geçici dosyadır ve sonunda silinir.
echo [*] Building update tests ...
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 ^
   /DUNICODE /D_UNICODE /I src ^
   tests\test_update.cpp src\core\update.cpp ^
   /Fobuild\tobj\ /Febuild\test_update.exe ^
   /link advapi32.lib winhttp.lib bcrypt.lib
if errorlevel 1 (
    echo [!] Guncelleme testi derlemesi basarisiz.
    exit /b 1
)

echo [*] Running update tests ...
build\test_update.exe
if errorlevel 1 (
    echo [!] Guncelleme testleri basarisiz.
    exit /b 1
)
echo [+] Update tests passed
