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
set SOURCES=src\main.cpp src\app.cpp src\gui\theme.cpp src\gui\fx.cpp src\gui\icons.cpp src\gui\brand_icons.cpp src\gui\logo.cpp src\gui\widgets.cpp ^
 src\core\license.cpp src\core\sysinfo.cpp src\core\sysinfo_detail.cpp src\core\cleaner.cpp src\core\tweaks.cpp src\core\network.cpp src\core\lang.cpp src\core\ram.cpp src\core\regpack.cpp src\core\elevate.cpp src\core\backup.cpp src\tray.cpp ^
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
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 /MP ^
   /DNDEBUG /DUNICODE /D_UNICODE /DIMGUI_DEFINE_MATH_OPERATORS ^
   /DTENGRI_HAS_TWEAK_KEYS ^
   /I src /I build\obj /I %IMGUI% /I %IMGUI%\backends ^
   %SOURCES% ^
   /Fobuild\obj\ /Febuild\TENGRI.exe ^
   /link build\obj\tengri.res /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib user32.lib gdi32.lib advapi32.lib shell32.lib iphlpapi.lib comctl32.lib version.lib windowscodecs.lib ole32.lib
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
