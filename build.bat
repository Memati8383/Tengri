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
set SOURCES=src\main.cpp src\app.cpp src\gui\theme.cpp src\gui\fx.cpp src\gui\icons.cpp src\gui\brand_icons.cpp src\gui\widgets.cpp ^
 src\core\license.cpp src\core\sysinfo.cpp src\core\sysinfo_detail.cpp src\core\cleaner.cpp src\core\tweaks.cpp src\core\network.cpp src\core\lang.cpp src\core\ram.cpp src\core\regpack.cpp src\tray.cpp ^
 %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp ^
 %IMGUI%\backends\imgui_impl_win32.cpp %IMGUI%\backends\imgui_impl_dx11.cpp

rem res\tengri.rc ikonu, uygulama manifestini (yetki + DPI + isletim sistemi uyumlulugu)
rem ve VERSIONINFO blogunu tasiyor. rc.exe ayri bir adim olarak calismak zorunda: cl bir
rem .rc argumanini kaynak dosya adi sanir, nesne dosyasinin var oldugunu varsayar ve
rem atlar; boylece ikonu ve manifesti olmayan bir exe sessizce baglanir.
echo [*] Compiling resources ...
if not exist build\obj mkdir build\obj
rc /nologo /fo build\obj\tengri.res res\tengri.rc
if errorlevel 1 (
    echo [!] Resource compilation failed.
    exit /b 1
)

echo [*] Building TENGRI.exe ...
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 /MP ^
   /DNDEBUG /DUNICODE /D_UNICODE /DIMGUI_DEFINE_MATH_OPERATORS ^
   /I src /I %IMGUI% /I %IMGUI%\backends ^
   %SOURCES% ^
   /Fobuild\obj\ /Febuild\TENGRI.exe ^
   /link build\obj\tengri.res /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib user32.lib gdi32.lib advapi32.lib shell32.lib iphlpapi.lib comctl32.lib version.lib
if errorlevel 1 (
    echo [!] Build failed.
    exit /b 1
)
echo [+] Done: build\TENGRI.exe
