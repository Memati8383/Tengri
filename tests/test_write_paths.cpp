// Guvenli yazma-yolu testleri.
//
// Amac: dosya temizleme (cleaner) ve yedek klasoru (backup) mantiginin GERCEK
// kodunu, gercek dosya/varin yan-etkileriyle ama YALNIZCA IZOLE bir ortamda
// sınamak. Bu, "once yaz sonra geri al -> bastaki durum" tersinirlik ozelligini
// kanitlar; salt-okunur testlerin (test_pure / test_modules / test_license)
// dokunamadigi yazi islevlerini kapsar.
//
// Izolasyon teknigi: cleaner ve backup tum yollarini environment degiskenlerinden
// cozuyor (TEMP, LOCALAPPDATA, WINDIR, ProgramData, ProgramFiles(x86)). _putenv_s
// hem CRT environ'ini hem proses ortam bloğunu guncelledigi icin GetEnvironment-
// VariableW bunlari gorur. Test bu bes degiskeni gecici bir agaca yonlendirir,
// boylece hiyerarsinin DISINA (gercek kullanicinin onbellekleri / Windows
// klasoru) hicbir sey silinmez ya da yazilmaz.
//
// Asla cagrilmayan yollar:
//   - backup::Restore() yalnizca .reg icermeyen klasorler ya da tabloya karsilik
//     gelmeyen adlar uzerinde denenir. Restore icine aktarimi reg.exe IMPORT ile
//     yazar; bu gercek registry'ye dokunur. Bu test calastirisi tweak_keys.h ILE
//     derleniyor (build.bat /DTENGRI_HAS_TWEAK_KEYS veriyor), yani tablodaki bir
//     (kategori,sira) ciftine uyan ad + gecen icerik = gercek import olurdu.
//     Testler o yuzden ya hicbir cifti olmayan adlar kullaniyor (99-99.reg gibi)
//     ya da icerik denetimini bilerek dusen dosyalar; import yolu hic calismaz.
//   - Icerik denetiminin kendisi (regtext.hpp) saf islevlerde, dosya uzerinde
//     kosuluyor: registry'ye hic yaklasmiyor, bu yuzden ici rahatca buyutulabilir.
//   - cleaner kategori 3 (Geri Donusum Kutusu) cagrilmaz: SHEmptyRecycleBinW gercek
//     kutuyu bosaltir. Test yalnizca kategori 0 (TEMP) ve shader onbellegini kullanir.
//   - ExportForCategories gercek anahtarlari disa aktarir: reg.exe EXPORT yalnizca
//     OKUR, registry'de hicbir sey degistirmez; yazdigi tek yer izole LOCALAPPDATA
//     altindaki gecici klasor ve test sonunda DeleteTree ile silinir.

#include "../src/core/cleaner.hpp"
#include "../src/core/backup.hpp"
#include "../src/core/regtext.hpp"

// build.bat bu derlemeyi /DTENGRI_HAS_TWEAK_KEYS /I build\obj ile insa ediyor:
// test, gercek uretilmis tabloyu gorur. Ayni tablo backup.cpp'nin de okudugu,
// dolayisiyla "gercek disa aktarim kapiyi gecer mi" denetimi gercek kosullarda
// yapilabiliyor.
#ifdef TENGRI_HAS_TWEAK_KEYS
#include "tweak_keys.h"
#endif

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

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

// ------------------------------------------------------------------ yardimcilar

static bool StartsWith(const std::wstring& s, const std::wstring& pre)
{
    if (s.size() < pre.size()) return false;
    for (size_t i = 0; i < pre.size(); ++i)
    {
        wchar_t a = s[i], b = pre[i];
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (a != b) return false;
    }
    return true;
}

