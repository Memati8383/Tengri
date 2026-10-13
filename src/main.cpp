// Tek renkli arayüz: Win32 penceresi üzerine Direct3D 11 ile çizilir.
// Burada yalnızca pencere ve grafik yaşam döngüsü vardır; ürünün adı, simgesi ve
// sürümü gibi kimlik bilgileri tek yerden okunur.
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "app.hpp"
#include "gui/theme.hpp"
#include "gui/logo.hpp"
#include "tray.hpp"
#include "brand.hpp"
#include "core/elevate.hpp"
#include "core/notify.hpp"
#include "core/update.hpp"
#include "core/sample_data.hpp"
#include "core/island.hpp"
#include <d3d11.h>
#include <dwmapi.h>
#include "core/selfgate.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")

static ID3D11Device*           g_device    = nullptr;
static ID3D11DeviceContext*    g_context   = nullptr;
static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv       = nullptr;
static bool                    g_occluded  = false;
static UINT                    g_resizeW = 0, g_resizeH = 0;
static float                   g_initialScale = 1.0f;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Köşe yuvarlatma isteği, çağrı başarılı dönse de bazı Windows 10 derlemelerinde
// yok sayılır; yani dönüş koduna bakarak yuvarlaklığın gerçekten uygulandığı
// anlaşılamaz. Bu yüzden ölçüt, derleme numarasıdır.
static bool IsWindows11OrGreater()
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi = { sizeof(vi) };
    if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = (RtlGetVersionFn)(void*)::GetProcAddress(ntdll, "RtlGetVersion"))
            fn(&vi);
    return vi.dwBuildNumber >= 22000;
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back)
    {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g_swapChain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) // donanım sürücüsü yok: yazılım çizim yoluna düş
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swapChain, &g_device, &level, &g_context);
    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context)   { g_context->Release();   g_context   = nullptr; }
    if (g_device)    { g_device->Release();    g_device    = nullptr; }
}

static bool g_trayRestore = false;

