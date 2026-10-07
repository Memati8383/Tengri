#include "notify.hpp"
#include "brand.hpp"
#include "lang.hpp"
#include <windows.h>
#include <shlobj.h>
#include <propsys.h>
#include <propkey.h>    // PKEY_AppUserModel_ID
#include <string>
#include <vector>
#include <cstring>    // memcpy: simge gövdeleri birleştirilirken

// C++/WinRT yalnızca BU dosyada görünür. Uygulamanın geri kalanı WinRT
// bilmez; tek istisna main.cpp'deki tek örnek kilidi (CreateMutex) ve
// WM_COPYDATA yönlendirmesi, ikisi de WinRT değil düz Win32'dir.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Data.Xml.Dom.h>

#pragma comment(lib, "windowsapp.lib")   // WinRT aktivasyon fabrikaları
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")     // IPropertyStore: kısayolun uygulama kimliği

namespace notify
{
    bool toastsEnabled = true;
    bool quietMode     = false;
    bool g_installedShortcut = false;

    namespace
    {
        // Uygulama kimliği. Windows bildirimleri bu kimliğe göre gruplanır ve
        // Başlat menüsü kısayolunun System.AppUserModel.ID değeriyle eşleşmek
        // ZORUNDADIR; ikisi ayrı ayrı değişirse bildirimler "bilinmeyen
        // uygulama" olarak görünür.
        constexpr const wchar_t* kAumid = L"TENGRI.SystemOptimizer";

        // Toast düğmelerinin XML'de taşıdığı komut kodları. Bunlar tek örnek
        // kilidinden geçerek ana sürece ulaşır; değerler kalıcıdır.
        constexpr int kCmdView    = 1;
        constexpr int kCmdUndo    = 2;
        constexpr int kCmdRetry   = 3;
        constexpr int kCmdDownload = 4;

        // kCmd* ile notify.hpp'teki Cmd* aynı sayılar olmak ZORUNDA: XML'e
        // yazılan değer buradan, okuyan ParseLaunch enum'dan gelir. Biri
        // sessizce kayarsa düğmeye basmak hiçbir şey yapmaz.
        static_assert(kCmdView     == CmdView,     "CmdView kaydi");
        static_assert(kCmdUndo     == CmdUndo,     "CmdUndo kaydi");
        static_assert(kCmdRetry    == CmdRetry,    "CmdRetry kaydi");
        static_assert(kCmdDownload == CmdDownload, "CmdDownload kaydi");

        bool g_ready = false;   // WinRT başlatıldı mı

        // Marka simgesinin diske yazılmış .ico yolu; boşsa görsel hiç istenmez.
        std::wstring g_iconFile;

