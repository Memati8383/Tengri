// license modülünün testleri.
//
// Amaç: anahtar maskeleme (Mask), donanim kimligi uretimi (sys::Hwid) ve
// "beni hatirla" kaliciligi (Save/LoadSaved/ForgetSaved) icin gercek, kalic
// mantigi dogrulamak.
//
// Yan-etki sozlesmesi (test_pure ile ayni):
//   - Save/LoadSaved/ForgetSaved yalnizca YONLENDIRILMIS bir APPDATA altindaki
//     gecici klasorde denenir; kullaniciyi gercek %APPDATA%\TENGRI dosyasina
//     dokunmaz.
//   - Activate/Poll cagrilmaz: Validate() bilincli olarak GECICI bir demo
//     govdesidir (1.4 sn uyku + "yayina cikarmadan once degistir" notu). Onu
//     test etmek, degistirilecek bir stub'i sabitlemek olurdu.
//
// Hwid() salt-okunurdur: GetVolumeInformationA + bilgisayar adi + FNV-1a. Lisans
// kirma amaci tasimaz; yalnizca ozetin deterministik ve bicime uygun oldugunu
// dogrular.

#include "../src/core/license.hpp"
#include "../src/core/sysinfo.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <windows.h>

static int g_fail = 0;
static int g_pass = 0;

static void Check(bool ok, const char* what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s\n", what); }
}

static void CheckEq(long long got, long long want, const char* what)
{
    if (got == want) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s (beklenen %lld, gelen %lld)\n", what, want, got); }
}

static bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }

// --------------------------------------------------------------------------- Mask

static void Test_Mask()
{
    std::printf("anahtar maskeleme\n");

    // 19 karakterlik ("XXXX-XXXX-XXXX-XXXX") anahtar maskelenir; uzunlugu 19
    // olmayan girdi AYNEN doner (bug: once sinir kontrolu olmadan substr cagirmak
    // kisa girdilerde istisna firlatirdi).
    CheckEq((long long)license::Mask("").size(), 0, "bos girdi aynen doner");
    Check(license::Mask("ABC") == "ABC", "kisa girdi aynen doner");
    Check(license::Mask("too-short-key") == "too-short-key", "19 olmayan giristi aynen");
    Check(license::Mask("12345678901234567890") == "12345678901234567890",
         "20 karakterlik girdi aynen doner");

    // Gecerli bicimli anahtar.
    const std::string key = "ABCD-EFGH-IJKL-MNOP";        // 19 karakter
    const std::string m   = license::Mask(key);

    // Birinci grup gorunur kalir, son grup ise yalnizca ilk 3 karakteriyle gorunur.
    Check(m.rfind("ABCD", 0) == 0, "ilk grup korunuyor");
    Check(m.find("EFGH") == std::string::npos, "ikinci grup maskelendi");
    Check(m.find("IJKL") == std::string::npos, "ucuncu grup maskelendi");
    Check(m.find("MNO") != std::string::npos, "son gruptan 3 karakter korunuyor");

    // Orta gruplar tirenler haric tamamen bulgu (U+2022) ile dolu olmali;
    // yani anahtarin tamami asla geri sizdirilmemeli.
    Check(m.find('P') == std::string::npos, "son karakterin tamamli sizmiyor");

    // Uzunluk byte bazinda olculur: maskelenen karakterler U+2022 (3 byte) olarak
    // kodlanir. 4 (ilk grup) + '-' + 4 bulgu*3 + '-' + 4 bulgu*3 + '-' + 3 (son)
    // => 4+1+12+1+12+1+3 = 34 byte.
    CheckEq((long long)m.size(), 34, "maske uzunlugu 34 byte (8 x U+2022 = 24 byte)");

    // Ayiraci tirenler yerinde: bulgular cok-byte oldugu icin indeksler kayar.
    // 0-3 "ABCD", 4 '-', 5-16 bulgu, 17 '-', 18-29 bulgu, 30 '-', 31-33 "MNO".
    Check(m[4] == '-' && m[17] == '-' && m[30] == '-', "ayiraci tirenler byte 4/17/30");
    Check(m.rfind("-MNO", 30) == 30, "kuyruk '-MNO' ile bitiyor");
}

// --------------------------------------------------------------------------- HWID

