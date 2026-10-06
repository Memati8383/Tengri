@echo off
rem GitHub'a yayinlama yardimcisi.
rem
rem Onceden yapilmis isler: git init, submodule sabitleme, ilk commit, remote ekleme.
rem Burada kalan tek adim depoyu GitHub'da olusturmak; o bir tarayici tiklamasi.
rem
rem Kullanim:
rem   1) https://github.com/new adresinden "Tengri" adiyla bos bir repo olustur
rem      (README / .gitignore / lisans seceneklerinin HICBIRINI isaretleme)
rem   2) bu betigi calistir
rem
rem Ilk push'ta Git Credential Manager bir tarayici penceresi acar; GitHub'da
rem oturum acman istenir. O onayi bir kez verdiginde sonraki push'lar otomatik gider.

setlocal
cd /d "%~dp0"

echo [*] Submodule kontrolu...
git submodule update --init --recursive || exit /b 1

echo [*] Degistirilmis dosya var mi?
git status --short

echo.
echo [*] Push: origin/main
git push -u origin main
if errorlevel 1 (
    echo.
    echo [!] Push basarisiz.
    echo     Depoyu olusturduysan: onceki hatayi oku.
    echo     Depo henuz yoksa: https://github.com/new adresinden olustur ve tekrar calistir.
    exit /b 1
)

echo.
echo [+] Yayinlandi:  https://github.com/Memati8383/Tengri
endlocal