        std::wstring Utf8(const char* s)
        {
            if (!s || !*s) return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
            if (n <= 1) return {};
            std::wstring w(static_cast<size_t>(n - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
            return w;
        }

        // XML kaçışı. Başlık ve gövde kaynaktan (L(...) sonucu) gelen metin
        // olabilir; & veya < içerse XML bozulur ve bildirim hiç görünmez.
        std::wstring XmlEscape(const wchar_t* s)
        {
            std::wstring out;
            if (!s) return out;
            for (const wchar_t* p = s; *p; ++p)
            {
                switch (*p)
                {
                case L'&':  out += L"&amp;";  break;
                case L'<':  out += L"&lt;";   break;
                case L'>':  out += L"&gt;";   break;
                case L'"':  out += L"&quot;"; break;
                case L'\'': out += L"&apos;"; break;
                default:    out += *p;        break;
                }
            }
            return out;
        }

        std::wstring PrefPath()
        {
            wchar_t dir[MAX_PATH] = {};
            if (!::GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) return {};
            const std::wstring root = std::wstring(dir) + L"\\" + brand::kAppDataFolder;
            ::CreateDirectoryW(root.c_str(), nullptr);
            return root + L"\\notify.dat";
        }

        // ---- marka simgesi dosyası -------------------------------------------
        // AUMID'in IconUri alanı bir GÖRSEL dosyası ister. Exe yolu yazılırsa
        // Windows oradaki kaynağı çıkaramaz ve bildirim simgesiz kalır (yapılan
        // hataydı). Kabuk simgesi çalışma anında exe'nin kendi RT_GROUP_ICON
        // kaynağından yeniden birleştirilip %APPDATA%\TENGRI\tengri.ico yazılır:
        // böylece bildirimdeki artwork ile görev çubuğundaki artwork aynı dosyanın
        // kopyası olur, ayrı bir görsel bakımı yoktur.
        //
        // Başlıklardaki ICONDIR/GRPICONDIRENTRY tanımları iki farklı paket
        // korumasında durduğu ve ikisi de 2 bayt hizalı olduğu için yapılar burada
        // kendileri tanımlanır; altlarındaki static_assert'ler hizalama sessizce
        // bozulursa derlemeyi durdurur.
        #pragma pack(push, 2)
        struct GrpEntry { BYTE bWidth, bHeight, bColors, bReserved; WORD planes, bits; DWORD size; WORD id; };
        struct GrpDir   { WORD reserved, type, count; };
        struct IcoEntry { BYTE bWidth, bHeight, bColors, bReserved; WORD planes, bits; DWORD size, offset; };
        struct IcoDir   { WORD reserved, type, count; };
        #pragma pack(pop)
        static_assert(sizeof(GrpEntry) == 14, "GRPICONDIRENTRY 14 bayt olmali");
        static_assert(sizeof(GrpDir)   ==  6, "GRPICONDIR 6 bayt olmali");
        static_assert(sizeof(IcoEntry) == 16, "ICONDIRENTRY 16 bayt olmali");
        static_assert(sizeof(IcoDir)   ==  6, "ICONDIR 6 bayt olmali");

        std::wstring BrandIconPath()
        {
            wchar_t dir[MAX_PATH] = {};
            if (!::GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) return {};
            const std::wstring root = std::wstring(dir) + L"\\" + brand::kAppDataFolder;
            ::CreateDirectoryW(root.c_str(), nullptr);
            return root + L"\\tengri.ico";
        }

        // Exe'nin kendi simge kaynağını tek bir .ico dosyasına yazar.
        // Başarısız olursa false döner ve çağıran exe yoluna düşer.
        bool EnsureBrandIcon()
        {
            const std::wstring path = BrandIconPath();
            if (path.empty()) return false;

            const HMODULE mod = ::GetModuleHandleW(nullptr);
            const HRSRC   gr  = ::FindResourceW(mod, MAKEINTRESOURCEW(brand::kIconId), RT_GROUP_ICON);
            if (!gr) return false;

            const DWORD   gsz = ::SizeofResource(mod, gr);
            const HGLOBAL hg  = ::LoadResource(mod, gr);
            if (!hg || gsz < sizeof(GrpDir)) return false;

            const GrpDir*   hdr   = (const GrpDir*)::LockResource(hg);
            const GrpEntry* entries = (const GrpEntry*)(hdr + 1);
            if (!hdr || !hdr->count ||
                gsz < sizeof(GrpDir) + (DWORD)hdr->count * sizeof(GrpEntry))
                return false;
            const WORD count = hdr->count > 16 ? 16 : hdr->count;   // uçmuş bir sayı sınırlanır

            // Her girdinin RT_ICON gövdesi ayrı bir kaynaktır; toplanır.
            std::vector<const BYTE*> img(count, nullptr);
            std::vector<DWORD>       len(count, 0);
            DWORD total = sizeof(IcoDir) + count * sizeof(IcoEntry);
            for (WORD i = 0; i < count; ++i)
            {
                const HRSRC r = ::FindResourceW(mod, MAKEINTRESOURCEW(entries[i].id), RT_ICON);
                if (!r) return false;
                const DWORD s = ::SizeofResource(mod, r);
                const HGLOBAL g = ::LoadResource(mod, r);
                if (!g || !s) return false;
                img[i] = (const BYTE*)::LockResource(g);
                len[i] = s;
                total += s;
            }
            if (!total || total > 4u << 20) return false;   // 4 MB'lik simge gerçek değil

            // Dosya zaten aynı boyuttaysa yeniden yazma: içerik yalnızca exe
            // değişirse değişir ve o durumda boyut da değişir.
            {
                WIN32_FILE_ATTRIBUTE_DATA fa;
                if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) &&
                    fa.nFileSizeLow == total && fa.nFileSizeHigh == 0)
                    return true;
            }

            std::vector<BYTE> out(total);
            IcoDir* od = (IcoDir*)out.data();
            od->reserved = 0; od->type = 1; od->count = count;

            IcoEntry* oe = (IcoEntry*)(od + 1);
            DWORD     at = sizeof(IcoDir) + count * sizeof(IcoEntry);
            for (WORD i = 0; i < count; ++i)
            {
                oe[i].bWidth   = entries[i].bWidth;
                oe[i].bHeight  = entries[i].bHeight;
                oe[i].bColors  = entries[i].bColors;
                oe[i].bReserved= entries[i].bReserved;
                oe[i].planes   = entries[i].planes;
                oe[i].bits     = entries[i].bits;
                oe[i].size     = len[i];
                oe[i].offset   = at;
                memcpy(out.data() + at, img[i], len[i]);
                at += len[i];
            }

            // Önce yan dosyaya yaz, sonra taşı: yarı yazılmış bir .ico, hiç
            // olmayan bir .ico'dan daha kötüdür (Windows onu okumaya çalışır).
            const std::wstring tmp = path + L".tmp";
            HANDLE f = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return false;
            bool ok = true;
            for (DWORD w = 0; w < total; )
            {
                DWORD n = 0;
                const DWORD chunk = (total - w < 65536) ? total - w : 65536;
                if (!::WriteFile(f, out.data() + w, chunk, &n, nullptr) || n != chunk) { ok = false; break; }
                w += n;
            }
            ::CloseHandle(f);
            if (ok)
                ok = ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (!ok) ::DeleteFileW(tmp.c_str());
            return ok;
        }