static void Test_Hwid()
{
    std::printf("donanim kimligi (salt-okunur)\n");

    const std::string a = sys::Hwid();
    const std::string b = sys::Hwid();

    // Deterministik: ayni surec icinde iki cagri esit olmali (o nebette cache).
    Check(a == b, "Hwid deterministik (cache)");

    // Bicim: XXXX-XXXX-XXXX-XXXX (4 x onaltilik grup, 19 karakter).
    CheckEq((long long)a.size(), 19, "Hwid uzunlugu 19");
    bool bicimTam = a.size() == 19;
    for (int g = 0; g < 4; ++g)
    {
        const size_t base = (size_t)g * 5;
        if (g > 0 && a[base - 1] != L'-') bicimTam = false;
        for (int i = 0; i < 4; ++i)
            if (!IsHex(a[base + i])) bicimTam = false;
    }
    Check(bicimTam, "Hwid bicimi XXXX-XXXX-XXXX-XXXX");

    // Hwid 19 karakter oldugu icin Mask onu maskelemeli; boylece UI'da tam kimlik
    // gorunmez. Mask'un gercek bir girdi uzerinde calistigini dogrular.
    const std::string mh = license::Mask(a);
    Check(mh != a, "Hwid maskelenince degisiyor");
    Check(mh.rfind(a.substr(0, 4), 0) == 0, "maskelenmis Hwid ilk grubu korur");
}

// --------------------------------------------------------------------------- kalicilik

// APPDATA gecici bir klasore yonlendirilir; Save/LoadSaved/ForgetSaved yalnizca
// oraya yazar, kullanicinin gercek lisans dosyasina dokunmaz.
//
// Kritik: StoragePath() APPDATA'yi _dupenv_s ile okur. _dupenv_s CRT'nin kendi
// ortam anlik goruntusunu (environ) kullanir; Win32 SetEnvironmentVariableA yalnizca
// proses ortam bloğunu degistirir ve bu anlik goruntuyu GUNCELLEMEZ. Bu yuzeden
// SetEnvironmentVariableA ile yonlendirme bosuna calisir ve test gercek
// %APPDATA%\TENGRI\license.dat uzerinde kosardi. _putenv_s ise hem CRT environ'i
// hem proses ortamini gunceller; izolasyon icin tek dogru cagri budur.
static void Test_Persistence()
{
    std::printf("beni hatirla kaliciligi (izole APPDATA)\n");

    char tmp[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmp))
    {
        Check(false, "GetTempPathA basarisiz");
        return;
    }

    // Benzersiz gecici kok.
    std::string root = tmp;
    root += "tengri_lic_test_";
    root += std::to_string(GetCurrentProcessId());
    CreateDirectoryA(root.c_str(), nullptr);

    // Eski APPDATA degerini sakla, test boyunca degistir, sonunda geri yukle.
    char oldAppdataBuf[MAX_PATH] = {};
    DWORD oldLen = GetEnvironmentVariableA("APPDATA", oldAppdataBuf, MAX_PATH);
    std::string oldAppdata = (oldLen > 0 && oldLen < MAX_PATH) ? std::string(oldAppdataBuf, oldLen) : std::string();

    if (_putenv_s("APPDATA", root.c_str()) != 0)
    {
        Check(false, "_putenv_s APPDATA ayarlayamadi");
        RemoveDirectoryA(root.c_str());
        return;
    }

    // Dogrulama: gercekten yonlendirildi mi? Gercek dosyaya yazmayi onlemek icin
    // bu kontrol sart -- yanilirsa test sessizce kullanici verisine dokunur.
    // _dupenv_s, StoragePath()'in kullandigi ayni CRT environ goruntusunu okur.
    {
        char* verify = nullptr;
        size_t vlen = 0;
        if (_dupenv_s(&verify, &vlen, "APPDATA") == 0 && verify)
        {
            Check(root == verify, "APPDATA gercekten gecici kokte (CRT okudu)");
            free(verify);
        }
        else
            Check(false, "APPDATA geri okunamadi");
    }

    const std::string key = "SAVE-LOAD-ROUNDTRIP";

    // Bos baslangic: yeni klasorde dosya yok.
    Check(license::LoadSaved().empty(), "once bos (dosya yok)");

    license::Save(key);
    Check(license::LoadSaved() == key, "Save -> LoadSaved ayni dondu");

    // Uzerine yazma (trunc): eski icerik kalmamali.
    const std::string key2 = "SECOND-VALUE";
    license::Save(key2);
    Check(license::LoadSaved() == key2, "Save tekrar -> uzerine yazdi");

    license::ForgetSaved();
    Check(license::LoadSaved().empty(), "ForgetSaved -> bos");

    // Geri yukle (_putenv_s ile, environ'i de duzeltmek icin).
    if (!oldAppdata.empty()) _putenv_s("APPDATA", oldAppdata.c_str());
    else                     _putenv_s("APPDATA", "");

    // Gecici klasoru temizle (license.dat + TENGRI alti + kok).
    std::string file = root + "\\TENGRI\\license.dat";
    DeleteFileA(file.c_str());
    RemoveDirectoryA((root + "\\TENGRI").c_str());
    RemoveDirectoryA(root.c_str());
}

int main()
{
    std::printf("TENGRI lisans modulunun testleri\n\n");

    Test_Mask();
    Test_Hwid();
    Test_Persistence();

    std::printf("\n%d gecti, %d kaldi\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