static void MakeDirs(const std::wstring& path)
{
    // Basit kademeli CreateDirectoryW: ara bilesenleri teker teker olusturur.
    std::wstring acc;
    size_t i = 0;
    while (i < path.size())
    {
        size_t next = path.find_first_of(L'\\', i + 1);
        if (next == std::wstring::npos) next = path.size();
        acc = path.substr(0, next);
        CreateDirectoryW(acc.c_str(), nullptr);
        i = next;
    }
}

// byteSize kadar icerik yazan bir dosya olusturur. true: basarili.
static bool MakeFile(const std::wstring& path, size_t byteSize)
{
    MakeDirs(path.substr(0, path.find_last_of(L'\\')));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<char> chunk(64 * 1024, 'x');
    size_t written = 0;
    while (written < byteSize)
    {
        size_t n = byteSize - written;
        if (n > chunk.size()) n = chunk.size();
        DWORD wr = 0;
        const BOOL ok = WriteFile(h, chunk.data(), (DWORD)n, &wr, nullptr);
        if (!ok || wr != n) { CloseHandle(h); return false; }
        written += wr;
    }
    CloseHandle(h);
    return true;
}

static bool PathExists(const std::wstring& p)
{
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring EnvW(const wchar_t* name)
{
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
}

// Genis-karakter -> dar-karakter. Tum yonlendirilen yollar ASCII gecici
// klasorde, bu yuzden karakter basina kisaltma guvenli. _putenv_s dar ad ister.
static std::string Narrow(const std::wstring& w)
{
    std::string s; s.reserve(w.size());
    for (wchar_t c : w) s.push_back((char)(c & 0xFF));
    return s;
}
static std::string Narrow(const wchar_t* w) { return Narrow(std::wstring(w)); }

static std::wstring g_root;      // gecici agac koku
static std::wstring g_local;     // root\local
static std::wstring g_win;       // root\win
static std::wstring g_progdata;  // root\programdata
static std::wstring g_pf86;      // root\pf86

// Eski ortam degerlerini geri yuklemek icin saklama yuvasi.
struct SavedEnv { std::wstring val; bool had; };
static SavedEnv g_old[5];

static void SaveEnv(int slot, const wchar_t* nameW)
{
    g_old[slot].val = EnvW(nameW);
    g_old[slot].had = !g_old[slot].val.empty();
}
static void RestoreEnv(int slot, const wchar_t* nameW)
{
    const std::string name = Narrow(nameW);
    if (g_old[slot].had) _putenv_s(name.c_str(), Narrow(g_old[slot].val).c_str());
    else                 _putenv_s(name.c_str(), "");
}

// Agaci kur + ortamdegiskenlerini yonlendir. Basarisizsa false.
static bool EnterIsolation()
{
    wchar_t tmp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tmp)) return false;
    g_root = std::wstring(tmp) + L"tengri_wp_" + std::to_wstring(GetCurrentProcessId());

    // Eski agactan kalan bir sey varsa kismen temizle (idempotent kosum).
    // (Tam rekursif silme yerine, testin kendisi olusturdugu dosyalari sileriz;
    //  CreateDirectoryW mevcut klasorlerde de basari dondurur.)
    g_local    = g_root + L"\\local";
    g_win      = g_root + L"\\win";
    g_progdata = g_root + L"\\programdata";
    g_pf86     = g_root + L"\\pf86";

    CreateDirectoryW(g_root.c_str(), nullptr);
    CreateDirectoryW(g_local.c_str(), nullptr);
    CreateDirectoryW(g_win.c_str(), nullptr);
    CreateDirectoryW(g_progdata.c_str(), nullptr);
    CreateDirectoryW(g_pf86.c_str(), nullptr);

    // Once kaydet, sonra yonlendir.
    SaveEnv(0, L"TEMP");        SaveEnv(1, L"LOCALAPPDATA"); SaveEnv(2, L"WINDIR");
    SaveEnv(3, L"ProgramData"); SaveEnv(4, L"ProgramFiles(x86)");

    // _putenv_s genis-karakterli degiskenler icin ANSI istiyor; yollarimiz ASCII
    // gecici klasorde, bu yuzden dar-karakter cevirisi guvenli.
    auto toNarrow = [](const std::wstring& w) {
        std::string s; s.reserve(w.size());
        for (wchar_t c : w) s.push_back((char)(c & 0xFF));
        return s;
    };
    // TEMP'i g_root\temp olarak ayarla; cleaner kategori 0 icin.
    std::wstring wtemp = g_root + L"\\temp";
    CreateDirectoryW(wtemp.c_str(), nullptr);
    // WINDIR\Temp alt dalini da olustur; kategori 0 onu da temizliyor.
    CreateDirectoryW((g_win + L"\\Temp").c_str(), nullptr);

    _putenv_s("TEMP",             toNarrow(wtemp).c_str());
    _putenv_s("LOCALAPPDATA",     toNarrow(g_local).c_str());
    _putenv_s("WINDIR",           toNarrow(g_win).c_str());
    _putenv_s("ProgramData",      toNarrow(g_progdata).c_str());
    _putenv_s("ProgramFiles(x86)", toNarrow(g_pf86).c_str());

    // Dogrulama: yonlendirme gercekten gorunur oldu mu? (GetEnvironmentVariableW
    // proses ortam bloğunu okur; _putenv_s onu gunceller.)
    return EnvW(L"LOCALAPPDATA") == g_local && EnvW(L"TEMP") == wtemp
        && EnvW(L"WINDIR") == g_win;
}