        // file:/// biçimi: WinRT görsel kaynağı mutlak Windows yolunu değil URI ister.
        std::wstring IconUri(const std::wstring& path)
        {
            std::wstring s = path;
            for (wchar_t& c : s) if (c == L'\\') c = L'/';
            return L"file:///" + s;
        }

        void LoadPrefs()
        {
            const std::wstring p = PrefPath();
            if (p.empty()) return;
            HANDLE f = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return;   // ilk çalıştırma: varsayılanlar

            char buf[3] = {};
            DWORD n = 0;
            const bool ok = ::ReadFile(f, buf, 2, &n, nullptr) && n == 2;
            ::CloseHandle(f);
            if (!ok) return;

            toastsEnabled = buf[0] != '0';
            quietMode     = buf[1] != '0';
        }

        void SavePrefs()
        {
            const std::wstring p = PrefPath();
            if (p.empty()) return;
            HANDLE f = ::CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return;
            const char buf[2] = { toastsEnabled ? '1' : '0', quietMode ? '1' : '0' };
            DWORD n = 0;
            ::WriteFile(f, buf, 2, &n, nullptr);
            ::CloseHandle(f);
        }

        // ---- AUMID kaydı -------------------------------------------------------
        // HKCU\Software\Classes\AppUserModelId\<AUMID>. Görünen ad ve simge
        // bildirim kartında kullanılır; kayıt yoksa bildirim uygulama adıyla
        // boş görünür.
        void RegisterAumid()
        {
            HKEY h = nullptr;
            std::wstring key = L"Software\\Classes\\AppUserModelId\\";
            key += kAumid;
            if (::RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &h,
                                  nullptr) != ERROR_SUCCESS)
                return;

            const wchar_t* display = brand::kMsgTitle;
            ::RegSetValueExW(h, L"DisplayName", 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(display),
                             static_cast<DWORD>((wcslen(display) + 1) * sizeof(wchar_t)));

