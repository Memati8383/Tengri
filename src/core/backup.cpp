#include "backup.hpp"
#include "brand.hpp"
#include "regtext.hpp"
#include "regwrite.hpp"
#include "tweak_keys.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace backup
{
    namespace
    {
        // ------------------------------------------------ dışa aktarım derleyicisi
        //
        // Bir dosya bir (kategori,sıra) çiftine karşılık gelir ve içinde o çiftin
        // dokunduğu anahtarların yalnızca DOKUNULAN değerleri durur. Alt anahtar ya
        // da dokunulmayan değer yazan bir yol kalmadı, çünkü üretici artık reg.exe
        // değil; bu yüzden dosyalar birkaç satır ve birkaç kilobayt.
        struct Section { std::wstring key; std::wstring lines; };
        struct PendingFile { int cat; int idx; std::vector<Section> sections; };

        PendingFile* FindFile(std::vector<PendingFile>& files, int cat, int idx)
        {
            for (PendingFile& f : files)
                if (f.cat == cat && f.idx == idx) return &f;
            files.push_back({ cat, idx, {} });
            return &files.back();
        }

        std::wstring* SectionLines(std::vector<Section>& secs, const std::wstring& key)
        {
            for (Section& s : secs)
                if (_wcsicmp(s.key.c_str(), key.c_str()) == 0) return &s.lines;
            secs.push_back({ key, {} });
            return &secs.back().lines;
        }

        std::wstring* FindSection(PendingFile& f, const std::wstring& key)
        {
            return SectionLines(f.sections, key);
        }

        // Canlı bir değerin tek satırı. Açık anahtar kolu çağranın kendisininki
        // olur, böylece bir anahtar için tek açılımla birkaç değer okunabilir.
        //
        // Üç atlama neden sessiz, neden zorunlu:
        //   - değer yoksa geri alınacak bir şey yoktur (ayar ilk kez yazılıyor),
        //   - tipi geri yüklenebilir değilse biz onu zaten yazamayız,
        //   - tavanı aşan bir bayt, üçüncü bir programın oraya koyduğu ve
        //     "eski ayar" sayılmayacak bir şeydir.
        std::wstring LiveValueLine(HKEY key, const wchar_t* name)
        {
            DWORD type = 0, cb = 0;
            if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS)
                return {};
            if (!regtext::TypeRestorable(type)) return {};
            if (cb > regtext::kMaxValueBytes) return {};

            std::vector<unsigned char> data(cb ? cb : 1u, 0);
            if (::RegQueryValueExW(key, name, nullptr, &type, data.data(), &cb) != ERROR_SUCCESS)
                return {};

            std::wstring line;
            if (!regtext::SerializeValue(name, type, data.data(), cb, &line)) return {};
            return line;
        }

        bool IsWanted(const int* cats, int count, int cat)
        {
            for (int c = 0; c < count; ++c)
                if (cats[c] == cat) return true;
            return false;
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

        // Aynı saniyede iki kez yedek istenirse (iki ayar üst üste çevrildiğinde
        // olur) ikinci CreateDirectoryW ERROR_ALREADY_EXISTS verir. O durumda
        // yedeği hiç almamak, kullanıcının geri al düğmesini sessizce eksik
        // bırakırdı; ada bir tekrar sayısı ekleniyor.
        std::wstring CreateBackupDir(const std::wstring& root)
        {
            const std::wstring stamp = Timestamp();
            for (int rep = 0; rep < 1000; ++rep)
            {
                wchar_t path[MAX_PATH] = {};
                if (rep == 0) ::swprintf_s(path, L"%s\\%s", root.c_str(), stamp.c_str());
                else          ::swprintf_s(path, L"%s\\%s-%d", root.c_str(), stamp.c_str(), rep);

                if (::CreateDirectoryW(path, nullptr)) return path;
                if (::GetLastError() != ERROR_ALREADY_EXISTS) return {};
            }
            return {};
        }

        bool IsDir(const std::wstring& p)
        {
            const DWORD a = GetFileAttributesW(p.c_str());
            return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
        }

        // ------------------------------------------------ klasör adı denetimi
        //
        // Yedek klasörünün adı yalnızca bizim yazdığımız biçimde olabilir:
        // yyyyMMdd-HHmmss ve çakışmada sonuna eklenen -<tekrar>. Rakam/tire
        // dışına çıkmak zaten yasaktı; buraya eklenen, basamakların SAYILARININ
        // denetlenmesi. Yedek kopyalari kullanici-yazilabilir bir yerde durdugu
        // icin biri "9999999999" gibi gecersiz ama alfabetik olarak en buyuk bir
        // ad koyarsa Latest() onu secer ve kullanicinin "geri al"i saldirganin
        // dosyalarina gider. Bicim denetimi bunu ve benzeri uyanci adlari keser;
        // gercek bir zaman damgasi seklinde yazilmis bir uyanci ad hâlâ gecerdur
        // ve kapsam zaten tablodan gelen izinle sinirlidir.
        bool IsBackupLeafName(const std::wstring& leaf)
        {
            size_t i = 0;
            auto take = [&](int n, int* out) -> bool {
                int v = 0;
                for (int k = 0; k < n; ++k)
                {
                    if (i >= leaf.size() || leaf[i] < L'0' || leaf[i] > L'9') return false;
                    v = v * 10 + (leaf[i++] - L'0');
                }
                *out = v;
                return true;
            };

            int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
            if (!take(4, &year) || !take(2, &month) || !take(2, &day)) return false;
            if (i >= leaf.size() || leaf[i] != L'-') return false;
            ++i;
            if (!take(2, &hour) || !take(2, &minute) || !take(2, &second)) return false;

            // 2020: arac ilk surumunden onceki yillar gecerli bir yedek adasi
            // olamaz; ileri sinir yok, cunku saatleri geleceye alan bir dosya en
            // fazla "geri al"in kapsam disindaki degerler nedeniyle reddedilmesine
            // yol acar (asagidaki icerik denetimi), yetki vermez.
            if (year < 2020 || year > 9999) return false;
            if (month < 1 || month > 12) return false;
            if (day < 1 || day > 31) return false;
            if (hour > 23 || minute > 59 || second > 59) return false;

            if (i == leaf.size()) return true;
            if (leaf[i] != L'-') return false;      // tire disinda bir karakter
            ++i;
            int rep = 0;
            if (!take(3, &rep) || rep == 0) return false;
            return i == leaf.size();
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
            return IsBackupLeafName(leaf);
        }

        // Bir (kategori,sıra) çiftinin izin listesi. Yollar tablodaki kısaltmayla
        // ("HKLM\...") durur; ParseRegText dosyadaki tam adla karşılaştırır ve
        // ExpandKey'i kendi içinde uygular, burada kısaltmayı olduğu gibi vermek
        // yeterli.
        void AllowedFor(int cat, int idx, std::vector<regtext::Allowed>* out)
        {
            out->clear();
            for (int i = 0; i < tweakkeys::kCount; ++i)
            {
                const tweakkeys::Entry& e = tweakkeys::kEntries[i];
                if (e.cat != cat || e.idx != idx) continue;
                for (const wchar_t* const* v = e.values; v && *v; ++v)
                    out->push_back({ e.path, *v });
            }
        }

        // UTF-16LE + BOM: regedit'in kendi yazdığı biçim, okuyucumuz da ikisini
        // kabul ediyor. Kullanıcı yedeği çift tıklayıp denetlemek isteyebilir.
        bool WriteRegFile(const std::wstring& path, const std::wstring& text)
        {
            HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;

            const wchar_t bom = 0xFEFF;
            DWORD done = 0;
            BOOL ok = ::WriteFile(h, &bom, sizeof(bom), &done, nullptr);
            if (ok && !text.empty())
                ok = ::WriteFile(h, text.data(), (DWORD)(text.size() * sizeof(wchar_t)),
                                 &done, nullptr);
            ::CloseHandle(h);
            return ok != FALSE;
        }

        std::wstring FileText(const std::vector<Section>& sections)
        {
            std::wstring t = L"Windows Registry Editor Version 5.00\r\n\r\n";
            for (const Section& s : sections)
            {
                // Bölgesi başlık satırı köşeli ayraçta durur; ayraç ya da satır
                // sonu taşıyan bir anahtar adı metni bozardı. Tabloda böyle bir
                // ad yok, ama üretici elle düzenlenmiş olsaydı burada dururduk.
                if (s.key.find(L'[') != std::wstring::npos ||
                    s.key.find(L']') != std::wstring::npos ||
                    s.key.find(L'\n') != std::wstring::npos) continue;
                t += L"[";
                t += s.key;
                t += L"]\r\n";
                t += s.lines;
                t += L"\r\n";
            }
            return t;
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

        const std::wstring dir = CreateBackupDir(root);
        if (dir.empty()) return false;

        std::vector<PendingFile> files;
        for (int i = 0; i < tweakkeys::kCount; ++i)
        {
            const tweakkeys::Entry& e = tweakkeys::kEntries[i];
            if (!IsWanted(cats, count, e.cat)) continue;

            const std::wstring full = regtext::ExpandKey(e.path);
            HKEY rootKey = nullptr;
            std::wstring sub;
            if (!regtext::SplitRoot(full, &rootKey, &sub)) continue;

            // Yalnızca OKUMA. Anahtar yoksa ayar ilk kez uygulanıyordur ve geri
            // alınacak bir değer zaten yoktur; bu bir başarısızlık değil.
            HKEY k = nullptr;
            if (::RegOpenKeyExW(rootKey, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
                continue;

            for (const wchar_t* const* vn = e.values; vn && *vn; ++vn)
            {
                const std::wstring line = LiveValueLine(k, *vn);
                if (line.empty()) continue;
                *FindSection(*FindFile(files, e.cat, e.idx), full) += line;
            }
            ::RegCloseKey(k);
        }

        int written = 0;
        bool clean = true;
        for (const PendingFile& f : files)
        {
            if (f.sections.empty()) continue;
            wchar_t path[MAX_PATH] = {};
            ::swprintf_s(path, L"%s\\%d-%d.reg", dir.c_str(), f.cat, f.idx);
            if (WriteRegFile(path, FileText(f.sections))) ++written;
            else clean = false;
        }

        if (written == 0 || !clean)
        {
            // Kısmi yedek "yedek var" diye bırakılmamalı: eksik dosyanın
            // farkında olmayan kullanıcı geri alma sırasında sessizce yarım
            // bir duruma döner.
            WIN32_FIND_DATAW fd = {};
            HANDLE h = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                    ::DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
                } while (::FindNextFileW(h, &fd));
                ::FindClose(h);
            }
            ::RemoveDirectoryW(dir.c_str());
            return false;
        }

        if (outDir) *outDir = dir;
        return true;
    }

    bool ExportPairs(const std::vector<std::pair<int, int>>& sel,
                     const std::wstring& file, int* pairsOut, int* toolOut)
    {
        if (pairsOut) *pairsOut = 0;
        if (toolOut)  *toolOut  = 0;
        if (file.empty() || sel.empty()) return false;

        std::vector<Section> secs;
        std::vector<std::pair<std::wstring, std::wstring>> seen;
        int values = 0, tools = 0;

        for (const std::pair<int, int>& s : sel)
        {
            int rows = 0;
            for (int i = 0; i < tweakkeys::kCount; ++i)
            {
                const tweakkeys::Entry& e = tweakkeys::kEntries[i];
                if (e.cat != s.first || e.idx != s.second) continue;
                ++rows;

                const std::wstring full = regtext::ExpandKey(e.path);
                HKEY rootKey = nullptr;
                std::wstring sub;
                if (!regtext::SplitRoot(full, &rootKey, &sub)) continue;

                HKEY k = nullptr;
                if (::RegOpenKeyExW(rootKey, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
                    continue;
                for (const wchar_t* const* vn = e.values; vn && *vn; ++vn)
                {
                    const std::pair<std::wstring, std::wstring> id(full, *vn);
                    bool dup = false;
                    for (const std::pair<std::wstring, std::wstring>& p : seen)
                        if (p == id) { dup = true; break; }
                    if (dup) continue;
                    seen.push_back(id);

                    const std::wstring line = LiveValueLine(k, *vn);
                    if (line.empty()) continue;
                    *SectionLines(secs, full) += line;
                    ++values;
                }
                ::RegCloseKey(k);
            }
            // Tabloda kaydı olmayan ayar komut araciyla degisiyor; profil
            // onu tasiyamaz. Sayiyi geri dondurmek zorunlu, cunku arayuz
            // "hepsi disa aktarildi" diyemez.
            if (rows == 0) ++tools;
        }

        if (secs.empty()) return false;
        if (!WriteRegFile(file, FileText(secs))) return false;

        if (pairsOut) *pairsOut = values;
        if (toolOut)  *toolOut  = tools;
        return true;
    }

    std::vector<Snapshot> Scan()
    {
        std::vector<Snapshot> out;
        const std::wstring root = RootDirectory();
        if (root.empty()) return out;

        for (const std::wstring& dir : List())
        {
            const size_t slash = dir.find_last_of(L'\\');
            const std::wstring leaf = (slash == std::wstring::npos)
                                        ? dir : dir.substr(slash + 1);
            // Bicimi taninmayan klasor Listedeki gibi gorunur ama Latest ve
            // Restore onu reddeder; arayuzde durmasi "secilebilir yedek" yalani
            // olurdu.
            if (!IsBackupLeafName(leaf)) continue;

            Snapshot s;
            s.dir  = dir;
            s.leaf = leaf;

            WIN32_FIND_DATAW fd = {};
            HANDLE h = ::FindFirstFileW((dir + L"\\*.reg").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    int cat = -1, idx = -1;
                    if (!regtext::ParseBackupName(fd.cFileName, &cat, &idx)) continue;
                    bool have = false;
                    for (int c : s.cats) if (c == cat) { have = true; break; }
                    if (!have) s.cats.push_back(cat);
                } while (::FindNextFileW(h, &fd));
                ::FindClose(h);
            }
            std::sort(s.cats.begin(), s.cats.end());
            out.push_back(std::move(s));
        }

        // Klasör adı zaman damgası: sözlüksel büyük olan yenidir.
        std::sort(out.begin(), out.end(), [](const Snapshot& a, const Snapshot& b)
                  { return a.leaf > b.leaf; });
        return out;
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
        // Yalnızca biçimi geçerli klasörler aday: InsideRoot/Restore zaten aynı
        // denetimi uyguluyor, burada elemek "yedek var ama içeriği reddedildi"
        // yerine "yedek yok" görmemizi sağlıyor.
        std::vector<std::wstring> all = List();
        std::wstring best;
        const std::wstring root = RootDirectory();
        for (const std::wstring& d : all)
        {
            const size_t slash = d.find_last_of(L'\\');
            const std::wstring name = (slash == std::wstring::npos) ? d : d.substr(slash + 1);
            if (!IsBackupLeafName(name)) continue;
            if (best.empty() || name > best) best = name;
        }
        if (best.empty()) return {};
        return root + L"\\" + best;
    }

    bool Restore(const std::wstring& dir)
    {
        if (dir.empty() || !IsDir(dir)) return false;
        if (!InsideRoot(dir)) return false;

        WIN32_FIND_DATAW fd = {};
        HANDLE h = ::FindFirstFileW((dir + L"\\*.reg").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;

        int applied = 0;
        int failed = 0;
        std::vector<regtext::Allowed> allowed;
        std::wstring text;
        std::vector<regtext::Record> recs;

        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            int cat = -1, idx = -1;
            if (!regtext::ParseBackupName(fd.cFileName, &cat, &idx)) continue;

            AllowedFor(cat, idx, &allowed);
            if (allowed.empty()) continue;              // bizim dışa aktarmadığımız bir ad

            const std::wstring file = dir + L"\\" + fd.cFileName;

            // İçerik denetimi: dosya yalnızca beklenen (anahtar,değer)
            // çiftlerini taşıyorsa kabul edilir. Okunamayan/bozuk dosya da
            // reddedilir. Denetimden geçen baytlar bellekte durur ve oradan
            // yazılır — dosya bu açılımdan sonra bir daha okunmuyor, yani
            // denetlenen içerikle yazılan içerik aynı.
            if (!regtext::ReadRegText(file, &text)) continue;
            if (!regtext::ParseRegText(text, &allowed, &recs)) continue;
            if (recs.empty()) continue;

            const regwrite::Result r = regwrite::Apply(recs);
            applied += r.written + r.removed + r.absent;
            failed  += r.failed;
        } while (::FindNextFileW(h, &fd));

        ::FindClose(h);
        // Kismi basari "tamamlandi" diye bildirilmemeli; kullanicinin bir degerin
        // geri gelmedigini bilmesi gerekiyor.
        return applied > 0 && failed == 0;
    }
}
