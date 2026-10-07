#include "tray.hpp"
#include <shellapi.h>
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
    }

    bool Init(HWND hwnd, HINSTANCE instance)
    {
        g_nid = { sizeof(g_nid) };
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;

        // Simge kaynaktan, görev çubuğu ve pencere ile aynı artwork olsun diye.
        // instance ZORUNLU: modül NULL iken LoadImage kimliği sistem kaynağı
        // sanar. LR_DEFAULTSIZE sistem ikon ölçülerini ister ve LR_SHARED
        // verilmediği için bu modülün sahibi olduğu, kapanışta yok edilebilen bir
        // kopya döndürür. (LR_DEFAULTCOPY SDK başlıklarında yok.)
        g_icon = (HICON)::LoadImageW(instance, MAKEINTRESOURCEW(brand::kIconId), IMAGE_ICON,
                                     0, 0, LR_DEFAULTSIZE);

        // Kaynak yine de okunamazsa kabuk JENERIK simge gösterir; sarı ünlem
        // üçgeni değil. LoadImage başarısız olup çöp bir tutamak döndürebilir,
        // o yüzden yedek yalnızca null için devreye girer.
        if (!g_icon)
            g_icon = (HICON)::LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);

        Add();
        g_have = g_added;
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
            HMENU menu = ::CreatePopupMenu();
            if (menu)
            {
                ::AppendMenuW(menu, MF_STRING, 1, brand::kTrayOpen);
                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(menu, MF_STRING, 2, L"Exit");
                ::SetForegroundWindow(g_nid.hWnd);
                const UINT cmd = (UINT)::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                                        pt.x, pt.y, 0, g_nid.hWnd, nullptr);
                ::DestroyMenu(menu);
                if (cmd == 1)
                {
                    if (restore) *restore = true;
                    ::PostMessageW(g_nid.hWnd, kRestoreMsg, 0, 0);
                }
                else if (cmd == 2) { ::PostQuitMessage(0); }
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
