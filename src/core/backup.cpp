#include "backup.hpp"
#include "brand.hpp"
#include "regtext.hpp"
#include "syspath.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cwchar>
#include <vector>

#ifdef TENGRI_HAS_TWEAK_KEYS
#include "tweak_keys.h"
#else
namespace tweakkeys
{
    struct Entry { int cat; int idx; const wchar_t* path; };
    static const Entry kEntries[] = {};
    static const int kCount = 0;
}
#endif

namespace backup
{
    namespace
    {
        // reg.exe bir cift tirnak icinde yol ister; yol guvenli bir yerden geliyor ama
        // icinde tirnak bulunmamali. Bulursa dosya adi bozulur ve import yanlis yere
        // yazabilir.
        bool HasQuote(const std::wstring& s)
        {
            return s.find(L'"') != std::wstring::npos;
        }

        std::wstring RegKeyPath(const wchar_t* abbrev)
        {
            // Uretici HKCU/HKLM kisaltmalarini yaziyor; reg.exe tam ad ister.
            // Yalnizca kisaltmanin dort harfi degistirilir, ayraç çizgisine
            // dokunulmaz: onceki bicim çizgiyi düsürüyor ve
            // "HKEY_CURRENT_USERSoftware\..." gibi gecersiz bir yol üretiyordu.
            // reg.exe böyle bir anahtari bulamaz, dışa aktarim sessizce hicbir
            // sey yapmazdi.
            std::wstring p(abbrev);
            if (p.rfind(L"HKCU\\", 0) == 0)       p.replace(0, 4, L"HKEY_CURRENT_USER");
            else if (p.rfind(L"HKLM\\", 0) == 0)  p.replace(0, 4, L"HKEY_LOCAL_MACHINE");
            return p;
        }

        // Komut calistirip gercek cikis kodunu dondurur. reg.exe hatali ice aktarimi
        // da 0 dondurebilir; bu yuzden cikis kodu kontrol edilir.
        bool RunReg(const std::wstring& args)
        {
            // Yalin "reg.exe" yazilmaz: CreateProcess aramaya uygulamanin kendi
            // klasoruyle baslar, portable kurulumda o klasor kullanici-yazilabilir.
            std::wstring cmd = syspath::CommandLine(L"reg.exe", args);
            if (cmd.empty()) return false;

            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags     = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};