static void DeleteTree(const std::wstring& path)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do {
            if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
                continue;
            std::wstring full = path + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(full);
            else { SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(full.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(path.c_str());
}

static void ExitIsolation()
{
    RestoreEnv(0, L"TEMP"); RestoreEnv(1, L"LOCALAPPDATA"); RestoreEnv(2, L"WINDIR");
    RestoreEnv(3, L"ProgramData"); RestoreEnv(4, L"ProgramFiles(x86)");
    if (!g_root.empty()) DeleteTree(g_root);
}

// ------------------------------------------------------------------ shader testi

// Onbellek dosyalari kurar, tarar, temizler ve tersinirligi dogrular.
static void Test_Shader_RoundTrip()
{
    std::printf("shader onbellegi: tarama + temizleme tersinirligi\n");

    const size_t k1 = 512 * 1024; // 0.5 MB -- MB'ye yuvarlaninca >0 kalir
    // Bilinen bir demet: D3DSCache 2 dosya, NVIDIA DXCache 1, NVIDIA GLCache 0,
    // Intel ShaderCache 1 dosya. AMD yok. Toplam Shader_Default = 4.
    bool made = true;
    made &= MakeFile(g_local + L"\\D3DSCache\\a.shader", k1);
    made &= MakeFile(g_local + L"\\D3DSCache\\sub\\b.shader", k1);
    made &= MakeFile(g_local + L"\\NVIDIA\\DXCache\\nv.dx", k1);
    made &= MakeFile(g_local + L"\\Intel\\ShaderCache\\ic.vk", k1);
    Check(made, "sahte shader dosyalari olusturuldu");

    // Alt-maskeler: D3D yalnizca D3DSCache'i (2 dosya) saymali.
    cleaner::ScanResult d3d = cleaner::ScanShader(cleaner::Shader_D3D);
    CheckEq(d3d.fileCount, 2, "Shader_D3D = 2 dosya");
    cleaner::ScanResult nvi = cleaner::ScanShader(cleaner::Shader_Intel);
    CheckEq(nvi.fileCount, 1, "Shader_Intel = 1 dosya");
    cleaner::ScanResult all = cleaner::ScanShader(cleaner::Shader_Default);
    CheckEq(all.fileCount, 4, "Shader_Default = 4 dosya");
    Check(all.sizeMB > 1.9 && all.sizeMB < 2.1, "Shader_Default boyutu ~2 MB");

    // Steam varsayilan maskede DEGIL: engine cache bilincli olarak haric.
    made = true;
    made &= MakeFile(g_pf86 + L"\\Steam\\steamapps\\shadercache\\99\\s.vkc", k1);
    Check(made, "sahte steam shadercache olusturuldu");
    cleaner::ScanResult def2 = cleaner::ScanShader(cleaner::Shader_Default);
    CheckEq(def2.fileCount, 4, "varsayilan masa Steam'i SAYMAZ (4'te kalir)");
    cleaner::ScanResult steam = cleaner::ScanShader(cleaner::Shader_SteamEngine);
    CheckEq(steam.fileCount, 1, "Shader_SteamEngine = 1 dosya");

    // Temizle (tum maske, Steam dahil) -> gercekten silinmis olmali (tersine).
    double freed = cleaner::CleanShader(cleaner::Shader_All);
    Check(freed > 2.4, "CleanShader(All) ~2.5 MB serbest birakti");
    CheckEq(cleaner::ScanShader(cleaner::Shader_All).fileCount, 0, "temizlik sonrasi 0 dosya (tersinir)");
    Check(!PathExists(g_local + L"\\D3DSCache\\a.shader"), "D3D dosyasi silindi");
    Check(!PathExists(g_pf86 + L"\\Steam\\steamapps\\shadercache\\99\\s.vkc"), "Steam dosyasi silindi");
}

// ------------------------------------------------------------------ TEMP temizleyici

static void Test_Temp_RoundTrip()
{
    std::printf("TEMP temizleyicisi: tarama + temizleme tersinirligi\n");

    std::wstring temp = EnvW(L"TEMP");
    Check(!temp.empty() && StartsWith(temp, g_root), "TEMP yonlendirilmis koku altinda");

    Check(MakeFile(temp + L"\\junk1.tmp", 256 * 1024), "TEMP junk1 yazildi");
    Check(MakeFile(temp + L"\\nest\\junk2.tmp", 256 * 1024), "TEMP junk2 (ic klasorde) yazildi");

    cleaner::ScanResult s0 = cleaner::Scan(0);
    CheckEq(s0.fileCount, 2, "Scan(0) iki sahte dosyayi gordu");

    double freed = cleaner::Clean(0);
    Check(freed > 0.4, "Clean(0) ~0.5 MB serbest birakti");
    Check(!PathExists(temp + L"\\junk1.tmp"), "junk1 silindi");
    Check(!PathExists(temp + L"\\nest\\junk2.tmp"), "junk2 silindi");
    CheckEq(cleaner::Scan(0).fileCount, 0, "Clean(0) sonrasi 0 dosya (tersinir)");
}

// ------------------------------------------------------------------ backup

static void Test_Backup_Paths()
{
    std::printf("yedek: kok dizini izolasyonu ve listeleme/en-yeni siralama\n");

    // RootDirectory() LOCALAPPDATA'ye dayanmali ve izole kokun altinda kalmali --
    // boylece yedekler gercek %LOCALAPPDATA%\TENGRI\backups'e yazmaz.
    const std::wstring root = backup::RootDirectory();
    Check(!root.empty(), "RootDirectory bos degil");
    Check(StartsWith(root, g_local), "RootDirectory yonlendirilmis LOCALAPPDATA altinda");

    // Sahte, zaman damgali klasorler kur; List() ve Latest() bunlari gorur.
    const std::wstring dOld = root + L"\\20200101-000000";
    const std::wstring dMid = root + L"\\20230615-120000";
    const std::wstring dNew = root + L"\\20251231-235959";
    CreateDirectoryW(dOld.c_str(), nullptr);
    CreateDirectoryW(dMid.c_str(), nullptr);
    CreateDirectoryW(dNew.c_str(), nullptr);

    std::vector<std::wstring> l = backup::List();
    // List() koku da saymaz ama icine yazabildigimiz her sey bir klasordur.
    Check(l.size() >= 3, "List() en az uc sahte yedegi dondurdu");

    // Latest(): ada gore en buyuk (alfabetik = kronolojik) secilmeli.
    Check(backup::Latest() == dNew, "Latest() en yeni zamandamgasi dondurdu");

    // ExportForCategories gercek tweak anahtarlarini disa aktarir. Bu YALNIZCA
    // OKUMA islemidir (reg.exe export); registry'yi DEGISTIRMEZ. Disa aktarimin
    // yazdigi tek sey izole LOCALAPPDATA altindaki gecici yedek klasorudur ve test sonunda
    // DeleteTree ile silinir. Makineden makineye gecerli anahtar kumesi degisebilir
    // (bir ayar henuz uygulanmamis olabilir), bu yuzden sonuc kosullu dogrulanir:
    //   true  -> yeni yedek klasoru izole kokte + icinde en az bir .reg + Latest onu gorur
    //   false -> bos klasor birakilmamali (temizleme dali)
    const int cats[] = { 0, 1, 2, 3, 4, 5, 6 };
    const size_t before = backup::List().size();
    std::wstring outDir;
    const bool exp = backup::ExportForCategories(cats, 6, &outDir);
    // Sessiz "hicbir sey yapmadim" regresyonu bu denetimle yakalanir: HKCU\Control
    // Panel\Mouse gibi her makinede var olan bir anahtar listede dururken disa
    // aktarimin false donmesi, yedek almanin calismadigi demek (bir donem anahtar
    // yollarindaki ayraç çizgisi kayboluyor ve reg.exe anahtari bulamiyordu).
    Check(exp, "ExportForCategories en az bir anahti disa aktardi");
    if (exp)
    {
        Check(!outDir.empty(), "ExportForCategories outDir doldurdu");
        Check(StartsWith(outDir, root), "yedek klasoru izole kokun altinda");
        Check(PathExists(outDir), "yedek klasoru fiilen olusturuldu");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((outDir + L"\\*.reg").c_str(), &fd);
        int regFiles = 0;
        if (h != INVALID_HANDLE_VALUE) { do { ++regFiles; } while (FindNextFileW(h, &fd)); FindClose(h); }
        Check(regFiles >= 1, "yedekte en az bir .reg dosyasi var");
        // Disa aktarim yalnizca okudu: geri alma (reg.exe import) hic cagrilmadi.
        Check(backup::Latest() == outDir, "yeni yedek Latest() oldu (zaman damgasi en taze)");

#ifdef TENGRI_HAS_TWEAK_KEYS
        // Uretilen HER yedek dosyasi denetim kapindan gecmek zorunda. Bu, gercek
        // reg.exe ciktilari uzerinde kosan tek denetim: alt anahtar satirlari
        // (bazi anahtarlarin disa aktarimi 14000+ satir alt anahtar tasir) ya da
        // ayni ayarin birden fazla anahtari kapiya takilirsa geri alma sessizce
        // hicbir sey yapmaz ve kimse fark etmez. Import YAPILMIYOR: dosyalar yalnizca
        // okunup denetleniyor, registry'ye hic dokunulmuyor.
        auto expandKey = [](const wchar_t* abbrev) {
            std::wstring p(abbrev);
            // Dort harfli kisaltma degistirilir, ayraç çizgisi oldugu gibi kalir.
            if (p.rfind(L"HKCU\\", 0) == 0)       p.replace(0, 4, L"HKEY_CURRENT_USER");
            else if (p.rfind(L"HKLM\\", 0) == 0)  p.replace(0, 4, L"HKEY_LOCAL_MACHINE");
            return p;
        };
        int gateRejected = 0, gateChecked = 0;
        WIN32_FIND_DATAW gf;
        HANDLE gh = FindFirstFileW((outDir + L"\\*.reg").c_str(), &gf);
        if (gh != INVALID_HANDLE_VALUE)
        {
            do
            {
                ++gateChecked;
                int cat = -1, idx = -1;
                if (!regtext::ParseBackupName(gf.cFileName, &cat, &idx)) { ++gateRejected; continue; }
                std::vector<std::wstring> keys;
                for (int i = 0; i < tweakkeys::kCount; ++i)
                    if (tweakkeys::kEntries[i].cat == cat && tweakkeys::kEntries[i].idx == idx)
                        keys.push_back(expandKey(tweakkeys::kEntries[i].path));
                if (keys.empty()) { ++gateRejected; continue; }   // uretilen ad tabloda yok
                std::wstring text;
                if (!regtext::ReadRegText(outDir + L"\\" + gf.cFileName, &text)) { ++gateRejected; continue; }
                if (!regtext::ContentMatchesKeys(text, keys)) ++gateRejected;
            } while (FindNextFileW(gh, &gf));
            FindClose(gh);
        }
        CheckEq(gateChecked, regFiles, "yedekteki her dosya denetlendi");
        CheckEq(gateRejected, 0, "uretilen hicbir yedek icerik kapisina takilmadi");
        // Gecen dosya sayisi kalici olarak yazdiriliyor: "kapi doğru ama gerçek
        // ciktiya hic uygulanmadi" ile "gercek cikti kapidan gecmiyor" ayrimi
        // ancak ölçüyle secilebiliyor.
        std::printf("  (%d gercek yedek dosyasi icerik kapisindan gecti)\n",
                    gateChecked - gateRejected);
#endif
    }
    else
    {
        Check(backup::List().size() == before, "ExportForCategories false -> bos klasor birakmadi");
    }

    // Restore() koruma dallari: yok-klasor ve .reg-icermeyen-klasor -> false.
    // HICBIR .reg import edilmez (gercek registry yazmayacagi icin guvenli). NOT:
    // yukaridaki export .reg uretmis olabilir ama Restore CAGIRILMAZ, dolayisiyla
    // import yolu hic calismaz.
    Check(backup::Restore(L"") == false, "Restore(bos) false");
    Check(backup::Restore(root + L"\\yoktur-klasor") == false, "Restore(yok) false");
    const std::wstring emptyDir = root + L"\\20990101-000000";
    CreateDirectoryW(emptyDir.c_str(), nullptr); // icinde .reg yok
    Check(backup::Restore(emptyDir) == false, "Restore(.reg yok) false, import calismadi");
    RemoveDirectoryW(emptyDir.c_str());
}

// ------------------------------------------------------------------ ReadRegText

// .reg dosyasini baytlariyla yazan yardimcilar: kodlama dali ayrica sinanmali,
// cunku denetim icerigi bellege okumadan once kodlamayi BOM'dan seciyor.
static bool MakeRawFile(const std::wstring& path, const void* data, size_t n)
{
    MakeDirs(path.substr(0, path.find_last_of(L'\\')));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    if (n > 0)
    {
        DWORD wr = 0;
        ok = WriteFile(h, data, (DWORD)n, &wr, nullptr) != FALSE && wr == n;
    }
    CloseHandle(h);
    return ok;
}

// reg.exe disa aktariminin gercek bicimi: UTF-16LE + FF FE BOM.
static bool MakeUtf16File(const std::wstring& path, const std::wstring& body)
{
    std::vector<unsigned char> b;
    b.push_back(0xFF); b.push_back(0xFE);
    for (wchar_t c : body)
    {
        b.push_back((unsigned char)(c & 0xFF));
        b.push_back((unsigned char)((c >> 8) & 0xFF));
    }
    return MakeRawFile(path, b.data(), b.size());
}

static bool MakeUtf8File(const std::wstring& path, const std::string& body)
{
    return MakeRawFile(path, body.data(), body.size());
}

static void Test_RegText_Reading()
{
    std::printf("yedek: kodlama cozumu ve icerik kapisi (dosya uzerinde)\n");

    const std::wstring dir = g_root + L"\\regtext";
    const wchar_t* H = L"Windows Registry Editor Version 5.00";
    const std::wstring keyLine = L"[HKEY_CURRENT_USER\\Software\\TENGRI\\Probe]\r\n";
    const std::vector<std::wstring> keys = { L"HKEY_CURRENT_USER\\Software\\TENGRI\\Probe" };

    // 1) UTF-16LE + BOM: disa aktarimin standart bicimi.
    const std::wstring u16 = dir + L"\\u16.reg";
    Check(MakeUtf16File(u16, std::wstring(H) + L"\r\n\r\n" + keyLine), "UTF-16LE dosya yazildi");
    std::wstring text;
    Check(regtext::ReadRegText(u16, &text), "UTF-16LE + BOM cozuldu");
    Check(regtext::ContentMatchesKeys(text, keys), "UTF-16LE yedek kapiyi gecti");

    // 2) UTF-8 + BOM ve BOM'suz UTF-8: her ikisi de taniniyor.
    const std::string ascii = "Windows Registry Editor Version 5.00\r\n"
                              "[HKEY_CURRENT_USER\\Software\\TENGRI\\Probe]\r\n";
    const std::wstring u8b = dir + L"\\u8bom.reg";
    const std::wstring u8  = dir + L"\\u8.reg";
    const std::string bom3 = "\xEF\xBB\xBF" + ascii;
    Check(MakeUtf8File(u8b, bom3) && MakeUtf8File(u8, ascii), "UTF-8 dosyalar yazildi");
    Check(regtext::ReadRegText(u8b, &text) && regtext::ContentMatchesKeys(text, keys),
          "UTF-8 + BOM kapiyi gecti");
    Check(regtext::ReadRegText(u8, &text) && regtext::ContentMatchesKeys(text, keys),
          "BOM'suz UTF-8 kapiyi gecti");

    // 3) BOM'suz ve gecersiz kodlamali veri reddedilmeli (MB_ERR_INVALID_CHARS).
    const std::wstring bad = dir + L"\\bad.reg";
    const unsigned char invalid[] = { 'W', 'i', 'n', 'd', 0xE9, 0xFF, 0xFE, 0x00 };
    Check(MakeRawFile(bad, invalid, sizeof(invalid)), "gecersiz kodlamali dosya yazildi");
    Check(!regtext::ReadRegText(bad, &text), "gecersiz UTF-8 reddedildi");

    // 4) BOM'suz UTF-16: karakterler arasi NUL'lar yuzunden kapi dusmeli. Bu,
    //    "kodlamayi varsayip icerik okuma" tuzaiginin testi.
    const std::wstring u16nb = dir + L"\\u16nobom.reg";
    std::vector<unsigned char> raw;
    const std::wstring src = std::wstring(H) + L"\r\n" + keyLine;
    for (wchar_t c : src)
    {
        raw.push_back((unsigned char)(c & 0xFF));
        raw.push_back((unsigned char)((c >> 8) & 0xFF));
    }
    Check(MakeRawFile(u16nb, raw.data(), raw.size()), "BOM'suz UTF-16 yazildi");
    bool passed = false;
    if (regtext::ReadRegText(u16nb, &text)) passed = regtext::ContentMatchesKeys(text, keys);
    Check(!passed, "BOM'suz UTF-16 icerik kapisindan gecemez");

    // 5) Bos dosya, olmayan yol ve klasor: ucunde de false.
    const std::wstring empty = dir + L"\\empty.reg";
    Check(MakeRawFile(empty, nullptr, 0), "bos dosya yazildi");
    Check(!regtext::ReadRegText(empty, &text), "bos dosya reddedildi");
    Check(!regtext::ReadRegText(dir + L"\\yok.reg", &text), "olmayan dosya reddedildi");
    Check(!regtext::ReadRegText(dir, &text), "klasor dosya degil -> reddedildi");

    // 6) Bisl disi bir anahtar, icerik kapisi tarafindan reddedilmeli.
    const std::wstring hostil = dir + L"\\hostil.reg";
    Check(MakeUtf16File(hostil, std::wstring(H) + L"\r\n"
          L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\notepad.exe]\r\n"
          L"\"Debugger\"=L\"\\\\yabanci\\\\araci.exe\"\r\n"),
          "IFEO icerikli sahte yedek yazildi");
    Check(regtext::ReadRegText(hostil, &text), "sahte yedek okundu");
    Check(!regtext::ContentMatchesKeys(text, keys), "IFEO icerigi kapiyi gecemez");

    DeleteTree(dir);
}

// Restore'un ret dallari: bu dosyalarin HICBIRI ice aktarilmaz. Guvenlik iki
// katmanli: adlar tablodaki hicbir (kategori,sira) ciftine karsilik gelmiyor
// (kategori 99 uretilemez; dort sayi grubu kabul edilmez; sayisal olmayan ad
// reddedilir) ve icerik denetimi de ayrica duser. Test derlemesi tweak_keys.h
// ILE yapilsa bile reg.exe import yoluna girilmez.
static void Test_Restore_Rejections()
{
    std::printf("yedek: geri alma ret dallari (import yoluna hic girilmez)\n");

    const std::wstring root = backup::RootDirectory();
    Check(!root.empty(), "Restore icin yedek koku cozuldu");

    const wchar_t* H = L"Windows Registry Editor Version 5.00";
    const std::wstring smuggle = std::wstring(H) + L"\r\n"
        L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run]\r\n"
        L"\"Deneme\"=\"\\\\yabanci\\\\araci.exe\"\r\n";

    // a) Kokun ALTINDA olmayan bir klasor: InsideRoot reddediyor.
    const std::wstring foreign = g_root + L"\\yarici\\klasor";
    Check(MakeUtf16File(foreign + L"\\99-99.reg", smuggle), "kok disi klasora .reg yazildi");
    Check(!backup::Restore(foreign), "Restore(kok disi yol) false, import yok");

    // b) Kokun kendisi: yaprak bilesen yok -> false.
    Check(!backup::Restore(root), "Restore(kokun kendisi) false");

    // c) Zaman damgali klasor, tabloya karsilik gelmeyen adlar.
    const std::wstring d = root + L"\\20240202-020202";
    CreateDirectoryW(d.c_str(), nullptr);
    Check(MakeUtf16File(d + L"\\99-99.reg", smuggle), "tablo-disi (99-99) yazildi");
    Check(MakeUtf16File(d + L"\\3-1-2-9.reg", smuggle), "dort gruplu ad yazildi");
    Check(MakeUtf16File(d + L"\\acikad.reg", smuggle), "sayisal olmayan ad yazildi");
    Check(!backup::Restore(d), "Restore(tablo-disi adlar) false, import yok");

    // d) Ice gomulu klasor: yaprakta bisl olmamali.
    CreateDirectoryW((d + L"\\alt").c_str(), nullptr);
    Check(!backup::Restore(d + L"\\alt"), "Restore(gomulu klasor) false");

    DeleteTree(d);
    DeleteTree(g_root + L"\\yarici");
}

int main()
{
    std::printf("TENGRI guvenli yazma-yolu testleri\n\n");

    if (!EnterIsolation())
    {
        std::printf("  BASARISIZ: izolasyon kurulamadi (GetTempPathW/_putenv_s)\n");
        return 1;
    }
    // Izolasyonun kendisini dogrula: gercek Windows kullanicisinin LOCALAPPDATA'si
    // ile ayni olmamali.
    Check(g_local != L"" && !StartsWith(g_local, EnvW(L"SystemRoot") + L"\\"),
          "izole LOCALAPPDATA SystemRoot ile celismiyor");

    Test_Shader_RoundTrip();
    Test_Temp_RoundTrip();
    Test_Backup_Paths();
    Test_RegText_Reading();
    Test_Restore_Rejections();

    ExitIsolation();

    std::printf("\n%d gecti, %d kaldi\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
