#include "notify.hpp"
#include "brand.hpp"
#include "lang.hpp"
#include <windows.h>
#include <shlobj.h>
#include <propsys.h>
#include <propkey.h>    // PKEY_AppUserModel_ID
#include <string>
#include <vector>

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

        bool g_ready = false;   // WinRT başlatıldı mı

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

            const wchar_t* display = L"TENGRI";
            ::RegSetValueExW(h, L"DisplayName", 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(display),
                             static_cast<DWORD>((wcslen(display) + 1) * sizeof(wchar_t)));

            wchar_t exe[MAX_PATH] = {};
            if (::GetModuleFileNameW(nullptr, exe, MAX_PATH))
            {
                ::RegSetValueExW(h, L"IconUri", 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(exe),
                                 static_cast<DWORD>((wcslen(exe) + 1) * sizeof(wchar_t)));
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
            return wcscmp(got, kAumid) != 0;
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

            // Simge kaynağın 101 kimliğinde. Yol yerine kaynak kimliği yazılır,
            // böylece exe taşınsa bile kısayol eski yolu göstermez.
            sl->SetIconLocation(exe, static_cast<int>(brand::kIconId));

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
                     Target target, const std::vector<Button>& buttons)
        {
            if (!ShouldSend(lvl)) return;

            const std::wstring wt = XmlEscape(Utf8(title).c_str());
            const std::wstring wb = XmlEscape(Utf8(body).c_str());

            std::wstring xml =
                L"<toast launch=\"cmd:1\" activationType=\"foreground\">"
                L"<visual><binding template=\"ToastGeneric\">"
                L"<text>" + wt + L"</text>"
                L"<text>" + wb + L"</text>"
                L"</binding></visual><actions>";

            for (const Button& b : buttons)
            {
                if (b.action == Action::Dismiss || b.action == Action::None) continue;

                int cmd = 0;
                switch (b.action)
                {
                case Action::View:  cmd = kCmdView;    break;
                case Action::Undo:  cmd = kCmdUndo;    break;
                case Action::Retry: cmd = kCmdRetry;   break;
                default: continue;
                }

                // arguments önce komut, sonra hedef sekme. Hedef olmayan
                // düğmelerde -1 gider; alıcı taraf geçersiz değeri yok sayar.
                xml += L"<action content=\"" + XmlEscape(Utf8(b.label).c_str()) +
                       L"\" arguments=\"" + std::to_wstring(cmd) + L":" +
                       std::to_wstring(static_cast<int>(target)) +
                       L"\" activationType=\"foreground\"/>";
            }

            // Sistem düğmesi uygulamayı açmaz; her bildirimde bulunması
            // kullanıcının "bunu bir daha gösterme" isteğini tek tıkla yerine
            // getirir.
            xml += L"<action content=\"" + XmlEscape(Utf8(L(NotifBtnOk)).c_str()) +
                   L"\" arguments=\"dismiss\" activationType=\"system\"/>";

            xml += L"</actions></toast>";

            try
            {
                using namespace winrt::Windows::Data::Xml::Dom;
                using namespace winrt::Windows::UI::Notifications;

                const auto doc = XmlDocument();
                doc.LoadXml(xml);
                ToastNotificationManager::CreateToastNotifier(kAumid)
                    .Show(ToastNotification(doc));
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
        PostXml(Level::Info, title, body, TargetNone, {});
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
        if (cmd != CmdView && cmd != CmdUndo && cmd != CmdRetry)
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