            std::wstring buf = cmd;
            if (!CreateProcessW(nullptr, &buf[0], nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                return false;

            // Zaman asimi: reg.exe takilirsa alt surec geride kalmasin. Tavan
            // geniş tutuluyor; meşru bir yedek bu makinede 30 MB'a kadar çıkıyor
            // (HKLM\SYSTEM\CurrentControlSet\Control alt ağacı), içe aktarımın
            // bitmesi gereken süre de o kadar uzun. Yarım kalan bir içe aktarım,
            // hiç yapılmamışından daha kötü olduğu için beklemeyi seçiyoruz.
            if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_TIMEOUT)
            {
                ::TerminateProcess(pi.hProcess, 1);
                ::WaitForSingleObject(pi.hProcess, 1000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return false;
            }

            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return code == 0;
        }

        std::wstring Timestamp()
        {
            SYSTEMTIME st = {};
            ::GetLocalTime(&st);
            wchar_t buf[32] = {};
            // Yil-ay-gun-saat-dakika-saniye: sirali mi (en yeni en buyuk) ve
            // dosya adinda kullanilabilir bir karakter dizisi.
            ::swprintf_s(buf, L"%04d%02d%02d-%02d%02d%02d",
                         st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            return buf;
        }

        bool IsDir(const std::wstring& p)
        {
            const DWORD a = GetFileAttributesW(p.c_str());
            return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
        }

        // ------------------------------------------------ geri alma doğrulaması
        //
        // Ad ayrıştırma, kodlama çözme ve içerik karşılaştırması regtext.hpp'te
        // duruyor: oradaki denetimler registry'ye ve üretici tabloya dokunmadan
        // sınanabiliyor. Burada yalnızca "bu dosyanın dokunabileceği anahtarlar
        // hangileri" sorusunun cevabı (ExpectedKeys) ve yolun yedek kökünün içinde
        // olup olmadığı denetleniyor.
        //
        // Bir (kategori,sıra) çifti birden fazla anahtara karşılık gelebilir:
        // üretici tablo, aynı ayarın dokunduğu her anahtarı ayrı satıra yazar
        // (ör. cat 7 idx 1 üç farklı .exe'nin IFEO alt anahtarını taşıyor).
        // ExportForCategories her satırı kendi dosyasına yazar, ama geri alma
        // sırasında hangi dosyanın hangi satıra ait olduğu güvenilir bir bilgi
        // değil (klasör kullanıcı tarafından düzenlenebiliyor). Bu yüzden denetim
        // dosyayı çiftin TÜM anahtarlarına karşı yapılıyor: içerik, o ayarın
        // yedeklediği alt ağaçlardan birinin dışına çıkıyorsa reddedilir.
        void ExpectedKeys(int cat, int idx, std::vector<std::wstring>* out)
        {
            out->clear();
            for (int i = 0; i < tweakkeys::kCount; ++i)
                if (tweakkeys::kEntries[i].cat == cat && tweakkeys::kEntries[i].idx == idx)
                    out->push_back(RegKeyPath(tweakkeys::kEntries[i].path));
        }

        // Yol, yedek kökünün ALTINDA ve tek bir zaman damgası adlı alt klasör
        // olmalı. Kök ortam değişkeninden çözüldüğü için ".." gibi bir parçanın
        // kökün dışına taşımasına izin verilmez.
        bool InsideRoot(const std::wstring& dir)
        {
            const std::wstring root = RootDirectory();
            if (root.empty() || dir.size() <= root.size() + 1) return false;
            if (_wcsnicmp(dir.c_str(), root.c_str(), root.size()) != 0) return false;
            if (dir[root.size()] != L'\\') return false;

            const std::wstring leaf = dir.substr(root.size() + 1);
            if (leaf.find(L'\\') != std::wstring::npos) return false;
            for (wchar_t c : leaf)
                if (c != L'-' && (c < L'0' || c > L'9')) return false;
            return true;
        }
    }

    std::wstring RootDirectory()
    {
        wchar_t dir[MAX_PATH] = {};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) return {};

        // Veriler %APPDATA% altindaki klasorde duruyor; yedekler de ayni yerde,
        // ayri bir alt klasorde tutulur ki "Temizle" gibi bir islem onlari gostermesin.
        std::wstring root = std::wstring(dir) + L"\\" + brand::kAppDataFolder + L"\\backups";
        ::CreateDirectoryW((std::wstring(dir) + L"\\" + brand::kAppDataFolder).c_str(), nullptr);
        ::CreateDirectoryW(root.c_str(), nullptr);
        return root;
    }

    bool ExportForCategories(const int* cats, int count, std::wstring* outDir)
    {
        if (!cats || count <= 0) return false;
        const std::wstring root = RootDirectory();
        if (root.empty()) return false;

        const std::wstring dir = root + L"\\" + Timestamp();
        if (!::CreateDirectoryW(dir.c_str(), nullptr)) return false;

        int exported = 0;
        int prevCat = -1, prevIdx = -1, rep = 0;
        for (int i = 0; i < tweakkeys::kCount; ++i)
        {
            const tweakkeys::Entry& e = tweakkeys::kEntries[i];

            bool wanted = false;
            for (int c = 0; c < count; ++c)
                if (cats[c] == e.cat) { wanted = true; break; }
            if (!wanted) continue;

            const std::wstring key = RegKeyPath(e.path);
            if (key.empty() || HasQuote(key)) continue;

            // Anahtar yoksa reg export hata verir. Bu bir basarisizlik degil -- ayar
            // ilk kez uygulaniyordur ve geri alinacak bir deger zaten yok. Dosya
            // olusmadigi icin o yedek icinde tutulmaz; hepsi basarisiz olursa klasor
            // de silinir.
            //
            // Ay adlandirmasi: bir (kategori,sira) cifti birden fazla anahtara
            // karsilik gelebiliyor (uretablo ayni ayarin her anahtarini ayri satira
            // yazar). Hepsine "<cat>-<idx>.reg" demek, ikinci disa aktarimin
            // birincinin uzerine yazmasi demekti: o ayarin yedeginde tek bir
            // anahtar kalirdi. Ikinci ve sonraki satirlar adin sonuna bir tekrar
            // sayisi aliyor; boylece her anahtar kendi dosyasina yaziyor. Geri
            // alma icin sayinin anlami yok -- denetim, adin karsilik geldigi
            // ciftin TUM anahtarlarina bakar.
            if (e.cat != prevCat || e.idx != prevIdx) { prevCat = e.cat; prevIdx = e.idx; rep = 0; }
            wchar_t file[MAX_PATH] = {};
            if (rep == 0)
                ::swprintf_s(file, L"%s\\%d-%d.reg", dir.c_str(), e.cat, e.idx);
            else
                ::swprintf_s(file, L"%s\\%d-%d-%d.reg", dir.c_str(), e.cat, e.idx, rep);
            ++rep;
            if (HasQuote(file)) continue;

            std::wstring args = L"export \"" + key + L"\" \"" + file + L"\" /y";
            if (RunReg(args)) ++exported;
            else ::DeleteFileW(file);
        }

        if (exported == 0)
        {
            // Hiçbir şey dışa aktarılamadı: boş bir yedek klasörü bırakmak, geri al
            // düğmesinin "yedek var" görünmesine yol açar.
            ::RemoveDirectoryW(dir.c_str());
            return false;
        }

        if (outDir) *outDir = dir;
        return true;
    }