            // IconUri bir görsel dosyası ister; exe yolu Windows'a göre boş bir
            // simgedir. Bu yüzden diske yazılmış .ico kullanılır, o da olmazsa
            // eski davranışa (exe yolu) düşülür — hiçbir değer vermemekten iyi.
            std::wstring icon = g_iconFile;
            if (icon.empty())
            {
                wchar_t exe[MAX_PATH] = {};
                if (::GetModuleFileNameW(nullptr, exe, MAX_PATH)) icon = exe;
            }
            if (!icon.empty())
            {
                ::RegSetValueExW(h, L"IconUri", 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(icon.c_str()),
                                 static_cast<DWORD>((icon.size() + 1) * sizeof(wchar_t)));
            }
            // Bildirim kartındaki simge zemini: marka siyah-beyaz, koyu zemin uygun.
            const DWORD color = 0xFF101014;   // COLORREF(BGR) = RGB(20,16,16)
            ::RegSetValueExW(h, L"IconBackgroundColor", 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&color), sizeof(color));
            ::RegCloseKey(h);
        }

        // ---- Başlat menüsü kısayolu -------------------------------------------
        // WinRT bildirimleri düğmeye basıldığında uygulamayı "şu AUMID'li
        // kısayol üzerinden başlat" komutuyla devreye sokar. Kısayol yoksa bu
        // yol çalışmaz; bu yüzden kayıt tek başına yetmez, kısayol şarttır.
        std::wstring ShortcutPath()
        {
            // Buffer 1024 olmalı: SHGetFolderPathW uzunluğu ÖĞRENMEZ, çağıranın
            // yazdığı yere doğrudan yazar. MAX_PATH (260) taşma yaratır ve
            // kurulum sessizce hiç yapılmaz.
            wchar_t dir[1024] = {};
            if (SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, dir) != S_OK)
                return {};
            return std::wstring(dir) + L"\\TENGRI.lnk";
        }

        // Kısayolun simge kaynağı: marka .ico'sı varsa o, yoksa exe'nin kendisi.
        // Görev çubuğu, AUMID ile gruplanmış düğmelerin simgesini bu kısayoldan
        // alır; simge ayrı bir dosyada olduğunda exe taşındığında da bozulmaz.
        std::wstring ShortcutIconSource()
        {
            if (!g_iconFile.empty()) return g_iconFile;
            wchar_t exe[MAX_PATH] = {};
            if (::GetModuleFileNameW(nullptr, exe, MAX_PATH)) return exe;
            return {};
        }

        // Kısayolun taşıdığı uygulama kimliğini okur.
        //
        // Burada ve WriteShortcutAumid'de CoInitializeEx ÇAĞRILMAZ. WinRT
        // başlatma (init_apartment) iş parçacığını çoklu kullanim (MTA) moduna
        // alır; ardından CoInitializeEx(COINIT_APARTMENTTHREADED) çağrılırsa
        // RPC_E_CHANGED_MODE döner ve WinRT'nin kullanımı bozulur. Bu hata
        // yapılmıştı ve tüm Windows bildirimlerini sessizce kapatıyordu.
        bool ReadShortcutAumid(const std::wstring& path, wchar_t* out)
        {
            IShellLinkW*  sl = nullptr;
            IPersistFile* pf = nullptr;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&sl))))
                return false;

            bool ok = false;
            if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) &&
                SUCCEEDED(pf->Load(path.c_str(), STGM_READ)))
            {
                IPropertyStore* store = nullptr;
                if (SUCCEEDED(sl->QueryInterface(__uuidof(IPropertyStore), (void**)&store)))
                {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &v)) &&
                        v.vt == VT_LPWSTR && v.pwszVal)
                    {
                        wcsncpy_s(out, 128, v.pwszVal, _TRUNCATE);
                        ok = true;
                    }
                    // GetValue dizeyi AYIRIR; burada Clear çağırmak doğru ve
                    // gerekli (yazma yolundan farklı).
                    PropVariantClear(&v);
                    store->Release();
                }
            }

            if (pf) pf->Release();
            sl->Release();
            return ok;
        }

        // Kısayolun simge kaynağını ve görevini okur. Görev sıfırdan başlar;
        // kısayol hiç simge taşımıyorsa out boş kalır ve false döner.
        bool ReadShortcutIcon(const std::wstring& path, wchar_t* out, int cap, int& index)
        {
            IShellLinkW*  sl = nullptr;
            IPersistFile* pf = nullptr;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&sl))))
                return false;

            bool ok = false;
            if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) &&
                SUCCEEDED(pf->Load(path.c_str(), STGM_READ)))
            {
                *out = L'\0';
                if (SUCCEEDED(sl->GetIconLocation(out, cap, &index)) && *out)
                    ok = true;
            }

            if (pf) pf->Release();
            sl->Release();
            return ok;
        }

        // Kimliği KAYITLI kısayol dosyasına yazar.
        //
        // Dosya yolu alır, çünkü kimlik ancak dosya diskteyken yazılır. Aynı
        // nesne üzerinde önce SetValue denemek çalışmıyor: üç adım da S_OK
        // dönmesine rağmen dosyada hiçbir iz oluşmuyor.
        bool WriteShortcutAumid(const std::wstring& path, const wchar_t* aumid)
        {
            IShellLinkW*  sl = nullptr;
            IPersistFile* pf = nullptr;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&sl))))
                return false;
            if (FAILED(sl->QueryInterface(IID_PPV_ARGS(&pf))) ||
                FAILED(pf->Load(path.c_str(), STGM_READWRITE)))
            {
                if (pf) pf->Release();
                sl->Release();
                return false;
            }

            bool ok = false;
            IPropertyStore* store = nullptr;
            if (SUCCEEDED(sl->QueryInterface(__uuidof(IPropertyStore), (void**)&store)))
            {
                PROPVARIANT v;
                PropVariantInit(&v);
                v.vt      = VT_LPWSTR;
                v.pwszVal = const_cast<LPWSTR>(aumid);

                // PropVariantClear BURADA ÇAĞRILMAZ: pwszVal bir dize literaline
                // işaret eder ve Clear onu serbest bırakıp yığını bozardı. Bu,
                // sürecin commit'ten sonra 0xC0000374 (heap corruption) ile
                // çökmesine yol açıyordu.
                HRESULT hr = store->SetValue(PKEY_AppUserModel_ID, v);
                if (SUCCEEDED(hr))
                    hr = store->Commit();
                ok = SUCCEEDED(hr);
                store->Release();
            }

            // Aynı dosyaya geri yaz.
            if (ok)
                ok = SUCCEEDED(pf->Save(path.c_str(), TRUE));

            pf->Release();
            sl->Release();
            return ok;
        }

        bool ShortcutNeedsWrite()
        {
            const std::wstring p = ShortcutPath();
            if (p.empty()) return false;
            if (::GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return true;

            // Dosya var ama AUMID yanlışsa da yazılmalı: eski bir sürümden ya da
            // elle bozulmuş olabilir.
            wchar_t got[128] = {};
            if (!ReadShortcutAumid(p, got)) return true;
            if (wcscmp(got, kAumid) != 0) return true;

            // Eski sürümlerin yazdığı hatalı simge görevi (101) düzeltilsin diye
            // simge kaynağı da karşılaştırılır; aksi halde kısayol hiç
            // yenilenmez ve görev çubuğu jenerik simgede kalır.
            const std::wstring want = ShortcutIconSource();
            wchar_t iconGot[MAX_PATH] = {};
            int idx = -1;
            if (!want.empty() &&
                ReadShortcutIcon(p, iconGot, MAX_PATH, idx) &&
                (idx != 0 || _wcsicmp(iconGot, want.c_str()) != 0))
                return true;
            return false;
        }

        void WriteShortcut()
        {
            const std::wstring p = ShortcutPath();
            if (p.empty()) return;

            wchar_t exe[MAX_PATH] = {};
            if (!::GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;

            IShellLinkW*  sl = nullptr;
            IPersistFile* pf = nullptr;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&sl))))
                return;

            sl->SetPath(exe);
            wchar_t dir[MAX_PATH] = {};
            if (wcslen(exe))
            {
                wcsncpy_s(dir, exe, _TRUNCATE);
                if (wchar_t* slash = wcsrchr(dir, L'\\')) *slash = L'\0';
            }
            sl->SetWorkingDirectory(dir);
            sl->SetDescription(L"TENGRI - Sistem Optimize Edici");

            // Simge kaynağı olarak marka .ico'sı kullanılır; olmazsa exe'nin
            // kendisine düşülür.
            //
            // SetIconLocation'ın ikinci parametresi sıfırdan başlayan GÖREV
            // (index), kaynak KİMLİĞİ değildir. exe'de tek simge olduğundan
            // doğru değer 0'dır. Daha önce kIconId (101) verilmişti: Explorer
            // o indeksde simge bulamayınca kısayolu simgesiz sayıyor, AUMID
            // üzerinden gruplanan görev çubuğu düğmesinde jenerik Windows
            // simgesi çiziyordu.
            const std::wstring iconSrc = ShortcutIconSource();
            if (!iconSrc.empty())
                sl->SetIconLocation(iconSrc.c_str(), 0);

            bool saved = false;
            if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))))
            {
                saved = SUCCEEDED(pf->Save(p.c_str(), TRUE));
                pf->Release();
            }
            sl->Release();

            // Kimlik ikinci bir geçişte yazılır: dosya önce diske çıkar, sonra
            // yeniden açılır ve özelliği güncellenir.
            g_installedShortcut = (saved && WriteShortcutAumid(p, kAumid));
        }

        bool ShouldSend(Level lvl)
        {
            if (!toastsEnabled || !g_ready) return false;
            if (quietMode && lvl != Level::Error && lvl != Level::Warning) return false;
            return true;
        }

        void PostXml(Level lvl, const char* title, const char* body,
                     Target target, const std::vector<Button>& buttons,
                     const wchar_t* tag = nullptr)
        {
            if (!ShouldSend(lvl)) return;

            const std::wstring wt = XmlEscape(Utf8(title).c_str());
            const std::wstring wb = XmlEscape(Utf8(body).c_str());

            // Marka resmi: kartın solundaki uygulama logosu. AUMID'deki IconUri
            // yalnızca Eylem Merkezi'nde kullanılıyor; kartın kendisinde görünmesi
            // için appLogoOverride gerekiyor.
            std::wstring logo;
            if (!g_iconFile.empty())
            {
                const std::wstring uri = XmlEscape(IconUri(g_iconFile).c_str());
                logo = L"<image placement=\"appLogoOverride\" src=\"" + uri + L"\"/>";
            }

            // Süreç bildirimleri sessizdir: aynı iş onlarca kez yinelenirse her
            // biri ses çalarak kendi başına bir gürültü olur.
            const bool silent = tag && *tag;

            std::wstring xml =
                L"<toast launch=\"cmd:1\" activationType=\"foreground\" group=\"tengri\">"
                L"<visual><binding template=\"ToastGeneric\">"
                // Başlık koyu, gövde normal: Windows'un kendi tipografi basamakları
                // kullanılır, kart böylece iki satırlık düz metin yığını gibi
                // görünmüyor.
                L"<text hint-style=\"subheader\" hint-wrap=\"true\">" + wt + L"</text>"
                // hint-maxLines olmadan gövde iki satırda kesilip üç noktaya
                // düşüyordu; uzun tarama sonuçları okunamaz hale geliyordu.
                L"<text hint-style=\"body\" hint-wrap=\"true\" hint-maxLines=\"3\">" + wb + L"</text>" +
                logo +
                L"</binding></visual><actions>";

            // İlk düğmeye verilen inline, tüm düğmeleri tek satıra dizer.
            // Varsayılan dizilim düğme başına tam genişlik satır açtığı için
            // üç düğmeli kart gereksiz yere uzuyor.
            bool first = true;
            for (const Button& b : buttons)
            {
                if (b.action == Action::Dismiss || b.action == Action::None) continue;

                int cmd = 0;
                switch (b.action)
                {
                case Action::View:     cmd = kCmdView;     break;
                case Action::Undo:     cmd = kCmdUndo;     break;
                case Action::Retry:    cmd = kCmdRetry;    break;
                case Action::Download: cmd = kCmdDownload; break;
                default: continue;
                }

                // arguments önce komut, sonra hedef sekme. Hedef olmayan
                // düğmelerde -1 gider; alıcı taraf geçersiz değeri yok sayar.
                xml += L"<action ";
                if (first) { xml += L"placement=\"inline\" "; first = false; }
                xml += L"content=\"" + XmlEscape(Utf8(b.label).c_str()) +
                       L"\" arguments=\"" + std::to_wstring(cmd) + L":" +
                       std::to_wstring(static_cast<int>(target)) +
                       L"\" activationType=\"foreground\"/>";
            }

            // Sistem düğmesi uygulamayı açmaz; her bildirimde bulunması
            // kullanıcının "bunu bir daha gösterme" isteğini tek tıkla yerine
            // getirir.
            xml += L"<action ";
            if (first) xml += L"placement=\"inline\" ";
            xml += L"content=\"" + XmlEscape(Utf8(L(NotifBtnOk)).c_str()) +
                   L"\" arguments=\"dismiss\" activationType=\"system\"/>";

            xml += L"</actions>";
            if (silent) xml += L"<audio silent=\"true\"/>";
            xml += L"</toast>";

            try
            {
                using namespace winrt::Windows::Data::Xml::Dom;
                using namespace winrt::Windows::UI::Notifications;

                const auto doc = XmlDocument();
                doc.LoadXml(xml);

                auto note = ToastNotification(doc);
                note.Group(L"tengri");
                // Etiket, aynı işin sonraki bildirimlerinin eskisinin ÜSTÜNE
                // yazılmasını sağlar: ilerleme bildirimi yığılmaz, tek kart yer
                // değiştirir.
                if (tag && *tag) note.Tag(tag);

                ToastNotificationManager::CreateToastNotifier(kAumid).Show(note);
            }
            catch (...)
            {
                // Bildirim gönderilememesi iş akışını bozmamalıdır.
            }
        }
    }

    bool AllowInApp(Level lvl)
    {
        if (quietMode)
            return lvl == Level::Error || lvl == Level::Warning;
        return true;
    }

    void Init()
    {
        LoadPrefs();

        // WinRT başlatma. Bu düğme de std::thread'ler başlatmadan ÖNCE
        // çağrılmalıdır; sonradan kurulamaz.
        try
        {
            winrt::init_apartment();
            g_ready = true;
        }
        catch (...)
        {
            // WinRT kurulamazsa uygulama çalışmaya devam eder, yalnızca
            // Windows bildirimleri devre dışı kalır.
            g_ready = false;
        }

        if (!g_ready) return;

        ::SetCurrentProcessExplicitAppUserModelID(kAumid);

        // Bildirim kartının resmi, exe'nin kendi simge kaynağından üretilir.
        // Başarısız olursa g_iconFile boş kalır ve kart simgesiz ama çalışır.
        if (EnsureBrandIcon()) g_iconFile = BrandIconPath();

        RegisterAumid();

        // Bir kez yaz, sonra bir daha dokunma. Kısayol veya kayıt silinirse
        // yeniden kurulur; bu, "kalıcı" seçiminin sınırı: kalıcı olan kendiliğinden
        // silinmemesi, elle silinmesine dayanıklı değil.
        if (ShortcutNeedsWrite())
            WriteShortcut();
    }

    void Shutdown()
    {
        if (g_ready)
        {
            try { winrt::uninit_apartment(); }
            catch (...) {}
            g_ready = false;
        }
    }

    void Save() { SavePrefs(); }
    void Load() { LoadPrefs(); }

    void Post(Level lvl, const char* title, const char* body,
              Target target, const std::vector<Button>& buttons)
    {
        PostXml(lvl, title, body, target, buttons);
    }

    void PostProgress(const char* title, const char* body)
    {
        // Süreç bildirimi bir bilgi düzeyindedir; sessiz modda susturulur.
        // Etiketlidir: aynı işin sonraki adımı kartı yenilemez, üstüne yazar.
        PostXml(Level::Info, title, body, TargetNone, {}, L"tengri-progress");
    }

    int ParseLaunch(const wchar_t* args, int* target)
    {
        if (target) *target = TargetNone;
        if (!args) return 0;

        const size_t n = ::wcsnlen(args, 64);
        const size_t p = ::wcsnlen(kLaunchPrefix, 64);
        if (n <= p || ::wcsncmp(args, kLaunchPrefix, p) != 0)
            return 0;

        wchar_t* end = nullptr;
        const int cmd = (int)::wcstol(args + p, &end, 10);
        if (end == args + p)
            return 0;
        if (cmd != CmdView && cmd != CmdUndo && cmd != CmdRetry && cmd != CmdDownload)
            return 0;   // tanınmayan kod: sessizce yok say

        // "cmd:N:hedef" biçiminde ikinci alan hedef sekmedir; yoksa -1 kalır.
        if (*end == L':' && target)
        {
            const int t = (int)::wcstol(end + 1, &end, 10);
            if (t >= TargetDashboard && t <= TargetSettings)
                *target = t;
        }
        return cmd;
    }
}