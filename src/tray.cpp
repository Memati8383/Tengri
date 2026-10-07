#include "tray.hpp"
#include <shellapi.h>
#include <string>
#include "app.hpp"
#include "brand.hpp"
#include "core/lang.hpp"

namespace tray
{
    // main.cpp bu değeri kendi mesaj döngüsünde dinlemek için dışarıdan okumak zorunda,
    // bu yüzden anonim namespace'in dışında duruyor.
    UINT kRestoreMsg = WM_APP + 1;

    namespace
    {
        constexpr UINT  WM_TRAY = WM_APP + 1;
        NOTIFYICONDATAW g_nid   = {};
        bool            g_added = false;
        bool            g_have  = false;   // shell accepted us at least once
        bool            g_enabled = false;
        HICON           g_icon = nullptr;

        // Explorer yeniden başladığında (çökme, shell_experience değişimi,
        // logoff/on) tüm bildirim simgeleri sürece haber verilmeden yok edilir.
        // Kabuk, pencereye yinelenen bir özel mesaj numarasıyla bunu bildirir;
        // numara oturumdan bağımsız olduğundan RegisterWindowMessageW ile alınır.
        UINT g_taskbarCreated = 0;

        void Add()
        {
            if (g_added || !g_icon) return;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAY;
            g_nid.hIcon = g_icon;
            wcscpy_s(g_nid.szTip, brand::kTrayTip);
            g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) ? true : false;
        }

        // "Simge tepsi çubuğunda gizli olabilir" ipucu.
        //
        // NIF_INFO balonları, kullanıcının "uygulama nereye gitti?" sorusunu tek
        // tıkla yanıtlar. NIF_SHOWTIP gerekir: bildirim ayarlarında "balon
        // göster" kapatılmışsa bile başlık/ipucu metni Eylem Merkezi'ne düşer.
        //
        // Yalnızca bir kez gösterilir; her küçültmede tekrarlanırsa rahatsız
        // edici olur ve kullanıcı kapatmayı öğrenir, o da tam olarak çözülecek
        // şeydir.
        bool g_hintShown = false;


        void Remove()
        {
            if (!g_added) return;
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_added = false;
        }