// Tepsi'den geri dönüş. Küçültme yolu pencereyi hem simge durumuna küçültüp hem
// gizliyor, kapatma yolu ise yalnızca gizliyor. SW_RESTORE gizli ama küçülmüş
// olmayan bir pencerede güvenilir olmadığı için iki durum ayrılıyor.
static void ShowFromTray(HWND hwnd)
{
    ::ShowWindow(hwnd, ::IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    ::SetForegroundWindow(hwnd);
}

// ---- çökme kaydı ------------------------------------------------------------
// Yakalanmayan bir istisna (0xC0000005 erişim ihlali en sık olanı) uygulamayı
// sessizce kapatır: kullanıcı yalnızca "siyah ekran, sonra kapandı" görür,
// nedenini ise Windows Olay Görüntüleyicisi'nde sadece ham bir adres olarak
// bulur. Burada RIP, hata kodu ve yığındaki dönüş adresleri TEMP'e yazılır;
// exe RVA'sı bir sonraki derlemenin haritasıyla eşleştirilip gerçek satır
// bulunabilir.
static void WriteCrashReport(EXCEPTION_POINTERS* ep)
{
    wchar_t path[MAX_PATH] = {};
    if (!::GetTempPathW(MAX_PATH, path)) return;
    wcscat_s(path, L"tengri_crash.log");

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w, ccs=UTF-8") != 0 || !f) return;

    EXCEPTION_RECORD* r  = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord : nullptr;
    CONTEXT*           cx = (ep && ep->ContextRecord) ? ep->ContextRecord : nullptr;
    if (!cx) return;
    const unsigned long long rip = (unsigned long long)cx->Rip;

    // exe taban adresi: RVA = RIP - taban
    HMODULE self = ::GetModuleHandleW(nullptr);
    const unsigned long long base = (unsigned long long)self;

    fwprintf(f, L"TENGRI crash\n");
    fwprintf(f, L"exception code : 0x%08X\n", r ? r->ExceptionCode : 0);
    fwprintf(f, L"exception flags: 0x%08X\n", r ? r->ExceptionFlags : 0);
    if (r && r->ExceptionRecord)
        fwprintf(f, L"inner          : 0x%08X\n", r->ExceptionRecord->ExceptionCode);
    // ExceptionInformation sayısı bir bit alanıdır; yalnızca anlamlı olan kodda
    // okunur (erişim ihlali / ihlal-dışı): [0]=okuma/yazma, [1]=adres.
    if (r && r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        fwprintf(f, L"fault          : %s, address 0x%016llX\n",
                 r->ExceptionInformation[0] ? L"WRITE" : L"READ",
                 (unsigned long long)r->ExceptionInformation[1]);
    if (r && r->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION)
        fwprintf(f, L"fault          : illegal instruction 0x%016llX\n",
                 (unsigned long long)r->ExceptionInformation[0]);
    fwprintf(f, L"rip            : 0x%016llX\n", rip);
    fwprintf(f, L"image base     : 0x%016llX\n", base);
    if (self)
        fwprintf(f, L"exe RVA        : 0x%08llX\n", rip - base);

    // Yığın: RBP zinciri. /Oyse /O2'de her zaman geçerli değildir ama çökmeyi
    // daraltmak için elde ne varsa yazar.
    unsigned long long* frame = (unsigned long long*)cx->Rbp;
    const unsigned long long rsp = cx->Rsp;
    fwprintf(f, L"rsp            : 0x%016llX\n", rsp);
    fwprintf(f, L"rbp            : 0x%016llX\n", cx->Rbp);
    for (int i = 0; i < 32 && frame; ++i)
    {
        unsigned long long* slot = frame + 1;
        if ((unsigned long long)slot < rsp) break;
        unsigned long long ret = *slot;
        fwprintf(f, L"  frame[%2d] ret=0x%016llX rva=0x%08llX\n",
                 i, ret, self ? ret - base : 0);
        frame = (unsigned long long*)(*frame);
        if ((unsigned long long)frame < rsp || ((unsigned long long)frame & 7)) break;
    }
    fclose(f);
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    // Yalnızca gerçek erişim ihlali/ihlal-dışı kodlar kaydedilir; işlenmiş
    // hatalarda (SEH kullanılan DLL'ler) gürültü üretmesin.
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0xC0000005 || code == 0xC000001D || code == 0xC0000374 ||
        code == 0xC00000FD || code == 0xC0000139 || code == 0xC0000135 ||
        code == 0xE06D7363)
    {
        WriteCrashReport(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- tek örnek kilidi -------------------------------------------------------
// Windows bildirimlerindeki düğmeler uygulamayı YENİDEN başlatır; Windows'un
// tek yapabildiği budur. Bu exe kendi başına ikinci bir pencere açtığında
// kullanıcı iki TENGRİ görür ve hangisinde olduğunu anlamaz. Bu yüzden
// başlatma komutu taşıyan her örnek, çalışan örneğe komutu iletip kendini
// kapatır.
//
// Tek istisna güncelleme takasından doğan örnektir: o, kapanmakta olan
// örneğe komut göndermemeli; gönderirse kapanan süreçle yarışır ve
// güncellenmiş uygulama hiç açılmaz. Aşağıda kilit beklenir.
static HANDLE g_singleMutex = nullptr;

// Kilit adı exe adına göre üretilir, ürün adına göre değil. Aynı exenin iki
// örneği çakışmaya devam eder (istenen davranış), ama build\TENGRI_shot.exe
// kopyası gerçek TENGRI.exe ile çakışmaz: ekran görüntüsü aracı kullanıcının
// açık uygulamasını kapatmak zorunda kalmaz. Sabit bir ad kullanılsaydı
// geliştirme derlemesi kurulu sürümü çalışırken hiç açılamazdı.
static std::wstring SingleMutexName()
{
    wchar_t exe[MAX_PATH] = {};
    if (!::GetModuleFileNameW(nullptr, exe, MAX_PATH)) return L"Local\\TENGRI_SingleInstance";

    std::wstring leaf(exe);
    const size_t slash = leaf.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        leaf = leaf.substr(slash + 1);

    const size_t dot = leaf.find_last_of(L'.');
    if (dot != std::wstring::npos)
        leaf = leaf.substr(0, dot);

    // Yalnızca harf, rakam, tire ve alt çizgi bırakılır: kernel nesne
    // adlarında ters bölü ve boşluk yasaktır, exe adı isteğe bağlı olarak
    // bunları içerebilir.
    for (wchar_t& c : leaf)
    {
        const bool ok = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                        (c >= L'0' && c <= L'9') || c == L'-' || c == L'_';
        if (!ok) c = L'_';
    }

    return L"Local\\TENGRI_" + leaf;
}

// ---- WM_COPYDATA gondereninin kimligi ---------------------------------------
// WM_COPYDATA, ayni oturumdaki HER surecin bize acabildigi bir kanaldir; UIPI
// yalnizca daha Dusuk bileskelik seviyesindeki gonderenleri engeller. Gonderenin
// kimligi iki adimda belirleniyor: wParam'deki pencerenin pid'si, sonra o pid'in
// bizim exemiz olup olmadigi (selfgate.hpp).
static bool SenderIsOurs(HWND from)
{
    if (!from || !::IsWindow(from)) return false;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(from, &pid);
    return selfgate::IsSelf(pid);
}

// Iletim penceresi: WM_COPYDATA'da gonderen imzasi wParam'deki penceredir. Ikinci
// orneğin kendi adina kayitli bir penceresi olmazsa gonderen ya alicinin kendi
// penceresini (yani alicinin pid'sini) yazar - ki hicbir sey kanitlamaz - ya da
// kimliksiz kaldigi icin reddedilir.
static const wchar_t* kForwardClass = L"TENGRI.Forward";

static HWND CreateForwardWindow(HINSTANCE instance)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = ::DefWindowProcW;
        wc.hInstance     = instance;
        wc.lpszClassName = kForwardClass;
        if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;
        registered = true;
    }
    // HWND_MESSAGE: mesaj-only pencere; ekranda hicbir sey gorunmez.
    return ::CreateWindowExW(0, kForwardClass, L"TENGRI", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, instance, nullptr);
}

static bool ForwardToRunningInstance(HINSTANCE instance, const wchar_t* args)
{
    if (!g_singleMutex)
        return false;

    HWND hwnd = ::FindWindowW(brand::kWindowClass, nullptr);
    if (!hwnd)
        return false;

    HWND self = CreateForwardWindow(instance);
    if (!self)
        return false;

    const std::wstring payload = args ? args : L"";
    const size_t bytes = (payload.size() + 1) * sizeof(wchar_t);
    COPYDATASTRUCT cds = {};
    cds.dwData = WM_USER + 1;          // kendi mesajımız; çakışma imkânsız
    cds.cbData = (DWORD)bytes;
    cds.lpData = (void*)payload.c_str();

    // Zaman aşımı: alıcı bir kare boyunca yanıt vermezse (kilitli, donmuş)
    // gönderen sonsuza kadar beklememeli; kendisi yeni örnek olur.
    DWORD_PTR ok = 0;
    ::SendMessageTimeoutW(hwnd, WM_COPYDATA, (WPARAM)self, (LPARAM)&cds,
                          SMTO_ABORTIFHUNG, 2000, &ok);
    ::DestroyWindow(self);
    return true;
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (tray::HandleMessage(msg, lParam, &g_trayRestore))
        return 0;

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_COPYDATA:
    {
        // Başka bir örnekten gelen bildirim komutu. SenderIsOurs: ayni kullanicinin
        // ayni exe'sinden gelmeyen komut yok sayilir (aciklamasi yukarida).
        const COPYDATASTRUCT* cds = (const COPYDATASTRUCT*)lParam;
        if (cds && cds->dwData == WM_USER + 1 && cds->lpData && cds->cbData >= sizeof(wchar_t)
            && SenderIsOurs((HWND)wParam))
        {
            const std::wstring args((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t) - 1);

            int target = notify::TargetNone;
            const int cmd = notify::ParseLaunch(args.c_str(), &target);
            if (cmd != 0)
            {
                app::HandleNotifyCommand(cmd, target);
                // Pencere tray'de gizliyse komut sessizce çalışıp kaybolmasın:
                // kullanıcı düğmeye bastı, karşılığını görmeli.
                ShowFromTray(hWnd);
            }
            return TRUE;
        }
        return FALSE;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            // Bildirim simgesi çalışıyorsa pencere gizli kalarak yaşamaya devam
            // eder; simge yoksa küçültmenin alışılmış anlamı korunur, aksi hâlde
            // kullanıcı pencereyi tamamen kaybetmiş olur.
            if (app::TrayEnabled() && tray::IsAvailable())
            {
                // Windows 11 yeni tray ikonlarini gizli tasma alanina atabiliyor.
                // Kullanici kuculttugunde pencere kaybolur ama tepside ikon
                // gorunmez; "calismiyor" sanir. Ilk kucultmede bir kez balon
                // gosterilerek ikonun nereye gittigi soylenir.
                tray::ShowHintBalloon();
                ::ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
            return 0;
        }
        g_resizeW = (UINT)LOWORD(lParam);
        g_resizeH = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // ALT ile sistem menüsünü açma
            return 0;
        break;
    case WM_DPICHANGED:
    {
        // Pencere sabit mantıksal boyutta tutulduğu için başka bir ekrana taşındığında
        // hem yeniden ölçeklenir hem de yerleşimin tamamının ifade edildiği ortak
        // ölçek güncellenir. Yazı tipleri yalnızca başlangıçta rasterleştirildiği için
        // yeni ölçek farkı, yazı ölçeği çarpanı üzerinden taşınır.
        RECT* rc = (RECT*)lParam;
        const float s = HIWORD(wParam) / 96.0f;
        const float ratio = s / g_initialScale;
        theme::scale = s;
        // Stil içindeki yazı ölçeği alanı, doğrudan DPI çarpanıdır (küresel yazı
        // ölçeği alanı kullanımdan kaldırıldı). Ölçüler başlangıçta bir kez
        // ölçeklendiği için burada yalnızca değişim oranı uygulanır; baştan
        // ölçeklemek her taşımada metni büyütüp küçültürdü.
        ImGui::GetStyle().FontScaleDpi = ImGui::GetStyle().FontScaleDpi * ratio;
        ImGui::GetStyle().ScaleAllSizes(ratio);
        ::SetWindowPos(hWnd, nullptr, rc->left, rc->top,
                       rc->right - rc->left, rc->bottom - rc->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        // Alt+F4 ve sistem menüsündeki "Kapat" çarpı düğmesiyle aynı yola girer;
        // aksi halde kullanıcı tepside kalması gereken uygulamayı klavyeyle
        // gerçekten kapatmış olurdu.
        app::RequestClose();
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Ayar yuklenmeden once: bundan sonra yapilacak her ad-yuklemesi (gecikmeli
    // baglar dahil) yalnizca System32'den cozulsun. Varsayilan arama sirasi
    // uygulamanin kendi klasoruyle o anki calisma klasorunden baslar ve ikisi de
    // ayni kullanicinin yazabildigi yerler. Olculdu: uygulama klasoruna konan
    // ihracatsiz ayni adli DLL, gecikmeli baglari bayraksiz kosumda devraliyordu
    // (process 0xC0000139 ile sonlaniyordu); bayrakla dortü de SYSTEM32'den acildi.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    // Sessiz kapanmaları teşhis edilebilir kılmak için: yakalanmayan istisna
    // TEMP\tengri_crash.log'a yazılır. İlk satırda (hiçbir pencere açılmadan
    // önce) kurulur, çökme arayüz kurulurken de olsa kaydedilir.
    ::SetUnhandledExceptionFilter(CrashFilter);

    // Yükseltilmiş yeniden başlatma: kullanıcı bir yazan iş istedi, süreç "runas" ile
    // bu exe'yi yüksek yetkiyle yeniden açtı ve bekleyen işi komut satırında taşıdı.
    // Burada hiçbir arayüz oluşturmadan iş uygulanır ve süreç çıkar. Normal açılışta
    // bu bayrak yoktur ve akış normaldir.
    elevate::CacheExecutablePath();

    // Ekran görüntüsü kipi burada, hiçbir bilgi okunmadan önce belirlenir:
    // okunan değerler önbelleklendiği için sonradan değişseydi aynı oturumun
    // yarısı gerçek, yarısı örnek veri çizerekti.
    sample::InitFromEnv();

    // Bildirimden gelen başlatma komutu "cmd:N" biçimindedir. Yükseltilmiş
    // yeniden başlatma bayrağıyla karışmaması için ikisi ayrı ayrı okunur.
    std::wstring launchArgs;
    bool takasYeniden = false;   // güncelleme takasından doğan örnek
    {
        int    argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                if (::wcscmp(argv[i], update::kRelaunchArg) == 0)
                    takasYeniden = true;
                else if (::wcsncmp(argv[i], notify::kLaunchPrefix, 4) == 0)
                {
                    launchArgs = argv[i];
                    break;
                }
            }
        }

        // Tek örnek kilidi. Yükseltilmiş süreç bu kilidi TUTMAZ: o süreç
        // kısa ömürlüdür ve kilidi alırsa ana uygulamanın kilidi tutmasını
        // engeller, dolayısıyla her açılışta yeni örnek sanılır.
        if (!elevate::IsElevated())
        {
            const std::wstring mutexName = SingleMutexName();
            g_singleMutex = ::CreateMutexW(nullptr, TRUE, mutexName.c_str());
            bool alreadyRunning = (::GetLastError() == ERROR_ALREADY_EXISTS);

            // Güncelleme takası: eski örnek kendi kapanışını update::Apply
            // içinde başlattığı için kilit, yeni örnek doğduktan sonra bile
            // bir süre dolu kalabilir. Komutu o kilide göndermek, kapanan
            // sürece gitmek ve güncellenmiş uygulamanın hiç açılmaması
            // demekti. Burada sahiplik BEKLENİR; kapanan süreç kilidi
            // bırakırken terk edilmiş (abandoned) duruma düşürür ve bu da
            // devralma sayılır. Süre dolarsa normal akışa dönülür: gerçekten
            // açık bir örnek varsa komut yine ona iletilir.
            if (alreadyRunning && takasYeniden)
            {
                const DWORD r = ::WaitForSingleObject(g_singleMutex, 5000);
                if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED)
                    alreadyRunning = false;
            }

            if (alreadyRunning)
            {
                // Komut varsa çalışan örneğe ilet, yoksa yalnızca öne getir.
                if (ForwardToRunningInstance(hInstance, launchArgs.empty() ? nullptr : launchArgs.c_str()))
                {
                    LocalFree(argv);
                    return 0;
                }
                // Pencere yoksa kilit sahibi ölmüş demektir; kullanıcı yeni
                // örneği açabilsin diye kilidi bırakıp normal akışa devam edilir.
                ::ReleaseMutex(g_singleMutex);
                ::CloseHandle(g_singleMutex);
                g_singleMutex = nullptr;
            }
        }
    }

    {
        int    argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv)
        {
            elevate::Pending pending;
            if (elevate::DecodeCommandLine(argc, argv, pending))
            {
                // Yalnızca yükseltilmiş bir başlatma bu bayrağı taşır. Yanlışlıkla
                // normal kullanıcı elle --apply yazarsa yazma reddedilir.
                const bool yuksek = elevate::IsElevated();
                LocalFree(argv);
                if (yuksek)
                    return elevate::ApplyOrDelegate(pending) == elevate::Result::Applied ? 0 : 1;
                // Yükseltilmemişse: kullanıcıya yeter, yazma olmadı.
                return 1;
            }
            LocalFree(argv);
        }
    }

    ImGui_ImplWin32_EnableDpiAwareness();

    // WinRT başlatma, iş parçacıkları başlatılmadan ÖNCE yapılmalıdır.
    // notify::Init aynı zamanda AUMID kaydını ve Başlat menüsü kısayolunu
    // da kurar; ikisi de yalnızca bu çağrıdan sonra güvenilirdir.
    notify::Init();

    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
    g_initialScale = scale > 0.0f ? scale : 1.0f;
    const int w = (int)(980 * scale), h = (int)(640 * scale);

    // Uygulama simgesi kaynak dosyasından gelir. İki ölçü AYRI AYRI yüklenir:
    // görev çubuğu düğmesi ve alt/üst simge alanları küçük simgeyi (SM_CXSMICON)
    // kullanır. WNDCLASSEXW.hIconSm boş bırakılırsa Windows 11 görev çubuğunda
    // jenerik simge çiziliyor — marka resmi exe içinde durmasına rağmen.
    // LoadIconW tek bir ölçü verdiği için o yol burada kullanılmıyor.
    const int bigPx   = ::GetSystemMetrics(SM_CXICON);
    const int smallPx = ::GetSystemMetrics(SM_CXSMICON);
    HICON hIcon   = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(brand::kIconId),
                                        IMAGE_ICON, bigPx, bigPx, LR_DEFAULTCOLOR);
    HICON hIconSm = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(brand::kIconId),
                                        IMAGE_ICON, smallPx, smallPx, LR_DEFAULTCOLOR);

    // Kaynak bağlanamazsa yedek olarak sistem simgesi yüklenir; bu durumda
    // görev çubuğunda genel Windows simgesi görünür ama uygulama çalışmaya devam eder.
    if (!hIcon)   hIcon   = ::LoadIconW(nullptr, IDI_APPLICATION);
    if (!hIconSm) hIconSm = hIcon;   // küçük ölçü yoksa kabuk büyüğü ölçekler

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInstance,
                       hIcon, ::LoadCursorW(nullptr, IDC_ARROW),
                       nullptr, nullptr, brand::kWindowClass, hIconSm };
    ::RegisterClassExW(&wc);

    RECT wa;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int x = wa.left + (wa.right - wa.left - w) / 2;
    const int y = wa.top + (wa.bottom - wa.top - h) / 2;

    HWND hwnd = ::CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, brand::kName, WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                                  x, y, w, h, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
    {
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        ::MessageBoxW(nullptr, L"Failed to create the window.", brand::kMsgTitle, MB_ICONERROR);
        return 1;
    }

    // Sınıf simgesi yalnızca simge hiç set edilmemiş pencereler için varsayılandır.
    // Kenarlıksız (WS_POPUP) pencere kabuğa "bu bir uygulama penceresi" demiyor,
    // bu yüzden simge açıkça pencerenin kendisine de verilir. ICON_SMALL2 görev
    // çubuğunun okuduğu kayıttır.
    ::SendMessageW(hwnd, WM_SETICON, ICON_BIG,    (LPARAM)hIcon);
    ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL,  (LPARAM)hIconSm);
    ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL2, (LPARAM)hIconSm);

    // Köşe yuvarlatma isteği yalnızca Windows 11'de yapılır: eski sürümler bu
    // özniteliği kabul edip yok saydığı için sürüm denetimiyle korunur. Yalnızca
    // yuvarlatma sağlanırsa çerçeve rengi de düzleştirilerek köşe geçişindeki
    // renk farkı giderilir.
    float corner = 8.0f * scale;
    bool rounded = false;
    if (IsWindows11OrGreater())
    {
        DWORD pref = 2; // yuvarlak köşe tercihi
        if (SUCCEEDED(::DwmSetWindowAttribute(hwnd, 33 /* köşe tercihi */, &pref, sizeof(pref))))
        {
            rounded = true;
            COLORREF border = RGB(32, 32, 34);
            ::DwmSetWindowAttribute(hwnd, 34 /* çerçeve rengi */, &border, sizeof(border));
        }
    }
    if (!rounded)
    {
        corner = 12.0f * scale;
        const int d = (int)(corner * 2.0f);
        ::SetWindowRgn(hwnd, ::CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d), TRUE);
    }

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        ::MessageBoxW(nullptr, L"Failed to initialize Direct3D 11.", brand::kMsgTitle, MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    theme::Init(scale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    // Marka dokusu, arayüz ilk kez çizilmeden önce hazır olmalı: hazır değilse
    // DrawLogo yedeğe düşer ve bir sonraki karede dokunmayı denemez.
    logo::Init(g_device);

    // Medya adası kendi karesini BU cihaz üzerinde çizer: kendi bir D3D11
    // cihazı ve kendi bir ImGui bağlamı YOK. İkisi de ayrı olsaydı iki yüzey
    // aynı anda yaşayamazdı — imgui_impl_dx11'in vertex buffer'ları tek bir
    // cihaza aittir, ikinci bir cihazda RenderDrawData geçersiz adreslere
    // yazar, ana pencere siyah kalır ve ada kapatılınca uygulama 0xC0000005
    // ile çöker. Paylaşım app::Init'ten ÖNCE olmalı: app::Init ada
    // ayarlarını okuyup pencereyi ilk karede kuruyor.
    island::SetSharedD3D(g_device, g_context);

    app::Init(hwnd, corner);
    tray::Init(hwnd, hInstance);

#ifdef TENGRI_DEV
    // Geliştirme sürümü: odağı çalma, diğer pencerelerin arkasında kal.
    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    ::SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    // SW_SHOWDEFAULT, STARTUPINFO'daki wShowWindow'a bakar. O alan ShellExecute /
    // Start-Process ile açılışta 0 (SW_HIDE) kalabiliyor; bu durumda pencere
    // "gösterilmiş" sayılır ama gerçekte görünmez — uygulama siyah ekranda
    // kalıp kayboluyor. Düzeltme yolu her zaman büyük harf SW_SHOWNORMAL'dır.
    ::ShowWindow(hwnd, SW_SHOWNORMAL);
#endif
    ::UpdateWindow(hwnd);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_trayRestore)
        {
            g_trayRestore = false;
            ShowFromTray(hwnd);
        }
        tray::Tick();

        // Pencere başka pencerelerin altında kaldığında ya da tepside gizliyken
        // çizilecek bir şey yoktur; her kare için maliyetli görünürlük denemesi
        // yapmak yerine kare atlanır. Maliyetsiz sunum çağrısı örtülme durumunu
        // tazelemeye devam eder, böylece pencere öne geldiğinde döngü
        // kendiliğinden çalışır. Bildirim simgesine tıklandığında pencere hemen
        // geri gelebilsin diye iletiler yine de işlenir.
        //
        // Gizli pencere ayrılmaz bir koşul olarak eklenmeli: örtülme sorgusu
        // yalnızca sunumun SONUCUNA bakar, gizli pencerede kabuk her zaman
        // DXGI_STATUS_OCCLUDED döndürmek zorunda değildir. Sonuç S_OK olursa
        // döngü çizilmeyen bir arayüzün tam karesini dikey eşitlemeli olarak
        // üretmeye devam ederdi; arka planda çalışma tam olarak buysa boşuna
        // işlemci ve pil yakar.
        if (g_occluded || !::IsWindowVisible(hwnd))
        {
            g_occluded = (g_swapChain->Present(0, 0) == DXGI_STATUS_OCCLUDED);
            MSG skip;
            while (::PeekMessageW(&skip, nullptr, 0U, 0U, PM_REMOVE))
            {
                ::TranslateMessage(&skip);
                ::DispatchMessageW(&skip);
                if (skip.message == WM_QUIT) done = true;
            }
            if (done) break;
            if (g_trayRestore)
            {
                g_trayRestore = false;
                ShowFromTray(hwnd);
            }
            tray::Tick();
            // Kare atlanan yolda ada yine çizilmeli; aksi hâlde tepsideyken ya
            // da pencere örtülüyken donmuş kalır.
            island::Frame();
            ::Sleep(16);
            continue;
        }

        if (g_resizeW != 0 && g_resizeH != 0)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        app::Frame();

        ImGui::Render();
        const float clear[4] = { 0.016f, 0.016f, 0.02f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        const HRESULT hr = g_swapChain->Present(1, 0); // dikey eşitleme
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);

        // Ada UYGULAMANIN KARESİNDEN SONRA çizilir. Ada artık kendi ImGui
        // karesini açmıyor (paylaşılan bağlamda ikinci NewFrame(), ana
        // pencerenin pencerelerinin WasActive/ActiveId defterini sıfırlar ve
        // arayüzün hiçbir yerine tıklanamaz hâle getirirdi); kendi ImDrawList
        // ve ImDrawData'sını doğrudan DX11 backend'ine verir. Yazı tipi
        // atlasını bu turun ilk yarısı yüklediği için sıra burada önemli.
        island::Frame();

        if (app::WantsQuit())
            done = true;
    }

    app::Shutdown();
    tray::Shutdown();
    notify::Shutdown();

    if (g_singleMutex)
    {
        ::ReleaseMutex(g_singleMutex);
        ::CloseHandle(g_singleMutex);
        g_singleMutex = nullptr;
    }

    // Cihaz kapatılmadan önce serbest bırakılmalı; ters sırada serbest bırakılan
    // doya cihaz yok sayılır ve sızıntı olur.
    logo::Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
