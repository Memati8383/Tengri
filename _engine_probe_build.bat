@echo off
setlocal
cd /d "%~dp0"
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    for /d %%D in ("%ProgramFiles(x86)%\Microsoft Visual Studio\2022\*" "%ProgramFiles%\Microsoft Visual Studio\2022\*") do (
        if exist "%%~fD\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%%~fD"
    )
)
if not defined VSPATH (echo [!] no toolset & exit /b 1)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist build\obj mkdir build\obj
cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src ^
   tools\probe_mft_engine.cpp src\core\diskanalyzer.cpp ^
   /Fobuild\obj\ /Febuild\probe_mft_engine.exe ^
   /link shell32.lib ole32.lib bcrypt.lib advapi32.lib user32.lib propsys.lib
exit /b %ERRORLEVEL%