    std::vector<std::wstring> List()
    {
        std::vector<std::wstring> out;
        const std::wstring root = RootDirectory();
        if (root.empty()) return out;

        WIN32_FIND_DATAW fd = {};
        const std::wstring pattern = root + L"\\*";
        HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return out;

        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            out.push_back(root + L"\\" + name);
        } while (::FindNextFileW(h, &fd));

        ::FindClose(h);
        return out;
    }

    std::wstring Latest()
    {
        // Dizin adlari zaman damgasiyla sirali oldugu icin alfabetik sira = kronolojik.
        // Yine de son ogeleri karsilastirip en buyuk olanini seçmek daha saglam.
        std::vector<std::wstring> all = List();
        std::wstring best;
        for (const std::wstring& d : all)
        {
            const size_t slash = d.find_last_of(L'\\');
            const std::wstring name = (slash == std::wstring::npos) ? d : d.substr(slash + 1);
            if (best.empty() || name > best) best = name;
        }
        if (best.empty()) return {};
        const std::wstring root = RootDirectory();
        return root + L"\\" + best;
    }

    bool Restore(const std::wstring& dir)
    {
        if (dir.empty() || !IsDir(dir)) return false;
        if (!InsideRoot(dir)) return false;

        WIN32_FIND_DATAW fd = {};
        HANDLE h = ::FindFirstFileW((dir + L"\\*.reg").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;

        bool any = false;
        bool all = true;
        std::vector<std::wstring> keys;
        do
        {
            int cat = -1, idx = -1;
            if (!regtext::ParseBackupName(fd.cFileName, &cat, &idx)) continue;
            ExpectedKeys(cat, idx, &keys);
            if (keys.empty()) continue;                    // bizim dışa aktarmadığımız bir ad

            const std::wstring file = dir + L"\\" + fd.cFileName;
            for (size_t i = 0; i < keys.size(); ++i)
                if (keys[i].empty() || HasQuote(keys[i])) { keys.clear(); break; }
            if (keys.empty() || HasQuote(file)) continue;

            // İçerik denetimi: dosya yalnızca beklenen anahtarların (ya da alt
            // anahtarlarının) içindeyse içe aktarılır. Okunamayan/bozuk dosya da
            // reddedilir.
            std::wstring text;
            if (!regtext::ReadRegText(file, &text) ||
                !regtext::ContentMatchesKeys(text, keys)) continue;

            std::wstring args = L"import \"" + file + L"\"";
            if (RunReg(args)) any = true;
            else all = false;
        } while (::FindNextFileW(h, &fd));

        ::FindClose(h);
        // Kismi basari "tamamlandi" diye bildirilmemeli; kullanicinin bir anahtarin
        // geri gelmedigini bilmesi gerekiyor.
        return any && all;
    }
}