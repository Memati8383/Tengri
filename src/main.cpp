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
#include <d3d11.h>
#include <dwmapi.h>

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

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (tray::HandleMessage(msg, lParam, &g_trayRestore))
        return 0;
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            // Bildirim simgesi çalışıyorsa pencere gizli kalarak yaşamaya devam
            // eder; simge yoksa küçültmenin alışılmış anlamı korunur, aksi hâlde
            // kullanıcı pencereyi tamamen kaybetmiş olur.
            if (app::TrayEnabled() && tray::IsAvailable())
            {
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
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Yükseltilmiş yeniden başlatma: kullanıcı bir yazan iş istedi, süreç "runas" ile
    // bu exe'yi yüksek yetkiyle yeniden açtı ve bekleyen işi komut satırında taşıdı.
    // Burada hiçbir arayüz oluşturmadan iş uygulanır ve süreç çıkar. Normal açılışta
    // bu bayrak yoktur ve akış normaldir.
    elevate::CacheExecutablePath();
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
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
    g_initialScale = scale > 0.0f ? scale : 1.0f;
    const int w = (int)(980 * scale), h = (int)(640 * scale);

    // Uygulama simgesi kaynak dosyasından gelir. Kaynak bağlanamazsa yedek olarak
    // sistem simgesi yüklenir; bu durumda görev çubuğunda genel Windows simgesi
    // görünür ama uygulama çalışmaya devam eder.
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(brand::kIconId));
    if (!hIcon) hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInstance,
                       hIcon, ::LoadCursorW(nullptr, IDC_ARROW),
                       nullptr, nullptr, brand::kWindowClass, nullptr };
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

    app::Init(hwnd, corner);
    tray::Init(hwnd, hInstance);

#ifdef TENGRI_DEV
    // Geliştirme sürümü: odağı çalma, diğer pencerelerin arkasında kal.
    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    ::SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
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
            ::ShowWindow(hwnd, SW_RESTORE);
            ::SetForegroundWindow(hwnd);
        }
        tray::Tick();

        // Pencere başka pencerelerin altında kaldığında çizilecek bir şey yoktur;
        // her kare için maliyetli görünürlük denemesi yapmak yerine kare atlanır.
        // Maliyetsiz sunum çağrısı örtülme durumunu tazelemeye devam eder, böylece
        // pencere öne geldiğinde döngü kendiliğinden çalışır. Bildirim simgesine
        // tıklandığında pencere hemen geri gelebilsin diye iletiler yine de işlenir.
        if (g_occluded)
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
                ::ShowWindow(hwnd, SW_RESTORE);
                ::SetForegroundWindow(hwnd);
            }
            tray::Tick();
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

        if (app::WantsQuit())
            done = true;
    }

    app::Shutdown();
    tray::Shutdown();

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