        // Menü alanları wchar_t ister, L() makrosu UTF-8 veriyor.
        std::wstring W(const char* utf8)
        {
            if (!utf8 || !*utf8) return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
            if (n <= 1) return {};
            std::wstring out((size_t)n - 1, L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], n);
            return out;
        }

        // Sağ tık menüsü her açılışta yeniden kurulur: işaretli düğmeler ve
        // "Geri al"ın erişilebilirliği menü kapanınca bilinen bir değerle
        // güncellenemez, çünkü kabuk seçimi WM_COMMAND olarak değil
        // TPM_RETURNCMD ile tek seferde döner.
        HMENU BuildMenu()
        {
            HMENU menu = ::CreatePopupMenu();
            if (!menu) return nullptr;

            // Başlık satırı: tepside hangi sürümün durduğunu gösterir;
            // Eylem Merkezi kartlarının başlıkları da sürüm taşıdığı için
            // kullanıcı aynı sürümün iki yüzünü karıştırmaz.
            const std::wstring header = std::wstring(brand::kName) + L"  v" + W(brand::kVersion);
            ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, header.c_str());
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

            ::AppendMenuW(menu, MF_STRING, app::TrayCmdOpen, W(L(TrayOpen)).c_str());

            // Sayfalar arayüzdeki sekme sırasıyla aynı; sırayı tek yer
            // (app.cpp) biliyor, menü yalnızca soruyor.
            HMENU pages = ::CreatePopupMenu();
            if (pages)
            {
                const int count = app::TrayPageCount();
                for (int i = 0; i < count; ++i)
                    ::AppendMenuW(pages, MF_STRING, app::TrayCmdGoto + i, W(app::TrayPageLabel(i)).c_str());
                ::AppendMenuW(menu, MF_POPUP, (UINT_PTR)pages, W(L(TrayGoTo)).c_str());
            }

            ::AppendMenuW(menu, app::TrayCanUndo() ? MF_STRING : MF_GRAYED,
                          app::TrayCmdUndo, W(L(UndoChanges)).c_str());
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

            ::AppendMenuW(menu, MF_STRING | (app::TrayNotificationsOn() ? MF_CHECKED : 0),
                          app::TrayCmdToasts, W(L(WindowsNotifications)).c_str());
            ::AppendMenuW(menu, MF_STRING | (app::TrayQuietOn() ? MF_CHECKED : 0),
                          app::TrayCmdQuiet, W(L(QuietMode)).c_str());
            ::AppendMenuW(menu, MF_STRING | (app::TrayStartupOn() ? MF_CHECKED : 0),
                          app::TrayCmdStartup, W(L(LaunchStartup)).c_str());
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

            ::AppendMenuW(menu, MF_STRING, app::TrayCmdSettings, W(L(Settings)).c_str());
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(menu, MF_STRING, app::TrayCmdExit, W(L(TrayExit)).c_str());
            return menu;
        }

        // Seçim iki yoldan birine gider: pencere istemeyen komutlar doğrudan
        // uygulama tarafına, isteyenler önce geri getirme mesajından. Geri
        // getirme main.cpp'nin elindeki tek ShowWindow yoludur (SW_HIDE ile
        // gizlenen pencerede SW_RESTORE güvenilir değil), bu yüzden burada
        // kopyalanmaz.
        void ApplyCommand(UINT cmd, bool* restore)
        {
            if (cmd == 0) return;   // menü boşuna kapatıldı
            if (cmd == (UINT)app::TrayCmdExit) { ::PostQuitMessage(0); return; }

            const bool showsWindow =
                cmd == (UINT)app::TrayCmdUndo     ||
                cmd == (UINT)app::TrayCmdSettings ||
                cmd >= (UINT)app::TrayCmdGoto;

            if (cmd != (UINT)app::TrayCmdOpen)
                app::HandleTrayCommand((int)cmd);

            if (cmd == (UINT)app::TrayCmdOpen || showsWindow)
            {
                if (restore) *restore = true;
                ::PostMessageW(g_nid.hWnd, kRestoreMsg, 0, 0);
            }
        }
    }

    bool Init(HWND hwnd, HINSTANCE instance)
    {
        g_nid = { sizeof(g_nid) };
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;

        // Explorer'ın yeniden başlama bildirisini dinlenebilir kıl: numara
        // makineden makineye değiştiği için sabit yazılamaz.
        g_taskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");

        // Simge kaynaktan, görev çubuğu ve pencere ile aynı artwork olsun diye.
        // instance ZORUNLU: modül NULL iken LoadImage kimliği sistem kaynağı
        // sanar. Ölçü açıkça SM_CXSMICON: tepsi kabuk küçültülmüş bir kareye
        // sığdırıldığı için 32 px'lik kopya (LR_DEFAULTSIZE'un verdiği) bulanık
        // çiziliyor. LR_SHARED verilmez, çünkü bu simge kapanışta DestroyIcon ile
        // yok ediliyor. (LR_DEFAULTCOPY SDK başlıklarında yok.)
        g_icon = (HICON)::LoadImageW(instance, MAKEINTRESOURCEW(brand::kIconId), IMAGE_ICON,
                                     ::GetSystemMetrics(SM_CXSMICON),
                                     ::GetSystemMetrics(SM_CYSMICON),
                                     LR_DEFAULTCOLOR);

        // Kaynak yine de okunamazsa kabuk JENERIK simge gösterir; sarı ünlem
        // üçgeni değil. LoadImage başarısız olup çöp bir tutamak döndürebilir,
        // o yüzden yedek yalnızca null için devreye girer.
        if (!g_icon)
            g_icon = (HICON)::LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);

        Add();
        g_have = g_added;
        // Açılışta arayüz varsayılan olarak tepsiyi açık tutar (app.cpp g_tray=true).
        // Bu bayrak daha önce hiç set edilmediği için Tick() bakım döngüsü ölü kalıyor,
        // dolayısıyla Explorer yeniden başladığında simge bir daha geri gelemiyordu.
        g_enabled = g_added;
        return g_added;
    }

    void ShowHintBalloon()
    {
        if (g_hintShown || !g_added) return;
        g_hintShown = true;

        NOTIFYICONDATAW nid = g_nid;
        nid.uFlags = NIF_INFO | NIF_SHOWTIP;
        wcscpy_s(nid.szInfoTitle, 128, brand::kName);
        // L() bir makro: lang::Get(S::key) çağırır ve const char* döndürür. Balon
            // alanları wchar_t dizisi olduğu için dar karakterden çevrilir.
            wchar_t info[256] = {};
            ::MultiByteToWideChar(CP_UTF8, 0, L(TrayHintBalloon), -1, info, 256);
            wcscpy_s(nid.szInfo, 256, info);
        // HINT_SHOWTIP: balon tepsi alanına bağlanır, kullanıcı oradan yönlendirilir.
        nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    bool HandleMessage(UINT msg, LPARAM lparam, bool* restore)
    {
        // Explorer yeniden başladı: eski bildirim kaydı artık geçersiz. g_added'i
        // sıfırla ve (tepsi açıksa) simgeyi hemen yeniden ekle. Bu olmazsa simge
        // yalnızca pencere tarafında kalır, tepside bir daha görünmez.
        if (g_taskbarCreated && msg == g_taskbarCreated)
        {
            g_added = false;
            if (g_enabled) Add();
            return true;
        }

        if (msg != WM_TRAY) return false;

        switch (LOWORD(lparam))
        {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            if (restore) *restore = true;
            // Kareler atlanırken ana döngünün uyanabilmesi için ayrıca gönderilir.
            ::PostMessageW(g_nid.hWnd, kRestoreMsg, 0, 0);
            return true;
        case WM_RBUTTONUP:
        {
            POINT pt;
            ::GetCursorPos(&pt);
            HMENU menu = BuildMenu();
            if (menu)
            {
                // İstemci öne getirilmezse kabuk menüyü anında kapatıyor.
                ::SetForegroundWindow(g_nid.hWnd);
                // "Pencereyi aç" kalın çizilir: tepsinin birincil eylemi bu.
                ::SetMenuDefaultItem(menu, app::TrayCmdOpen, TRUE);
                const UINT cmd = (UINT)::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                                        pt.x, pt.y, 0, g_nid.hWnd, nullptr);
                ::DestroyMenu(menu);
                // Klasik kabuk düzeltmesi: TrackPopupMenu döndükten sonra pencere
                // menüyü hâlâ açık sanabiliyor ve ilk tıklamayı yutuyor.
                ::PostMessageW(g_nid.hWnd, WM_NULL, 0, 0);
                ApplyCommand(cmd, restore);
            }
            return true;
        }
        default:
            return true;
        }
    }

    bool IsAvailable() { return g_have; }

    void SetEnabled(bool enabled)
    {
        g_enabled = enabled;
        if (enabled) Add(); else Remove();
    }

    void Tick()
    {
        // Explorer'ın yeniden başlaması ikonu yok eder; her karede NIM_ADD
        // gereksiz yük olurdu, bu yüzden ekleme yalnızca saniyede bir yeniden denenir.
        static double last = 0.0;
        if (!g_enabled || !g_have || !g_icon) return;

        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        const double t = (double)now.QuadPart / freq.QuadPart;
        if (t - last < 1.0) return;
        last = t;
        if (!g_added) Add();
    }

    void Shutdown()
    {
        Remove();
        if (g_icon) { ::DestroyIcon(g_icon); g_icon = nullptr; }
    }
}
