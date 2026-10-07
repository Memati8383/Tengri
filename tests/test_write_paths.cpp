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
//   - backup::Restore() yalnizca .reg İÇERMEYEN klasorler uzerinde denenir. Restore
//     .reg gorevini reg.exe IMPORT ile yazar; bu gercek registry'ye dokunur. O yuzden
//     testte hicbir .reg dosyasi import edilmez, yalnizca koruma/dondurucu dallari
//     denenir.
//   - cleaner kategori 3 (Geri Donusum Kutusu) cagrilmaz: SHEmptyRecycleBinW gercek
//     kutuyu bosaltir. Test yalnizca kategori 0 (TEMP) ve shader onbellegini kullanir.
//   - ExportForCategories /FE/TENGRI_HAS_TWEAK_KEYS derlenir; tweakkeys::kCount=0
//     oldugundan disa aktarim yapilmaz ama "bos yedek klasoru birakma" temizleme
//     dali calisir. Boylece registry'den hicbir anahtar disa aktarilmaz (reg export
//     yalnizca okur ama yine de gereksiz islem yapmiyoruz).

#include "../src/core/cleaner.hpp"
#include "../src/core/backup.hpp"

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
    // OKUMA islemidir (reg.exe export); registry'yi DEGISTIRMEZ. Akin yazdigi tek
    // sey izole LOCALAPPDATA altindaki gecici yedek klasorude ve test sonunda
    // DeleteTree ile silinir. Makineden makineye gecerli anahtar kumesi degisebilir
    // (bir ayar henuz uygulanmamis olabilir), bu yuzden sonuc kosullu dogrulanir:
    //   true  -> yeni yedek klasoru izole kokte + icinde en az bir .reg + Latest onu gorur
    //   false -> bos klasor birakilmamali (temizleme dali)
    const int cats[] = { 0, 1, 2, 3, 4, 5, 6 };
    const size_t before = backup::List().size();
    std::wstring outDir;
    const bool exp = backup::ExportForCategories(cats, 6, &outDir);
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
        // Disa aktarim yalnizca okudu: geri alma (import) CALL EDILMEDI.
        Check(backup::Latest() == outDir, "yeni yedek Latest() oldu (zaman damgasi en taze)");
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

    ExitIsolation();

    std::printf("\n%d gecti, %d kaldi\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
