@echo off
rem GitHub'a yayinlama.
rem
rem Bu depo icin daha once yapilmis olanlar: git init, submodule'in sabit bir commit'e
rem baglanmasi, ilk commit, origin remote'u. Buradaki tek is, GitHub'da bos bir depo
rem olusturmak ve ilk push'u yapmak.
rem
rem gh kuruluysa depo burada otomatik olusur. Kurulu degilse hata verir ve asagida
rem elle yol gosterilir.
rem
rem Onceden yapilmis bir yayin varsa sadece "git push" yeterlidir.

setlocal
cd /d "%~dp0"

echo [*] Submodule kontrolu...
git submodule update --init --recursive || exit /b 1

set "REPO=Memati8383/Tengri"
set "DESC=Windows sistem optimize edici - C++ / DirectX 11 / monokrom arayuz"

where gh >nul 2>&1
if errorlevel 1 (
    echo [!] GitHub CLI ^(gh^) bulunamadi.
    echo.
    echo     Kurmak icin:   winget install --id GitHub.cli --exact
    echo     Kurduktan sonra bu betigi tekrar calistir.
    echo.
    echo     Ya da depoyu elle olustur:
    echo       https://github.com/new  --^>  ad: Tengri
    echo       README / .gitignore / lisans seceneklerinin HICBIRINI isaretleme
    echo     sonra bu betigi tekrar calistir.
    exit /b 1
)

echo [*] GitHub oturumu kontrol ediliyor...
gh auth status >nul 2>&1
if errorlevel 1 (
    echo [!] gh kurulu ama GitHub'a girilmemis.
    echo     Simdi oturum acmak icin ayri bir pencere ac ve su komutu calistir:
    echo.
    echo         gh auth login --web --git-protocol https
    echo.
    echo     Tarayicida acilan pencerede onayi ver, sonra bu betigi tekrar calistir.
    exit /b 1
)

echo [*] Uzak depo ekleniyor...
git remote remove origin 2>nul
git remote add origin https://github.com/%REPO%.git

echo [*] Depo olusturuluyor / guncelleniyor: %REPO%
gh repo view %REPO% >nul 2>&1
if errorlevel 1 (
    gh repo create %REPO% --public --description "%DESC%" --source . --remote origin --push
) else (
    echo     Depo zaten var, yalnizca push ediliyor.
    git push -u origin main
)

if errorlevel 1 (
    echo.
    echo [!] Push basarisiz. Yukaridaki mesaji oku.
    exit /b 1
)

echo.
echo [+] Yayinlandi:  https://github.com/%REPO%
endlocal