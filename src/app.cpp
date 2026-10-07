#include "app.hpp"
#include "gui/theme.hpp"
#include "gui/widgets.hpp"
#include "gui/fx.hpp"
#include "gui/icons.hpp"
#include "gui/logo.hpp"
#include "core/license.hpp"
#include "core/sysinfo.hpp"
#include "core/cleaner.hpp"
#include "core/tweaks.hpp"
#include "core/network.hpp"
#include "core/lang.hpp"
#include "core/sysinfo_detail.hpp"
#include "core/ram.hpp"
#include "core/elevate.hpp"
#include "core/backup.hpp"
#include "tray.hpp"
#include "brand.hpp"
#include <shellapi.h>
#include "imgui_internal.h"
#include <string>
#include <vector>
#include <future>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "version.lib")

using theme::px;
using theme::White;
using theme::Gray;
using ui::ButtonStyle;
using ui::Toast;

namespace app
{
    namespace
    {
        constexpr const char* kVersion = brand::kVersion;

        enum class Screen     { Login, Loading, Main };
        enum class CleanState { Idle, Scanning, Ready, Cleaning, Done };

        // Görünen ad ve açıklamalar burada saklanmaz; çizim anında
        // lang::Get(kCatKeys[i]) üzerinden okunur. Böylece yapı yalnızca kategoriye özgü
        // tarama durumunu taşır.
        struct Category
        {
            bool  enabled;
            float found  = 0.0f;
            float target = 0.0f;
        };

        // Metinler yapıya gömülmez: (kategori, satır) çiftiyle çizim anında dil tablosundan
        // çözülür. Gömüldüğünde ilk çizimde yakalanıp kalıcılaşıyor ve 56 tweak ile 6 ağ
        // anahtarı dil değiştikten sonra da eski dilde görünmeye devam ediyordu.
        struct Tweak
        {
            bool on;
        };

        // ------------------------------------------------------------ genel durum

        HWND   g_hwnd   = nullptr;
        float  g_corner = 0.0f;

        Screen g_screen      = Screen::Login;
        Screen g_next        = Screen::Login;
        bool   g_switching   = false;
        double g_screenStart = 0.0;
        bool   g_closing     = false;
        bool   g_quit        = false;

        bool   g_dragging = false;
        POINT  g_dragStart{};
        RECT   g_dragRect{};

        char        g_key[64] = {};
        bool        g_reveal   = false;
        bool        g_remember = true;
        bool        g_waiting  = false;
        std::string g_loginError;
        double      g_errorTime = -100.0;

        int    g_tab     = 0;
        double g_tabTime = 0.0;

        std::vector<float> g_cpu(61, 0.0f);
        std::vector<float> g_ping(61, 0.0f);
        float  g_latency    = 0.0f;
        bool   g_pingSampled = false;
        double g_lastSample = -1.0;

        // Gecikme geçmişinde kaç kutunun GERÇEK örnek olduğu. Dizinin kalanı sıfır
        // olmak "0 ms gecikme" demek değildir, "henüz ölçülmedi" demektir; ikisi
        // grafikte tamamen farklı görünür. Sıfırları çizmek en iyi senaryoyu
        // uyduruyordu - ekrandaki düz çizginin sebebi buydu.
        int    g_pingFilled  = 0;
        double g_pingSampleAt = -1.0;   // son örneğin düştüğü an

        float  g_health     = 0.0f;
        bool   g_optimizing = false;
        double g_optStart   = 0.0;

        // std::async'in future nesnesi yıkıcısında bloklar; üyeyi yeniden atamak (ya da
        // kapanışta yok olmasına izin vermek) render thread'ini tam bir disk taraması
        // kadar bekletiyordu. Join edilebilir bir thread ve mutex ile korunan sonuç
        // yuvası aynı bloklamasız yoklamayı verir; Shutdown() thread'i join eder.
        struct Job
        {
            std::thread  worker;
            std::mutex   mtx;
            bool         ready = false;
        };

        Job        g_scanJob;
        Job        g_cleanJob;
        std::vector<cleaner::ScanResult> g_scanResults;
        double     g_freedReal = 0.0;

        // Temizleyici %TEMP% içini recursive olarak siler, regpack ise import .reg dosyasını
// oraya yazar. Aynı anda bir temizlik çalışırsa reg.exe'in okuduğu dosya silinir. Bu
// bayrak ikisini karşılıklı olarak birbirini dışlayan hale getirir.
        std::atomic<bool> g_cleaning{ false };
        std::atomic<bool> g_applyingReg{ false };

        void JoinAll()
        {
            if (g_scanJob.worker.joinable())  g_scanJob.worker.join();
            if (g_cleanJob.worker.joinable()) g_cleanJob.worker.join();
        }

        struct CatKey { S::Key nameKey, descKey; };
        const CatKey kCatKeys[] = {
            { S::TempFiles,      S::TempFilesDesc },
            { S::BrowserCache,   S::BrowserCacheDesc },
            { S::WinUpdateCache, S::WinUpdateCacheDesc },
            { S::RecycleBin,     S::RecycleBinDesc },
            { S::PrefetchData,   S::PrefetchDesc },
            { S::SystemLogs,     S::SystemLogsDesc },
            { S::ThumbnailCache, S::ThumbnailDesc },
            { S::CrashDumps,     S::CrashDumpsDesc },
            { S::ShaderCache,    S::ShaderCacheDesc },
            { S::DeliveryOpt,    S::DeliveryOptDesc },
        };

        // Sıra yukarıdaki kCatKeys[] ile birebir aynı olmalı.
        std::vector<Category> g_cats = {
            { true  }, // 0  geçici dosyalar
            { true  }, // 1  tarayıcı önbelleği
            { true  }, // 2  Windows Update önbelleği
            { false }, // 3  Geri Dönüşüm Kutusu
            { true  }, // 4  prefetch verisi
            { true  }, // 5  sistem günlükleri
            { true  }, // 6  küçük resim önbelleği
            { true  }, // 7  crash dump dosyaları
            { false }, // 8  shader önbelleği
            { true  }, // 9  Delivery Optimization
        };
        CleanState g_cleanState = CleanState::Idle;
        double     g_cleanStart = 0.0;
        float      g_cleanP     = 0.0f;
        float      g_freed      = 0.0f;
        bool g_scanDone  = false;
        bool g_cleanDone = false;

        constexpr int kTweakCats = 7;
        // Etiketler satır indeksinden lang::TweakNameKey/TweakDescKey ile gelir; yani kategori
        // yalnızca kendi 8 satırının varsayılan durumunu tutmak zorunda.
        std::vector<Tweak> g_tweaks[kTweakCats] = {
            {   // Performans
                { true  }, { true  }, { false }, { false },
                { false }, { true  }, { false }, { false },
            },
            {   // Oyun
                { true  }, { false }, { true  }, { false },
                { true  }, { false }, { false }, { false },
            },
            {   // Gizlilik
                { true  }, { true  }, { true  }, { false },
                { false }, { true  }, { false }, { false },
            },
            {   // Görsel
                { false }, { false }, { true  }, { true  },
                { false }, { false }, { true  }, { false },
            },
            {   // Oyunlar
                { false }, { false }, { false }, { false },
                { false }, { false }, { false }, { false },
            },
            {   // FiveM oyunları
                { false }, { false }, { false }, { false },
                { false }, { false }, { false }, { false },
            },
            {   // Gecikme
                { false }, { false }, { false }, { false },
                { false }, { false }, { false }, { false },
            },
        };
        int    g_tweakCat     = 0;
        double g_tweakCatTime = 0.0;
        bool   g_applying     = false;
        double g_applyStart   = 0.0;

        // Son başarılı uygulamadan bu yana kullanıcının çevirdiği anahtarlar. Kategori
        // 4-6 tek bir birleşik .reg gövdesi olarak yazılır; dokunulmamış girdileri
        // yeniden yazmak, başka bir aracın sahibi olduğu registry değerlerini geri alırdı.
        bool g_dirty[kTweakCats][16] = {};
        bool g_dirtyAny[kTweakCats] = {};

        // Bekleyen tekil işler: kullanıcı yükseltilmiş geçiş tetiklendiğinde, tweak
        // dışındaki sayfalarda da seçilmiş ama henüz uygulanmamış işleri aynı yükseltilmiş
        // sürece ekler. Böylece kullanıcı bir kez UAC görür, bekleyen tüm işler birden
        // uygulanır; her iş için ayrı UAC istemek sinir bozucu olurdu.
        int  g_pendingRam     = -1;
        int  g_pendingDns     = -1;
        int  g_pendingStartup = -1;

        // Son uygulamadan önce alınan yedeğin tam yolu. "Geri al" düğmesi bunu
        // kullanır; boşsa hiç yedek alınmamıştır.
        std::wstring g_lastBackup;

        void CollectPendingFromPages(elevate::Pending& p)
        {
            p.ramPreset     = g_pendingRam;
            p.dnsProvider   = g_pendingDns;
            p.startupToggle = g_pendingStartup;
        }

        // Etiketler yine satır indeksinden lang::NetNameKey/NetDescKey ile gelir, tweak kategorileriyle aynı düzen.
        std::vector<Tweak> g_netTweaks = {
            { true  }, { false }, { true  },
            { false }, { false }, { false },
        };
        int g_dns = 1;

        bool g_startup = false;   // gerçek durum HKCU Run girdisinden okunur
        bool g_tray    = true;    // gerçek tray ikonu: küçült gizler, ikon geri getirir

        constexpr int kTabCount = 7;
        const Icon kTabIcons[] = { Icon::Dashboard, Icon::Cleaner, Icon::Tweaks, Icon::Network, Icon::Monitor, Icon::Settings, Icon::Info };
        const S::Key kTabNameKeys[] = { S::Dashboard, S::Cleaner, S::Tweaks, S::Network, S::SystemInfo, S::Settings, S::About };
        const S::Key kTabSubKeys[]  = { S::DashOverview, S::CleanDesc, S::TweakDesc, S::NetDesc, S::SysInfoDesc, S::SettDesc, S::AboutDesc };

        // Plan ve bitiş metni license.cpp'de önceden biçimlenmiş olarak saklanmaz, burada çözülür.
        const char* PlanText(const license::Info& li)
        {
            return li.lifetime ? L(PlanLifetime) : L(PlanDemo);
        }

        const char* ExpiryText(const license::Info& li)
        {
            return li.lifetime ? L(ExpiresNever) : li.expires.c_str();
        }

        // sysdetail yalnızca durumu bildirir; ifade burada aranır, böylece dil değişimi
        // bu satırları Gather() anında yakalanan metni göstermek yerine yeniden çizer.
        const char* BiosModeText(bool legacy)
        {
            return legacy ? L(BiosLegacy) : L(BiosUefi);
        }

        const char* SecureBootText(sysdetail::SecureBootState s)
        {
            switch (s)
            {
            case sysdetail::SecureBootState::Enabled:     return L(Enabled);
            case sysdetail::SecureBootState::Disabled:    return L(Disabled);
            case sysdetail::SecureBootState::Unsupported: return L(SecureBootNotSupported);
            case sysdetail::SecureBootState::Unknown:
            default:                                      return "N/A (admin required)";
            }
        }

        const char* VirtText(sysdetail::VirtState s)
        {
            switch (s)
            {
            case sysdetail::VirtState::Firmware: return L(VirtFirmware);
            case sysdetail::VirtState::Vbs:      return L(VirtVbs);
            case sysdetail::VirtState::HyperV:   return L(VirtHyperV);
            case sysdetail::VirtState::Disabled:
            default:                            return L(VirtDisabled);
            }
        }

        // ------------------------------------------------------------------ hakkında

        bool g_aboutOpen = false;

        // Hakkında sayfasındaki bağlantılar burada çözümlenip işlenmez, doğrudan shell'e
        // bırakılır. ShellExecute adresi olduğu gibi alır; doğrulama gerektirecek bir
        // şey yoktur çünkü bu dizgeler kullanıcı girdisi değil, brand.hpp içindeki
        // derleme zamanı sabitleridir.
        void OpenUrl(const wchar_t* url)
        {
            ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        }

        // Hakkında panelinde gösterilen sürüm de derleme damgası da VERSIONINFO kaynağından
        // gelir; böylece kaynak dosyalarındaki metinler, çalıştırılabilir dosyanın
        // Explorer'a bildirdiği değerden kopamaz.
        std::string OwnFileVersion()
        {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, path, MAX_PATH);

            DWORD handle = 0;
            const DWORD sz = GetFileVersionInfoSizeW(path, &handle);
            if (sz == 0) return {};

            std::vector<char> buf(sz);
            if (!GetFileVersionInfoW(path, handle, sz, buf.data())) return {};

            VS_FIXEDFILEINFO* ffi = nullptr;
            UINT len = 0;
            if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &len) || !ffi || len == 0)
                return {};

            char v[32];
            snprintf(v, sizeof(v), "%u.%u.%u",
                     HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                     HIWORD(ffi->dwFileVersionLS));
            return v;
        }

        const char* DisplayVersion()
        {
            static const std::string v = OwnFileVersion();
            // Kaynak eksikse derleme zamanı sabitine düşer.
            return v.empty() ? brand::kVersion : v.c_str();
        }

        // Hakkında ekranı popup değil gerçek bir sayfadır: içinde bağlantılar vardır ve popup
        // navigasyon için yanlış kap.

        // ------------------------------------------------------------------ yardımcılar

        float Rand(float a, float b)
        {
            static std::mt19937 rng{ std::random_device{}() };
            return std::uniform_real_distribution<float>(a, b)(rng);
        }

        std::string FormatSize(float mb)
        {
            char b[32];
            if (mb >= 1024.0f) snprintf(b, sizeof(b), "%.2f GB", mb / 1024.0f);
            else               snprintf(b, sizeof(b), "%.0f MB", mb);
            return b;
        }

        float TotalFound()
        {
            float t = 0.0f;
            for (const Category& c : g_cats)
                if (c.enabled)
                    t += c.found;
            return t;
        }

        // Skor rastgele bir sayıdan değil, anlık sistem görüntüsünden türetilir: yüksek CPU,
        // dolu RAM ya da neredeyse tıklı sistem sürücüsü her biri puan düşürür.
        float ComputeHealth()
        {
            const auto& s = sys::Get();
            float score = 100.0f;
            score -= ImMax(0.0f, s.cpu - 0.65f) * 60.0f;
            score -= ImMax(0.0f, s.ramFrac - 0.80f) * 90.0f;
            score -= ImMax(0.0f, s.diskFrac - 0.85f) * 70.0f;
            if (s.diskTotalGB > 0.0f && s.diskFreeGB < 10.0f) score -= 10.0f;
            return ImClamp(score, 0.0f, 100.0f);
        }

        void GoTo(Screen s)
        {
            if (g_switching || s == g_screen)
                return;
            g_next      = s;
            g_switching = true;
        }

        void TextCentered(ImDrawList* dl, ImFont* f, float size, float cx, float y, ImU32 col, const char* text)
        {
            const ImVec2 ts = ui::TextSize(f, size, text);
            ui::Text(dl, f, size, ImVec2(cx - ts.x * 0.5f, y), col, text);
        }

        // Marka işareti. Doku hazırsa PNG'den çizilir; değilse (kaynak eksik,
        // doku yaratılamadı) çizgili elmas yedeğine düşülür, böylece arayüz hiçbir
        // koşulda marksız kalmaz.
        void DrawLogo(ImDrawList* dl, const ImVec2& c, float s, float glow = 1.0f)
        {
            const float h = s * 0.5f;

            if (logo::Ready())
            {
                // Doku zaten kendi koyu karesini taşıyor, arkaya ayrı bir hale
                // gerekmiyor; glow yalnızca kenarı yumuşatmak için düşük tutulur.
                const ImU32 tint = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(1.0f, 1.0f, 1.0f, ImClamp(glow, 0.0f, 1.0f)));
                logo::Draw(dl, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), tint);
                return;
            }

            const float t = (float)ImGui::GetTime();
            fx::RadialGradient(dl, c, s * 1.5f, s * 1.5f, White(0.10f * glow), White(0.0f), 40);

            const ImVec2 d[4] = { ImVec2(c.x, c.y - h), ImVec2(c.x + h, c.y), ImVec2(c.x, c.y + h), ImVec2(c.x - h, c.y) };
            dl->AddPolyline(d, 4, White(0.95f), ImDrawFlags_Closed, ImMax(1.5f, s * 0.06f));

            const float a = t * 0.8f, r = h * 0.40f;
            ImVec2 q[4];
            for (int i = 0; i < 4; ++i)
                q[i] = ImVec2(c.x + cosf(a + i * IM_PI * 0.5f) * r, c.y + sinf(a + i * IM_PI * 0.5f) * r);
            dl->AddConvexPolyFilled(q, 4, White(0.95f));
        }

        void InfoRow(const char* k, const char* v, float w, bool sep = true)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float  h = px(30);
            const ImVec2 ks = ui::TextSize(theme::fonts.regular,theme::size::Body, k);
            const ImVec2 vs = ui::TextSize(theme::fonts.medium,theme::size::Body, v);
            ui::Text(dl, theme::fonts.regular,theme::size::Body, ImVec2(p.x, p.y + (h - ks.y) * 0.5f), Gray(0.50f), k);
            ui::Text(dl, theme::fonts.medium,theme::size::Body, ImVec2(p.x + w - vs.x, p.y + (h - vs.y) * 0.5f), Gray(0.92f), v);
            if (sep)
                dl->AddLine(ImVec2(p.x, p.y + h + px(3)), ImVec2(p.x + w, p.y + h + px(3)), White(0.05f));
            ImGui::Dummy(ImVec2(w, h));
        }

        // ------------------------------------------------------------------ eylemler

        void TryActivate()
        {
            if (g_waiting)
                return;
            if (!g_key[0])
            {
                g_loginError = L(PleaseEnterKey);
                g_errorTime  = ImGui::GetTime();
                return;
            }
            g_loginError.clear();
            license::Activate(g_key);
            g_waiting = true;
        }

        void SignOut()
        {
            license::Logout();
            if (!g_remember)
            {
                license::ForgetSaved();
                memset(g_key, 0, sizeof(g_key));
            }
            g_loginError.clear();
            ui::Notify(Toast::Info, L(SignedOut), L(SignedOutMsg));
            GoTo(Screen::Login);
        }

        void GenerateKey()
        {
            static const char hex[] = "0123456789ABCDEF";
            char key[20];
            for (int i = 0; i < 19; ++i)
                key[i] = (i == 4 || i == 9 || i == 14) ? '-' : hex[(int)Rand(0, 15.99f)];
            key[19] = 0;
            strncpy_s(g_key, key, _TRUNCATE);
        }

        void StartScan()
        {
            for (Category& c : g_cats) { c.target = 0.0f; c.found = 0.0f; }
            g_cleanState = CleanState::Scanning;
            g_cleanStart = ImGui::GetTime();
            g_scanDone   = false;

            // Arama sürerken arayüz düğmeyi pasifleştiriyor, ancak bir kategori anahtarından yine
            // buraya ulaşılabilir; önce join etmek taramayı tek uçuşlu tutar.
            JoinAll();
            {
                // Kimsenin tüketmediği bir sonucu düşür, yoksa bu koşunun sonucu sanılır.
                std::lock_guard<std::mutex> lock(g_scanJob.mtx);
                g_scanJob.ready = false;
                g_scanResults.clear();
            }

            std::vector<bool> enabled;
            for (const Category& c : g_cats) enabled.push_back(c.enabled);

            g_scanJob.worker = std::thread([enabled]() {
                std::vector<cleaner::ScanResult> results;
                for (int i = 0; i < (int)enabled.size(); ++i)
                    results.push_back(enabled[i] ? cleaner::Scan(i) : cleaner::ScanResult{});
                std::lock_guard<std::mutex> lock(g_scanJob.mtx);
                g_scanResults = std::move(results);
                g_scanJob.ready = true;
            });
        }

        void StartClean()
        {
            if (g_applyingReg.load()) return;   // şu anda bir tweak import'u %TEMP% üzerinde sahip
            g_cleaning.store(true);
            g_freed = 0.0f;
            for (Category& c : g_cats)
            {
                c.target = c.enabled ? c.found : 0.0f;
                g_freed += c.target;
            }
            g_cleanState = CleanState::Cleaning;
            g_cleanStart = ImGui::GetTime();
            g_cleanDone  = false;

            JoinAll();
            {
                std::lock_guard<std::mutex> lock(g_cleanJob.mtx);
                g_cleanJob.ready = false;
            }

            std::vector<bool> cats;
            for (const Category& c : g_cats) cats.push_back(c.enabled && c.found > 0.5f);

            g_cleanJob.worker = std::thread([cats]() {
                double total = 0.0;
                for (int i = 0; i < (int)cats.size(); ++i)
                    if (cats[i]) total += cleaner::Clean(i);
                std::lock_guard<std::mutex> lock(g_cleanJob.mtx);
                g_freedReal = total;
                g_cleanJob.ready = true;
            });
            // Sonuç tüketildikten sonra UpdateData içinde bırakılır.
        }

        // ------------------------------------------------- benzetim / veri yoklama

        void UpdateData(double now)
        {
            if (g_lastSample < 0.0 || now - g_lastSample >= 0.5)
            {
                sys::Update();
                static bool primed = false; // ilk gerçek CPU örneği geçmişi doldurur, grafik düz başlamaz
                if (!primed && sys::Get().cpu > 0.0f)
                {
                    std::fill(g_cpu.begin(), g_cpu.end(), sys::Get().cpu);
                    primed = true;
                }
                g_cpu.erase(g_cpu.begin());
                g_cpu.push_back(sys::Get().cpu);

                g_lastSample = now;
                g_health = ComputeHealth();
            }

            // Gecikme geçmişi yalnızca arkasında gerçek bir örnek varken ilerletilir. Sondaç
            // yanıt vermedikçe kutular boş kalır ve grafik onları ÇİZMEZ; sıfır
            // yazıp "0 ms" diye göstermek ölçüm uydurmak olurdu.
            if (network::PollLatency(&g_latency))
            {
                const float v = ImClamp(g_latency, 1.0f, 500.0f);
                g_ping.erase(g_ping.begin());
                g_ping.push_back(v);
                g_pingFilled  = ImMin((int)g_ping.size(), g_pingFilled + 1);
                g_pingSampleAt = now;
                g_pingSampled  = true;
            }

            if (g_waiting)
            {
                const license::Status st = license::Poll();
                if (st == license::Status::Valid)
                {
                    g_waiting = false;
                    if (g_remember) license::Save(license::Current().key);
                    else            license::ForgetSaved();
                    const std::string msg = std::string(license::Current().lifetime ? L(PlanLifetime) : L(PlanDemo)) + " " + L(ActivatedMsg);
                    ui::Notify(Toast::Success, L(ActivateLicense), msg.c_str());
                    GoTo(Screen::Loading);
                }
                else if (st == license::Status::Invalid)
                {
                    g_waiting    = false;
                    g_loginError = license::Error();
                    g_errorTime  = now;
                    ui::Notify(Toast::Error, L(ActivationFailed), g_loginError.c_str());
                }
            }

            if (g_optimizing && now - g_optStart > 3.2)
            {
                g_optimizing = false;
                // Skor, göz alıcı bir rastgele değer atanmak yerine bir sonraki örnekten yeniden hesaplanır.
                SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
                network::FlushDns();
                ui::Notify(Toast::Success, L(SystemOptimized), L(OptimizedDesc));
            }

            const int n = (int)g_cats.size();
            if (g_cleanState == CleanState::Scanning)
            {
                g_cleanP = ImSaturate((float)(now - g_cleanStart) / 3.0f);
                if (!g_scanDone)
                {
                    std::lock_guard<std::mutex> lock(g_scanJob.mtx);
                    if (g_scanJob.ready)
                    {
                        for (int i = 0; i < n && i < (int)g_scanResults.size(); ++i)
                            g_cats[i].target = (float)g_scanResults[i].sizeMB;
                        g_scanJob.ready = false;
                        g_scanDone = true;
                    }
                }
                for (int i = 0; i < n; ++i)
                {
                    const float k = ImSaturate(g_cleanP * n - (float)i);
                    g_cats[i].found = g_cats[i].target * k;
                }
                if (g_cleanP >= 1.0f && g_scanDone)
                {
                    g_cleanState = CleanState::Ready;
                    const std::string msg = FormatSize(TotalFound()) + " of junk found";
                    ui::Notify(Toast::Info, L(ScanComplete), msg.c_str());
                }
            }
            else if (g_cleanState == CleanState::Cleaning)
            {
                g_cleanP = ImSaturate((float)(now - g_cleanStart) / 2.4f);
                for (int i = 0; i < n; ++i)
                    if (g_cats[i].enabled)
                        g_cats[i].found = g_cats[i].target * (1.0f - ImSaturate(g_cleanP * n - (float)i));
                if (!g_cleanDone)
                {
                    std::lock_guard<std::mutex> lock(g_cleanJob.mtx);
                    if (g_cleanJob.ready)
                    {
                        g_freed     = (float)g_freedReal;
                        g_cleanJob.ready = false;
                        g_cleanDone = true;
                        g_cleaning.store(false);
                    }
                }
                if (g_cleanP >= 1.0f && g_cleanDone)
                {
                    g_cleanState = CleanState::Done;
                    const std::string msg = FormatSize(g_freed) + " freed successfully";
                    ui::Notify(Toast::Success, L(AllClean), msg.c_str());
                }
            }

            if (g_applying && now - g_applyStart > 1.4)
            {
                g_applying = false;

                // Yalnızca kullanıcının o sırada baktığı kategoriye dokunulur. Tek bir
                // düğmeden 56 ayarın tamamını uygulamak, arayüzün arkasından tüm
                // registry kümesini yazıyor ve kimsenin çevirmediği anahtarları da geri alıyordu.
                const int c = g_tweakCat;

                // Uygulamadan hemen once dokunulacak anahtarlar disa aktarilir.
                // "Kapat" islemi kullanici degerini degil kodlanmis varsayilani geri
                // yukler; bu yedek olmadan geri al dugmesi gercek bir geri alma
                // sayilmaz. Yedekleme yalnizca OKUMA ister, yonetici gerekmez.
                if (g_dirtyAny[c])
                {
                    const int cats[1] = { c };
                    std::wstring yedek;
                    if (backup::ExportForCategories(cats, 1, &yedek)) g_lastBackup = yedek;
                }

                // Süreç yükseltilmiş değilse, bekleyen iş "runas" ile yeni bir yükseltilmiş
                // sürece devredilir ve bu arayüz kapanır. Kullanıcı UAC'i bir kez görür;
                // ayarının uygulanmış olması gerektiğini o süreç yapar. Buradaki düğme
                // basıldığı an sistem bilgisi okumak gibi işlemler için UAC hiç çıkmaz.
                if (g_dirtyAny[c] && !elevate::IsElevated())
                {
                    elevate::Pending p;
                    p.anyChange[c] = true;
                    const int cnt = ImMin((int)g_tweaks[c].size(), 8);
                    for (int i = 0; i < cnt; ++i) p.categories[c][i] = g_tweaks[c][i].on;
                    // Başka sayfalarda bekleyen işler varsa (RAM/DNS/başlangıç) aynı yükseltilmiş
                    // geçişte onlar da taşınsın; kullanıcı bir kez UAC görsün.
                    CollectPendingFromPages(p);
                    elevate::ApplyOrDelegate(p);
                    // ApplyOrDelegate devredildiyse süreci kapatır; buraya dönmez.
                    // UAC reddedildiyse bu satıra düşer ve ayar uygulanmamış olur.
                    g_dirtyAny[c] = false;
                    for (int i = 0; i < cnt; ++i) g_dirty[c][i] = false;
                    ui::Notify(Toast::Warning, L(TweaksApplied), L(NeedAdmin));
                    return;
                }

                int on = 0, fail = 0, written = 0;
                // Uygulama boyunca tutulur: regpack import .reg dosyasını %TEMP% altına yazar,
                // çalışan bir temizleyici bu dosyayı reg.exe'in elinden siler.
                g_applyingReg.store(true);

                if (g_dirtyAny[c])
                {
                    const int cnt = ImMin((int)g_tweaks[c].size(), 16);

                    if (tweaks::IsRegPack(c))
                    {
                        bool states[16] = {}, mask[16] = {};
                        for (int i = 0; i < cnt; ++i)
                        {
                            states[i] = g_tweaks[c][i].on;
                            mask[i]   = g_dirty[c][i];
                        }
                        if (tweaks::ApplyCategory(c, states, mask, cnt))
                        {
                            for (int i = 0; i < cnt; ++i) { g_dirty[c][i] = false; written++; }
                        }
                        else
                        {
                            fail += cnt;
                        }
                    }
                    else
                    {
                        for (int i = 0; i < cnt; ++i)
                        {
                            if (!g_dirty[c][i]) continue;
                            if (tweaks::Apply(c, i, g_tweaks[c][i].on)) { g_dirty[c][i] = false; written++; }
                            else fail++;
                        }
                    }
                    g_dirtyAny[c] = false;
                }

                // İki anahtarın ortak sahiplendiği bir registry değeri, ikisi de yazıldığı anda kapalı
                // olan kardeş tarafından geri alınır; bu yüzden açık duran her paylaşımcı
                // en son yeniden iddia edilir. Kardeşler farklı kategorilerde olabildiği için
                // tüm kategorilerde çalışır ve yalnızca şu an açık olan anahtarlara dokunur.
                for (int cc = 0; cc < kTweakCats; ++cc)
                    for (int i = 0; i < (int)g_tweaks[cc].size(); ++i)
                        if (g_tweaks[cc][i].on && tweaks::IsShared(cc, i))
                            tweaks::Apply(cc, i, true);

                for (int cc = 0; cc < kTweakCats; ++cc)
                    for (const Tweak& t : g_tweaks[cc])
                        on += t.on ? 1 : 0;

                char msg[192];
                if (written == 0)
                    snprintf(msg, sizeof(msg), L(NothingChangedFmt), on);
                else if (fail > 0)
                    snprintf(msg, sizeof(msg), L(AdminFailedFmt), on, fail);
                else
                    snprintf(msg, sizeof(msg), L(RestartFmt), on);
                g_applyingReg.store(false);
                ui::Notify(fail > 0 ? Toast::Warning : Toast::Success, L(TweaksApplied), msg);
            }
        }

        // ------------------------------------------------------------------ pencere kabuğu

        void DrawWindowControls(const ImVec2& ds)
        {
            const ImVec2 bs = px(34, 28);
            const float  y  = px(12);
            ImGui::SetCursorScreenPos(ImVec2(ds.x - px(12) - bs.x * 2.0f - px(4), y));
            if (ui::IconButton("##minimize", Icon::Minimize, bs, px(12)))
            {
                // Mesaj pencere üzerinden gönderilir, böylece tray yolu (küçültme yerine gizleme)
                // da geçerli sayılır.
                ::PostMessageW(g_hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            }
            ImGui::SetCursorScreenPos(ImVec2(ds.x - px(12) - bs.x, y));
            if (ui::IconButton("##close", Icon::Close, bs, px(11), true))
                g_closing = true;
        }

        void HandleDrag()
        {
            const ImGuiIO& io = ImGui::GetIO();
            const float dragH = g_screen == Screen::Main ? px(78) : px(60);
            if (ImGui::IsMouseClicked(0) && io.MousePos.y >= 0.0f && io.MousePos.y < dragH &&
                !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive())
            {
                g_dragging = true;
                GetCursorPos(&g_dragStart);
                GetWindowRect(g_hwnd, &g_dragRect);
            }
            if (g_dragging)
            {
                if (!ImGui::IsMouseDown(0))
                {
                    g_dragging = false;
                }
                else
                {
                    POINT p;
                    GetCursorPos(&p);
                    SetWindowPos(g_hwnd, nullptr, g_dragRect.left + p.x - g_dragStart.x, g_dragRect.top + p.y - g_dragStart.y,
                                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
        }

        // ------------------------------------------------------------------ giriş

        void DrawLogin(const ImVec2& ds, float slide)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const double now = ImGui::GetTime();
            const auto& F = theme::fonts;

            // sol üstte marka
            ui::TextSpaced(dl, F.bold,theme::size::Body, px(22, 19), Gray(0.9f), brand::kNameA, px(theme::track::Micro));
            const float bw = ui::SpacedSize(F.bold,theme::size::Body, brand::kNameA, px(theme::track::Micro)).x;
            ui::Text(dl, F.regular,theme::size::Body, ImVec2(px(22) + bw + px(10), px(19)), Gray(theme::ink::Tertiary),  L(SecureLoader));

            // kart (hata durumunda titrer)
            const float cw = px(380), ch = px(468);
            const float since = (float)(now - g_errorTime);
            const float shake = since < 0.5f ? sinf(since * 55.0f) * px(7) * (1.0f - since / 0.5f) : 0.0f;
            const ImVec2 cmin(floorf((ds.x - cw) * 0.5f + shake), floorf((ds.y - ch) * 0.5f + px(10) + slide));
            const ImVec2 cmax = cmin + ImVec2(cw, ch);
            const float  cx   = (cmin.x + cmax.x) * 0.5f;

            fx::RadialGradient(dl, ImVec2(cx, cmin.y + px(40)), cw * 0.95f, ch * 0.7f, White(0.035f), White(0.0f), 64);
            ui::Card(dl, cmin, cmax, px(18));
            const float sp = fmodf((float)now, 7.0f) / 1.8f;
            if (sp < 1.0f)
                fx::Shine(dl, cmin, cmax, sp, White(0.03f));

            // marka bloğu
            DrawLogo(dl, ImVec2(cx, cmin.y + px(66)), px(40));
            const ImVec2 ts = ui::SpacedSize(F.bold,theme::size::Display, brand::kNameA, px(theme::track::Wide));
            ui::TextSpaced(dl, F.bold,theme::size::Display, ImVec2(cx - ts.x * 0.5f, cmin.y + px(104)), Gray(0.97f), brand::kNameA, px(theme::track::Wide));
            TextCentered(dl, F.regular,theme::size::Body, cx, cmin.y + px(144), Gray(0.50f), L(Tagline));

            // form alanları
            const float fx0 = cmin.x + px(32), fw = cw - px(64);
            ui::TextSpaced(dl, F.medium,theme::size::Meta, ImVec2(fx0, cmin.y + px(188)), Gray(theme::ink::Tertiary),  L(LicenseKey), px(theme::track::Micro));

            const float gbw = px(88);
            ImGui::SetCursorScreenPos(ImVec2(fx0 + fw - gbw, cmin.y + px(183)));
            ImGui::BeginDisabled(g_waiting);
            char genLabel[64]; snprintf(genLabel, sizeof(genLabel), "%s###gen", L(Generate));
            if (ui::Button(genLabel, ImVec2(gbw, px(24)), ButtonStyle::Ghost, Icon::Bolt))
                GenerateKey();
            ImGui::EndDisabled();

            ImGui::SetCursorScreenPos(ImVec2(fx0, cmin.y + px(208)));
            ImGui::BeginDisabled(g_waiting);
            const bool enter = ui::InputField("##license", "XXXX-XXXX-XXXX-XXXX", g_key, sizeof(g_key), Icon::Key, &g_reveal, fw,
                                              ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SetCursorScreenPos(ImVec2(fx0, cmin.y + px(264)));
            ui::ToggleCard(L(RememberMe), nullptr, &g_remember, fw, false);
            ImGui::EndDisabled();

            ImGui::SetCursorScreenPos(ImVec2(fx0, cmin.y + px(320)));
            if (ui::Button(L(ActivateLicense), ImVec2(fw, px(46)), ButtonStyle::Primary, Icon::None, g_waiting) || enter)
                TryActivate();

            // durum satırı
            const float sy = cmin.y + px(384);
            if (g_waiting)
            {
                TextCentered(dl, F.regular,theme::size::Body, cx, sy, Gray(0.55f), L(ContactingServer));
            }
            else if (!g_loginError.empty())
            {
                const ImVec2 es = ui::TextSize(F.medium,theme::size::Body, g_loginError.c_str());
                const float  x  = cx - (es.x + px(22)) * 0.5f;
                dl->AddCircleFilled(ImVec2(x + px(8), sy + es.y * 0.5f), px(8), White(0.92f), 20);
                icons::Draw(dl, Icon::Warning, ImVec2(x + px(8), sy + es.y * 0.5f), px(9), Gray(0.05f), px(1.6f));
                ui::Text(dl, F.medium,theme::size::Body, ImVec2(x + px(22), sy), Gray(0.88f), g_loginError.c_str());
            }
            else
            {
                char demoMsg[128];
                snprintf(demoMsg, sizeof(demoMsg), "%s \xC2\xB7 %s", L(DemoMode), L(AnyKeyAccepted));
                TextCentered(dl, F.regular,theme::size::Body, cx, sy, Gray(theme::ink::Tertiary),  demoMsg);
            }

            // alt bilgi
            const float fy = cmax.y - px(50);
            dl->AddLine(ImVec2(cmin.x + px(1), fy), ImVec2(cmax.x - px(1), fy), White(0.06f));
            ui::TextSpaced(dl, F.medium,theme::size::Meta, ImVec2(fx0, fy + px(19)), Gray(theme::ink::Tertiary),  L(HWID), px(theme::track::Micro));
            ui::Text(dl, F.regular,theme::size::Caption, ImVec2(fx0 + px(44), fy + px(17)), Gray(0.62f), sys::Hwid().c_str());
            char ver[16];
            snprintf(ver, sizeof(ver), "v%s", kVersion);
            const ImVec2 vs = ui::TextSize(F.regular,theme::size::Caption, ver);
            ui::Text(dl, F.regular,theme::size::Caption, ImVec2(cmax.x - px(32) - vs.x, fy + px(17)), Gray(theme::ink::Tertiary),  ver);
        }

        // ------------------------------------------------------------------ yükleme

        void DrawLoading(const ImVec2& ds, float slide)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F = theme::fonts;
            const float t   = (float)(ImGui::GetTime() - g_screenStart);
            const float dur = 2.6f;
            const float p   = ImSaturate(t / dur);
            const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f - px(50) + slide);

            dl->AddCircle(c, px(48), White(0.06f), 72, px(1.5f));
            ui::Spinner(dl, c, px(48), px(1.8f), White(0.85f));
            DrawLogo(dl, c, px(44));

            const std::string welcome = std::string(L(WelcomeBack)) + license::Current().user;
            TextCentered(dl, F.bold, theme::size::PageTitle, c.x, c.y + px(76), Gray(0.97f), welcome.c_str());

            static const char* const steps[] = {
                L(LoadingStep1), L(LoadingStep2), L(LoadingStep3), L(LoadingStep4)
            };
            const int idx = ImMin((int)(p * 4.0f), 3);
            TextCentered(dl, F.regular,theme::size::Body, c.x, c.y + px(108), Gray(0.50f), steps[idx]);

            ImGui::SetCursorScreenPos(ImVec2(c.x - px(150), c.y + px(142)));
            ui::ProgressBar("##boot", p, ImVec2(px(300), px(4)));

            char pct[8];
            snprintf(pct, sizeof(pct), "%d%%", (int)(p * 100.0f));
            TextCentered(dl, F.medium,theme::size::Meta, c.x, c.y + px(156), Gray(theme::ink::Secondary), pct);

            if (t > dur + 0.3f)
                GoTo(Screen::Main);
        }

        // ------------------------------------------------------------------ sayfalar

        void StatCard(const ImVec2& mn, const ImVec2& sz, Icon icon, const char* label, const char* value, const char* sub, float frac)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const ImVec2 mx = mn + sz;
            const ImGuiID id = ImGui::GetID(label);
            const bool  hov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(mn, mx);
            const float hv  = ui::Anim(ui::Key(id, "h"), hov ? 1.0f : 0.0f, 12.0f);
            const float af  = ui::Anim(ui::Key(id, "f"), frac, 6.0f);

            ui::Card(dl, mn, mx, px(14), hv);
            const ImVec2 ib = mn + px(16, 16);
            dl->AddRectFilled(ib, ib + px(30, 30), White(0.06f + 0.04f * hv), px(8));
            dl->AddRect(ib, ib + px(30, 30), White(0.08f), px(8), 0, ImMax(1.0f, px(1)));
            icons::Draw(dl, icon, ib + px(15, 15), px(15), Gray(0.92f), px(1.4f));

            ui::Text(dl, F.medium,theme::size::Body, ImVec2(mn.x + px(56), mn.y + px(22)), Gray(0.60f), label);
            ui::Text(dl, F.bold,theme::size::PageTitle, ImVec2(mn.x + px(16), mn.y + px(54)), Gray(0.97f), value);
            ui::Text(dl, F.regular,theme::size::Caption, ImVec2(mn.x + px(16), mn.y + px(88)), Gray(theme::ink::Tertiary),  sub);

            const ImVec2 b0(mn.x + px(16), mx.y - px(15)), b1(mx.x - px(16), mx.y - px(12));
            dl->AddRectFilled(b0, b1, White(0.07f), px(2));
            dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * ImSaturate(af), b1.y), White(0.9f), px(2));
        }

        void PageDashboard(float cw)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const auto& s  = sys::Get();
            const double now = ImGui::GetTime();
            const float gap = px(14);
            char v[64], sub[64];

            // istatistik satırı
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float sw = floorf((cw - gap * 3.0f) / 4.0f), sh = px(128);
            const float health = ui::Anim(ImGui::GetID("##health"), g_health, 3.0f);

            snprintf(v, sizeof(v), "%.0f%%", s.cpu * 100.0f);
            snprintf(sub, sizeof(sub), L(LogicalThreads), s.threads);
            StatCard(p, ImVec2(sw, sh), Icon::Cpu, L(Processor), v, sub, s.cpu);

            snprintf(v, sizeof(v), "%.1f GB", s.ramUsedGB);
            snprintf(sub, sizeof(sub), L(OfGBInUse), s.ramTotalGB);
            StatCard(p + ImVec2((sw + gap) * 1.0f, 0), ImVec2(sw, sh), Icon::Memory, L(Memory), v, sub, s.ramFrac);

            snprintf(v, sizeof(v), "%.0f GB", s.diskFreeGB);
            snprintf(sub, sizeof(sub), L(FreeOfGB), s.diskTotalGB);
            StatCard(p + ImVec2((sw + gap) * 2.0f, 0), ImVec2(sw, sh), Icon::Disk, L(Storage), v, sub, s.diskFrac);

            snprintf(v, sizeof(v), "%.0f", health);
            StatCard(p + ImVec2((sw + gap) * 3.0f, 0), ImVec2(sw, sh), Icon::Shield, L(Health), v,
                     health >= 92.0f ? L(Excellent) : health >= 75.0f ? L(Good) : L(NeedsAttention), health / 100.0f);
            ImGui::Dummy(ImVec2(cw, sh));

            // grafik + hızlı optimizasyon
            p = ImGui::GetCursorScreenPos();
            const float gw = floorf((cw - gap) * 0.64f), rh = px(236);
            ui::Card(dl, p, p + ImVec2(gw, rh));
            ui::Text(dl, F.bold,theme::size::Title, p + px(18, 16), Gray(0.96f), L(ProcessorLoad));
            ui::Text(dl, F.regular,theme::size::Body, p + px(18, 38), Gray(theme::ink::Secondary), L(LiveLast30s));

            const float cur = ui::Anim(ImGui::GetID("##cpucur"), g_cpu.back() * 100.0f, 6.0f);
            snprintf(v, sizeof(v), "%.0f%%", cur);
            const ImVec2 cs = ui::TextSize(F.bold,theme::size::Heading, v);
            ui::Text(dl, F.bold,theme::size::Heading, ImVec2(p.x + gw - px(18) - cs.x, p.y + px(18)), Gray(0.97f), v);

            float peak = 0.0f;
            for (float c : g_cpu) peak = ImMax(peak, c);
            const float vmax  = ui::Anim(ImGui::GetID("##cpumax"), ImMax(0.25f, peak * 1.3f), 3.0f);
            const float scroll = (float)((now - g_lastSample) / 0.5);
            ui::Graph(dl, p + px(18, 70), p + ImVec2(gw - px(18), rh - px(18)), g_cpu.data(), (int)g_cpu.size(), 0.0f, vmax, scroll);

            const ImVec2 o(p.x + gw + gap, p.y), osz(cw - gw - gap, rh);
            ui::Card(dl, o, o + osz);
            ui::Text(dl, F.bold,theme::size::Title, o + px(18, 16), Gray(0.96f), L(QuickOptimize));
            ui::Text(dl, F.regular,theme::size::Body, o + px(18, 38), Gray(theme::ink::Secondary), L(OneClickEvery));

            const float prog  = g_optimizing ? ImSaturate((float)(now - g_optStart) / 3.2f) : 0.0f;
            const float ringV = ui::Anim(ImGui::GetID("##ring"), g_optimizing ? prog : health / 100.0f, 6.0f);
            const ImVec2 rc(o.x + osz.x * 0.5f, o.y + px(114));
            ui::Ring(dl, rc, px(40), px(6), ringV);
            snprintf(v, sizeof(v), g_optimizing ? "%.0f%%" : "%.0f", g_optimizing ? prog * 100.0f : health);
            TextCentered(dl, F.bold,theme::size::PageTitle, rc.x, rc.y - px(19), Gray(0.97f), v);
            TextCentered(dl, F.regular,theme::size::Meta, rc.x, rc.y + px(8), Gray(theme::ink::Tertiary),  g_optimizing ? L(Optimizing) : L(HealthScore));

            ImGui::SetCursorScreenPos(ImVec2(o.x + px(18), o.y + rh - px(58)));
            if (ui::Button(L(OptimizeNow), ImVec2(osz.x - px(36), px(40)), ButtonStyle::Primary, Icon::Bolt, g_optimizing))
            {
                g_optimizing = true;
                g_optStart   = now;
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(cw, rh));

            // bilgi kartları
            const float hw = floorf((cw - gap) * 0.5f);
            const unsigned long long up = s.uptimeSec;
            char uptime[32];
            snprintf(uptime, sizeof(uptime), "%llu h %02llu m", up / 3600ULL, (up / 60ULL) % 60ULL);
            static const std::string os = sys::OsName(), pc = sys::ComputerName(), user = sys::UserName(),
                                     cpuN = sys::CpuName(), gpuN = sys::GpuName();

            ui::BeginCard("##system", hw, L(System));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                InfoRow(L(OperatingSystem), os.c_str(), w);
                InfoRow(L(Processor), cpuN.c_str(), w);
                InfoRow(L(Graphics), gpuN.c_str(), w);
                InfoRow(L(Computer), pc.c_str(), w);
                InfoRow(L(User), user.c_str(), w);
                InfoRow(L(Uptime), uptime, w, false);
            }
            ui::EndCard();
            ImGui::SameLine(0, gap);
            ui::BeginCard("##subscription", hw, L(Subscription));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                const auto& li = license::Current();
                InfoRow(L(Plan), PlanText(li), w);
                InfoRow(L(Status), L(Active), w);
                InfoRow(L(Expires), ExpiryText(li), w);
                InfoRow(L(HWID), li.hwid.c_str(), w, false);
            }
            ui::EndCard();
        }

        void PageCleaner(float cw)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const float gap = px(14);
            const int   n   = (int)g_cats.size();

            ImVec2 p = ImGui::GetCursorScreenPos();
            const float hh = px(128);
            ui::Card(dl, p, p + ImVec2(cw, hh));

            const float total = TotalFound();
            const int   cur   = ImMin((int)(g_cleanP * n), n - 1);
            std::string big;
            char cap[128];
            switch (g_cleanState)
            {
            case CleanState::Idle:
                big = L(ReadyToScan);
                snprintf(cap, sizeof(cap), "%s", L(SelectCategories));
                break;
            case CleanState::Scanning:
                big = FormatSize(total);
                snprintf(cap, sizeof(cap), L(ScanningFmt), lang::Get(kCatKeys[cur].nameKey));
                break;
            case CleanState::Ready:
            {
                int k = 0;
                for (const Category& c : g_cats) k += (c.enabled && c.found > 0.5f) ? 1 : 0;
                big = FormatSize(total);
                snprintf(cap, sizeof(cap), "%s \xC2\xB7 %s", L(JunkFound), L(ReadyToClean));
                break;
            }
            case CleanState::Cleaning:
                big = FormatSize(total);
                snprintf(cap, sizeof(cap), L(CleaningFmt), lang::Get(kCatKeys[cur].nameKey));
                break;
            case CleanState::Done:
                big = L(AllClean);
                snprintf(cap, sizeof(cap), "%s %s \xC2\xB7 %s", FormatSize(g_freed).c_str(), L(Freed), L(Spotless));
                break;
            }
            ui::Text(dl, F.bold,theme::size::Display, p + px(22, 20), Gray(0.97f), big.c_str());
            ui::Text(dl, F.regular,theme::size::Body, p + px(22, 62), Gray(0.50f), cap);

            const bool  scanning = g_cleanState == CleanState::Scanning;
            const bool  cleaning = g_cleanState == CleanState::Cleaning;
            const float bw1 = px(112), bw2 = px(132), bh = px(40);

            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - px(22) - bw2 - px(10) - bw1, p.y + px(24)));
            // Bir tweak import'u çalışırken %TEMP% üzerinde sahip; temizleyici bu yüzden
            // .reg dosyasını içeri aktarma sırasında silmek yerine uygulama bitene kadar pasif kalır.
            const bool cleanerBlocked = cleaning || g_applyingReg.load();

            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - px(22) - bw2 - px(10) - bw1, p.y + px(24)));
            ImGui::BeginDisabled(cleaning);
            char scanLabel[64];
            snprintf(scanLabel, sizeof(scanLabel), "%s###scan", g_cleanState == CleanState::Idle ? L(Scan) : L(Rescan));
            if (ui::Button(scanLabel, ImVec2(bw1, bh), ButtonStyle::Secondary, Icon::Search, scanning))
                StartScan();
            ImGui::EndDisabled();

            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - px(22) - bw2, p.y + px(24)));
            ImGui::BeginDisabled(cleanerBlocked || !(g_cleanState == CleanState::Ready && total > 0.5f));
            if (ui::Button(L(CleanNow), ImVec2(bw2, bh), ButtonStyle::Primary, Icon::Cleaner, cleaning))
                StartClean();
            ImGui::EndDisabled();

            const float frac = (scanning || cleaning) ? g_cleanP
                             : (g_cleanState == CleanState::Ready || g_cleanState == CleanState::Done) ? 1.0f : 0.0f;
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(22), p.y + hh - px(28)));
            ui::ProgressBar("##cleanprog", frac, ImVec2(cw - px(44), px(5)));
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(cw, hh));

            ui::SectionLabel(L(Categories));

            const float colw = floorf((cw - gap) * 0.5f);
            ImGui::BeginDisabled(scanning || cleaning);
            ImVec2 row;
            for (int i = 0; i < n; ++i)
            {
                Category& c = g_cats[i];
                const int col = i % 2;
                if (col == 0)
                    row = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(row + ImVec2(col * (colw + gap), 0));

                std::string right;
                if (!c.enabled)                          right = L(Skipped);
                else if (g_cleanState == CleanState::Idle) right = "";
                else if (scanning && c.found <= 0.0f)    right = "...";
                else                                     right = FormatSize(c.found);

                ui::CheckRow(lang::Get(kCatKeys[i].nameKey), lang::Get(kCatKeys[i].descKey), right.c_str(), &c.enabled, colw);
                if (col == 1 || i == n - 1)
                {
                    ImGui::SetCursorScreenPos(row);
                    ImGui::Dummy(ImVec2(cw, px(64)));
                }
            }
            ImGui::EndDisabled();
        }

        void ToggleGrid(std::vector<Tweak>& list, int cat, float cw)
        {
            const float gap  = px(14);
            const float colw = floorf((cw - gap) * 0.5f);
            ImVec2 row;
            for (int i = 0; i < (int)list.size(); ++i)
            {
                const int col = i % 2;
                if (col == 0)
                    row = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(row + ImVec2(col * (colw + gap), 0));
                const bool prev = list[i].on;
                ui::ToggleCard(lang::Get(S::TweakNameKey(cat, i)),
                               lang::Get(S::TweakDescKey(cat, i)),
                               &list[i].on, colw);
                if (list[i].on != prev && i < 16)
                {
                    g_dirty[cat][i] = true;
                    g_dirtyAny[cat] = true;
                }
                if (col == 1 || i == (int)list.size() - 1)
                {
                    ImGui::SetCursorScreenPos(row);
                    ImGui::Dummy(ImVec2(cw, px(64)));
                }
            }
        }

        int g_ramPick = -1;   // seçili preset; Init() registry'yi okuyana kadar -1

        // RAM boyutu çipleri ve bir uygulama çubuğu. Kurulu RAM ile eşleşen preset vurgulanır.
        void RamOptimizer(float cw)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const int   n  = ram::PresetCount();
            const int   detected = ram::DetectedGB();

            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float  ch = px(238);
            ui::Card(dl, p0, p0 + ImVec2(cw, ch));
            ui::Text(dl, F.bold,theme::size::Title, p0 + px(18, 16), Gray(0.96f), L(RamOptimization));
            ui::Text(dl, F.regular,theme::size::Body, p0 + px(18, 38), Gray(theme::ink::Secondary), L(RamOptDesc));

            // sağ üstte kurulu bellek rozeti
            {
                char badge[64];
                snprintf(badge, sizeof(badge), "%s \xC2\xB7 %d GB", L(InstalledRam), detected);
                const ImVec2 bs = ui::TextSize(F.medium,theme::size::Caption, badge);
                const ImVec2 b0(p0.x + cw - px(18) - bs.x - px(20), p0.y + px(18));
                const ImVec2 b1 = b0 + ImVec2(bs.x + px(20), px(24));
                dl->AddRectFilled(b0, b1, White(0.06f), px(7));
                dl->AddRect(b0, b1, White(0.12f), px(7), 0, ImMax(1.0f, px(1)));
                ui::Text(dl, F.medium,theme::size::Caption, ImVec2(b0.x + px(10), b0.y + (px(24) - bs.y) * 0.5f), Gray(0.80f), badge);
            }

            // çip ızgarası
            const float gap = px(8), cols = 7.0f;
            const float chipW = floorf((cw - px(36) - gap * (cols - 1)) / cols), chipH = px(38);
            for (int i = 0; i < n; ++i)
            {
                const int   col = i % (int)cols, rowi = i / (int)cols;
                const ImVec2 c0(p0.x + px(18) + col * (chipW + gap), p0.y + px(74) + rowi * (chipH + gap));
                const ImVec2 c1 = c0 + ImVec2(chipW, chipH);

                ImGui::SetCursorScreenPos(c0);
                char id[32];
                snprintf(id, sizeof(id), "##ram%d", i);
                const bool clicked = ImGui::InvisibleButton(id, ImVec2(chipW, chipH));
                const bool hov     = ImGui::IsItemHovered();
                if (clicked) g_ramPick = i;

                const bool sel  = g_ramPick == i;
                const bool rec  = ram::PresetGB(i) == detected;
                const ImGuiID aid = ImGui::GetID(id);
                const float   sa  = ui::Anim(ui::Key(aid, "s"), sel ? 1.0f : 0.0f, 14.0f);
                const float   ha  = ui::Anim(ui::Key(aid, "h"), hov ? 1.0f : 0.0f, 14.0f);

                dl->AddRectFilled(c0, c1, White(0.045f + 0.05f * ha + 0.80f * sa), px(9));
                dl->AddRect(c0, c1, White(sel ? 0.0f : (rec ? 0.26f : 0.09f)), px(9), 0, ImMax(1.0f, px(1)));

                const char*  nm = ram::PresetName(i);
                const ImVec2 ts = ui::TextSize(sel ? F.bold : F.medium,theme::size::Body, nm);
                ui::Text(dl, sel ? F.bold : F.medium,theme::size::Body,
                         ImVec2((c0.x + c1.x - ts.x) * 0.5f, (c0.y + c1.y - ts.y) * 0.5f),
                         sel ? Gray(0.05f) : Gray(rec ? 0.92f : 0.60f), nm);

                // küçük nokta, kurulu RAM ile eşleşen preset'i işaretler
                if (rec && !sel)
                    dl->AddCircleFilled(ImVec2(c1.x - px(8), c0.y + px(8)), px(2.5f), White(0.75f), 10);
            }

            // uygulama çubuğu
            const float by = p0.y + ch - px(50);
            dl->AddLine(ImVec2(p0.x + px(1), by), ImVec2(p0.x + cw - px(1), by), White(0.06f));

            char cur[96];
            const int live = ram::CurrentPreset();
            snprintf(cur, sizeof(cur), "%s: %s", L(CurrentProfile),
                     live < 0 ? L(Custom) : ram::PresetName(live));
            ui::Text(dl, F.regular,theme::size::Caption, ImVec2(p0.x + px(18), by + px(19)), Gray(0.50f), cur);

            const float bw = px(150);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + cw - px(18) - bw, by + px(10)));
            ImGui::BeginDisabled(g_ramPick < 0 || g_ramPick == live);
            if (ui::Button(L(ApplyRamProfile), ImVec2(bw, px(32)), ButtonStyle::Primary, Icon::Memory))
            {
                g_pendingRam = g_ramPick;
                elevate::Pending pending;
                CollectPendingFromPages(pending);
                if (elevate::ApplyOrDelegate(pending) == elevate::Result::Applied)
                    ui::Notify(Toast::Success, L(RamProfileApplied), L(RamRestartNote));
                g_pendingRam = -1;   // devredildiyse süreç zaten kapanmıştı
            }
            ImGui::EndDisabled();

            ImGui::SetCursorScreenPos(p0);
            ImGui::Dummy(ImVec2(cw, ch));
        }

        void PageTweaks(float cw)
        {
            const char* const cats[] = { L(Performance), L(Gaming), L(Privacy), L(Visual), L(Games), L(FiveM), L(Delay) };
            const double now = ImGui::GetTime();

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float segW = ImMin(px(640), cw - px(240));
            if (ui::Segmented("##tweakcat", cats, kTweakCats, &g_tweakCat, segW))
                g_tweakCatTime = now;

            std::vector<Tweak>& list = g_tweaks[g_tweakCat];
            const float bh = px(38), bwA = px(112), bwR = px(96);
            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - bwA, p.y));
            // Uygulama %TEMP% içine bir import .reg yazar, bu yüzden ikisi birbirini dışlar.
            ImGui::BeginDisabled(!g_dirtyAny[g_tweakCat] || g_applying || g_cleaning.load());
            if (ui::Button(g_dirtyAny[g_tweakCat] ? L(ApplyChanges) : L(NoChanges),
                           ImVec2(bwA, bh), ButtonStyle::Primary, Icon::Check, g_applying))
            {
                g_applying   = true;
                g_applyStart = now;
            }
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - bwA - px(10) - bwR, p.y));
            if (ui::Button(L(Reset), ImVec2(bwR, bh), ButtonStyle::Secondary, Icon::Refresh))
            {
                // Bir anahtarı kapatmak da bir değişikliktir; kirli olarak işaretlenmezse bir sonraki
                // Uygula'nın yazacak bir şeyi kalmaz ve registry az önce burada kapatılan
                // ayarı korumaya devam eder.
                for (int i = 0; i < (int)list.size(); ++i)
                {
                    if (!list[i].on) continue;
                    list[i].on = false;
                    if (i < 16) g_dirty[g_tweakCat][i] = true;
                    g_dirtyAny[g_tweakCat] = true;
                }
                ui::Notify(Toast::Info, L(DefaultsRestored), L(DefaultsRestoredDesc));
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(cw, bh));

            // Geri al: uygulama öncesi alınan yedeği içe aktarır. Yedekleme/geri alma
            // HKLM'ye yazdığı için yetki ister; tıpkı "Uygula" gibi yükseltilmiş
            // sürece devredilir. Kullanıcı UAC'i bir kez daha görür.
            {
                std::wstring yedek = g_lastBackup;
                if (yedek.empty()) yedek = backup::Latest();

                ImGui::SetCursorScreenPos(ImVec2(p.x + cw - bwR, p.y + bh + px(8)));
                ImGui::BeginDisabled(yedek.empty() || g_applying);
                if (ui::Button(L(UndoChanges), ImVec2(bwR, px(30)), ButtonStyle::Secondary, Icon::Refresh))
                {
                    const size_t slash = yedek.find_last_of(L'\\');
                    elevate::Pending undo;
                    if (slash != std::wstring::npos)
                    {
                        const std::wstring ad = yedek.substr(slash + 1);
                        // Klasor adi ASCII; wchar_t -> char darlaltmasi acikca yazildi,
                        // implicit donustum C4244 uyarisi veriyordu.
                        undo.restoreFrom.clear();
                        for (wchar_t ch : ad)
                            undo.restoreFrom.push_back(ch < 128 ? (char)ch : '?');
                    }
                    elevate::ApplyOrDelegate(undo);
                    g_lastBackup.clear();
                    ui::Notify(Toast::Info, L(UndoChanges), L(UndoDone));
                }
                ImGui::EndDisabled();
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(cw, bh + px(38)));

            int on = 0;
            for (const Tweak& t : list)
                on += t.on ? 1 : 0;
            char info[128];
            snprintf(info, sizeof(info), L(TweaksEnabledFmt), on, (int)list.size(), cats[g_tweakCat]);
            ui::Label(theme::fonts.regular,theme::size::Body, theme::ink::Secondary, info);

            const float a = ImSaturate((float)(now - g_tweakCatTime) / 0.25f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
            ToggleGrid(list, g_tweakCat, cw);
            ImGui::PopStyleVar();

            ui::SectionLabel(L(RamOptimization));
            RamOptimizer(cw);
        }

        void PageNetwork(float cw)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const double now = ImGui::GetTime();
            char v[64];

            // gecikme grafiği
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float gh = px(210);
            ui::Card(dl, p, p + ImVec2(cw, gh));
            ui::Text(dl, F.bold,theme::size::Title, p + px(18, 16), Gray(0.96f), L(Latency));

            // Sondaç ölmüşse donmuş bir değeri göstermek yanlış olur; böyle bir
            // durumda "ölçülmedi" kabul edilir. Etiketten önce hesaplanır çünkü
            // ikisi de aynı koşula bağlı.
            const bool stale = g_pingSampleAt < 0.0 ||
                               (now - g_pingSampleAt) > network::kLatencyStaleSec;
            const bool have = g_pingSampled && !stale && g_pingFilled > 0;

            {
                char rtLabel[128];
                snprintf(rtLabel, sizeof(rtLabel), "%s \xC2\xB7 %s", L(RoundTrip),
                         have ? "1.1.1.1" : L(Simulated));
                ui::Text(dl, F.regular,theme::size::Body, p + px(18, 38), Gray(theme::ink::Secondary), rtLabel);
            }

            const float cur = ui::Anim(ImGui::GetID("##pingcur"), g_latency, 6.0f);

            // Jitter YALNIZCA gerçek örnekler üzerinden hesaplanır. Doldurulmamış
            // kutular sıfır olduğu için, onları dahil etmek 138 ms'lik sabit bir
            // bağlantıda 41 ms jitter üretiyordu.
            const int window = have ? ImMin(10, g_pingFilled) : 0;
            float mean = 0.0f, var = 0.0f;
            for (int i = (int)g_ping.size() - window; i < (int)g_ping.size(); ++i)
                mean += g_ping[i] / (float)window;
            for (int i = (int)g_ping.size() - window; i < (int)g_ping.size(); ++i)
                var += (g_ping[i] - mean) * (g_ping[i] - mean) / (float)window;

            if (have)
            {
                snprintf(v, sizeof(v), "%.0f ms", cur);
                const ImVec2 cs = ui::TextSize(F.bold, theme::size::Heading, v);
                ui::Text(dl, F.bold, theme::size::Heading, ImVec2(p.x + cw - px(18) - cs.x, p.y + px(14)), Gray(theme::ink::Primary), v);

                // Tek örnekten sapma sıfırdır; "0.0 ms jitter" yazmak ölçülmüş bir
                // kesinlik izlenimi verirdi. İki örnekten sonra gösterilir.
                if (window >= 2)
                {
                    snprintf(v, sizeof(v), "%s %.1f ms", L(Jitter), sqrtf(var));
                    const ImVec2 js = ui::TextSize(F.regular, theme::size::Caption, v);
                    ui::Text(dl, F.regular, theme::size::Caption,
                             ImVec2(p.x + cw - px(18) - js.x, p.y + px(40)), Gray(theme::ink::Tertiary), v);
                }
            }
            else
            {
                const char* pending = L(PingUnavailable);
                const ImVec2 cs = ui::TextSize(F.medium, theme::size::Title, pending);
                ui::Text(dl, F.medium, theme::size::Title,
                         ImVec2(p.x + cw - px(18) - cs.x, p.y + px(18)), Gray(theme::ink::Secondary), pending);
            }

            // Düşey eksen gerçek zirveye göre. Sabit bir tavan (60 ms) 138 ms'lik
            // bir bağlantıyı tepede sabitler ve 61 ms ile 600 ms arasındaki farkı
            // tamamen yok ederdi.
            float peak = 60.0f;
            for (int i = (int)g_ping.size() - g_pingFilled; i < (int)g_ping.size(); ++i)
                peak = ImMax(peak, g_ping[i]);
            peak = ceilf(peak / 50.0f) * 50.0f;   // 50 ms'lik dilimler
            // Yumuşatma: her tepe ekseni zıplatıp geri indirip grafiği titrettiğinden
            // daha okunaklı.
            const float vmax = ui::Anim(ImGui::GetID("##pingvmax"), peak, 3.0f);

            // Kaydırma kendi saatinden gelir. g_lastSample CPU grafiğinin 0.5
            // saniyelik sayacıydı; gecikme grafiği onunla sürülünce örnek başına
            // değil, yarım saniyede bir kayıyordu.
            const float scroll = g_pingSampleAt < 0.0
                ? 0.0f
                : ImSaturate((float)((now - g_pingSampleAt) / network::kLatencyIntervalSec));

            // Yalnızca dolu olan kuyruk çizilir. Boş kutular sıfır olarak ekrana
            // yansıdığı için grafik "0 ms gecikme" izlenimi veriyordu - ekrandaki
            // düz çizgi buydu.
            ui::Graph(dl, p + px(18, 70), p + ImVec2(cw - px(18), gh - px(18)),
                      g_ping.data() + (g_ping.size() - g_pingFilled), g_pingFilled,
                      0.0f, vmax, scroll);
            ImGui::Dummy(ImVec2(cw, gh));

            // DNS
            // Değer olarak değil işaretçi olarak tutulur: dizge her çizimde dil tablosundan
            // çözüldüğü için dil değişimi anında yansır.
            static const char* const dns[] = { L(AutomaticDns), "Cloudflare", "Google", "Quad9" };
            p = ImGui::GetCursorScreenPos();
            const float dh = px(136);
            ui::Card(dl, p, p + ImVec2(cw, dh));
            ui::Text(dl, F.bold,theme::size::Title, p + px(18, 16), Gray(0.96f), L(DnsProvider));
            ui::Text(dl, F.regular,theme::size::Body, p + px(18, 38), Gray(theme::ink::Secondary), L(ResolverUsed));

            const float fbw = px(160);
            ImGui::SetCursorScreenPos(p + px(18, 78));
            if (ui::Segmented("##dns", dns, 4, &g_dns, ImMin(px(440), cw - px(36) - fbw - px(14))))
            {
                // DNS sunucusunu değiştirmek HKLM'ye yazar, yani yetki ister. Süreç
                // yükseltilmiş değilse iş "runas" ile yükseltilmiş sürece devredilir.
                g_pendingDns = g_dns;
                elevate::Pending pending;
                CollectPendingFromPages(pending);
                if (elevate::ApplyOrDelegate(pending) == elevate::Result::Applied)
                    ui::Notify(Toast::Success, L(DnsUpdated), dns[g_dns]);
                g_pendingDns = -1;   // devredildiyse süreç zaten kapanmıştı
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x + cw - px(18) - fbw, p.y + px(78)));
            if (ui::Button(L(FlushDnsCache), ImVec2(fbw, px(38)), ButtonStyle::Secondary, Icon::Refresh))
            {
                if (network::FlushDns())
                    ui::Notify(Toast::Success, L(DnsFlushed), L(DnsFlushedDesc));
                else
                    ui::Notify(Toast::Warning, L(DnsFlushed), L(NeedAdmin));
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(cw, dh));

            // bağlantı bilgileri
            {
                static std::string adp = network::AdapterName(), lip = network::LocalIP(), gip = network::GatewayIP();
                p = ImGui::GetCursorScreenPos();
                const float ih = px(136);
                ui::Card(dl, p, p + ImVec2(cw, ih));
                ui::Text(dl, F.bold,theme::size::Title, p + px(18, 16), Gray(0.96f), L(Connection));
                ui::Text(dl, F.regular,theme::size::Body, p + px(18, 38), Gray(theme::ink::Secondary), L(ActiveAdapter));
                const float rw = cw - px(36);
                ImGui::SetCursorScreenPos(p + px(18, 64));
                ImGui::BeginGroup();
                InfoRow(L(AdapterName), adp.c_str(), rw);
                InfoRow(L(LocalIp), lip.c_str(), rw);
                InfoRow(L(GatewayName), gip.c_str(), rw, false);
                ImGui::EndGroup();
                ImGui::SetCursorScreenPos(p);
                ImGui::Dummy(ImVec2(cw, ih));
            }

            ui::SectionLabel(L(Optimizations));
            {
                const float gap = px(14);
                const float colw = floorf((cw - gap) * 0.5f);
                ImVec2 row;
                for (int i = 0; i < (int)g_netTweaks.size(); ++i)
                {
                    const int col = i % 2;
                    if (col == 0) row = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(row + ImVec2(col * (colw + gap), 0));
                    const bool supported = network::IsSupported(i);

                    if (supported)
                    {
                        const bool prev = g_netTweaks[i].on;
                        ui::ToggleCard(lang::Get(S::NetNameKey(i)),
                                       lang::Get(S::NetDescKey(i)),
                                       &g_netTweaks[i].on, colw);
                        if (g_netTweaks[i].on != prev)
                        {
                            if (network::ApplyTweak(i, g_netTweaks[i].on))
                                g_netTweaks[i].on = network::ReadTweak(i); // gerçekte ne tutulduysa onu göster
                            else
                            {
                                g_netTweaks[i].on = prev; // yazma başarısız, anahtarı eski konumuna geri al
                                ui::Notify(Toast::Warning, lang::Get(S::NetNameKey(i)), L(NeedAdmin));
                            }
                        }
                    }
                    else
                    {
                        // Hiçbir şey yapmayan bir denetim gibi görünmektense soluklaştırılır.
                        ImGui::BeginDisabled();
                        ui::ToggleCard(lang::Get(S::NetNameKey(i)),
                                       lang::Get(S::NetDescKey(i)),
                                       &g_netTweaks[i].on, colw);
                        ImGui::EndDisabled();
                        const ImVec2 c0 = row + ImVec2(col * (colw + gap), 0);
                        if (ImGui::IsMouseHoveringRect(c0, c0 + ImVec2(colw, px(64))))
                            ImGui::SetTooltip("%s", L(UnsupportedTweak));
                    }
                    if (col == 1 || i == (int)g_netTweaks.size() - 1)
                    {
                        ImGui::SetCursorScreenPos(row);
                        ImGui::Dummy(ImVec2(cw, px(64)));
                    }
                }
            }
        }

        void SysInfoSection(const char* title, Icon icon, float cw)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const ImVec2 p  = ImGui::GetCursorScreenPos();
            const float  ih = px(24);
            dl->AddRectFilled(p, ImVec2(p.x + px(4), p.y + ih), White(0.80f), px(2));
            icons::Draw(dl, icon, ImVec2(p.x + px(20), p.y + ih * 0.5f), px(10), Gray(0.60f), px(1.2f));
            ui::TextSpaced(dl, F.bold,theme::size::Meta, ImVec2(p.x + px(36), p.y + (ih - ui::TextSize(F.bold,theme::size::Meta, title).y) * 0.5f),
                           Gray(0.55f), title, px(1.5f));
            ImGui::Dummy(ImVec2(cw, ih + px(6)));
        }

        void PageSystemInfo(float cw)
        {
            const float gap = px(14);
            const float hw  = floorf((cw - gap) * 0.5f);

            static bool gathered = false;
            if (!gathered) { sysdetail::Gather(); gathered = true; }
            const auto& d  = sysdetail::Get();
            static const std::string cpuN = sys::CpuName(), gpuN = sys::GpuName(),
                                     os = sys::OsName(), pc = sys::ComputerName();

            // Donanım bölümü
            ImGui::BeginGroup();
            SysInfoSection(L(HardwareInfo), Icon::Chip, cw);

            ui::BeginCard("##sicpu", hw, L(Processor));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                InfoRow(L(CPUName), cpuN.c_str(), w);
                char cores[16], threads[16];
                snprintf(cores, sizeof(cores), "%d", d.cpuCores);
                snprintf(threads, sizeof(threads), "%d", d.cpuThreads);
                InfoRow(L(CPUCores), cores, w);
                InfoRow(L(CPUThreads), threads, w);
                InfoRow(L(CPUClock), d.cpuClock.c_str(), w, false);
            }
            ui::EndCard();
            ImGui::SameLine(0, gap);
            ui::BeginCard("##sigpu", hw, L(GPUName));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                InfoRow(L(GPUName), gpuN.c_str(), w);
                InfoRow(L(GPUVRAM), d.gpuVram.c_str(), w);
                InfoRow(L(GPUDriver), d.gpuDriver.c_str(), w);
                InfoRow(L(DirectXVersion), d.directX.c_str(), w, false);
            }
            ui::EndCard();

            ui::BeginCard("##siram", hw, L(RAMTotal));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                InfoRow(L(RAMTotal), d.ramTotal.c_str(), w, false);
            }
            ui::EndCard();
            ImGui::SameLine(0, gap);
            ui::BeginCard("##siboard", hw, L(Motherboard));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                InfoRow(L(Motherboard), d.motherboard.c_str(), w);
                InfoRow(L(BIOSVersion), d.biosVersion.c_str(), w);
                InfoRow(L(BIOSMode), BiosModeText(d.biosLegacy), w, false);
            }
            ui::EndCard();
            ImGui::EndGroup();

            // Güvenlik bölümü
            ImGui::BeginGroup();
            SysInfoSection(L(SecurityInfo), Icon::Lock, cw);

            ui::BeginCard("##sisec", hw, L(SecurityInfo));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                InfoRow(L(SecureBoot), SecureBootText(d.secureBoot), w);
                InfoRow(L(Virtualization), VirtText(d.virtState), w);
                InfoRow(L(BIOSMode), BiosModeText(d.biosLegacy), w, false);
            }
            ui::EndCard();
            ImGui::SameLine(0, gap);
            ui::BeginCard("##sisw", hw, L(SoftwareInfo));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                InfoRow(L(OperatingSystem), os.c_str(), w);
                InfoRow(L(InstallDate), d.installDate.c_str(), w);
                InfoRow(L(DisplayResolution), d.displayRes.c_str(), w);
                InfoRow(L(SystemLocale), d.systemLocale.c_str(), w);
                InfoRow(L(Computer), pc.c_str(), w, false);
            }
            ui::EndCard();
            ImGui::EndGroup();
        }

        void PageSettings(float cw)
        {
            const float gap  = px(14);
            const float colw = floorf((cw - gap) * 0.5f);

            ImGui::BeginGroup();
            ui::BeginCard("##visual", colw, L(VisualEffects), L(TuneBg));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                auto& s = fx::settings;
                ui::ToggleCard(L(Particles), nullptr, &s.particles, w, false);
                ui::ToggleCard(L(ConnectionLines), nullptr, &s.lines, w, false);
                ui::ToggleCard(L(MouseInteraction), nullptr, &s.mouse, w, false);
                ui::ToggleCard(L(TopLight), nullptr, &s.glow, w, false);
                ui::ToggleCard(L(LightSweep), nullptr, &s.sweep, w, false);
                ImGui::Dummy(ImVec2(0, px(2)));
                ui::SliderInt(L(ParticleCount), &s.count, 20, 220, w);
                ui::Slider(L(ParticleSpeed), &s.speed, 0.2f, 3.0f, "%.1fx", w);
            }
            ui::EndCard();
            ImGui::EndGroup();

            ImGui::SameLine(0, gap);

            ImGui::BeginGroup();
            ui::BeginCard("##general", colw, L(General), L(AppBehaviour));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                ui::ToggleCard(L(RememberLicense), nullptr, &g_remember, w, false);
                ui::ToggleCard(L(Notifications), nullptr, &ui::notificationsEnabled, w, false);
                wchar_t exePath[MAX_PATH] = {};
                ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                const bool prevStartup = g_startup;
                ui::ToggleCard(L(LaunchStartup), nullptr, &g_startup, w, false);
                if (g_startup != prevStartup)
                {
                    // Run girdisi yazılır, ardından registry'nin söylediğine geri dönülür; böylece
                    // bir başarısızlık (kısıtlanmış profil) anahtarı hiç olmamış bir şeyi
                    // iddia eder bırakmaz.
                    g_startup = network::SetRunAtStartup(exePath, g_startup) &&
                                network::GetRunAtStartup(exePath);
                    ui::Notify(g_startup ? Toast::Success : Toast::Warning,
                               L(LaunchStartup), g_startup ? L(StartupEnabled) : L(NeedAdmin));
                }

                const bool prevTray = g_tray;
                ui::ToggleCard(L(MinimizeToTray), nullptr, &g_tray, w, false);
                if (g_tray != prevTray)
                {
                    tray::SetEnabled(g_tray);
                    if (g_tray && !tray::IsAvailable())
                    {
                        g_tray = false;   // shell reddetti: açık gibi davranma
                        ui::Notify(Toast::Warning, L(MinimizeToTray), L(TrayUnavailable));
                    }
                }
            }
            ui::EndCard();

            // Dil kartı
            ui::BeginCard("##langcard", colw, L(Language));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                static const char* const langs[] = { "English", "T\xC3\xBCrk\xC3\xA7\x65" };
                static int langIdx = (int)lang::Current();
                if (ui::Segmented("##lang", langs, 2, &langIdx, w))
                {
                    lang::Set((lang::Id)langIdx);
                    lang::Save();   // kaydedilmezse seçim yeniden açılışta İngilizceye döner
                }
            }
            ui::EndCard();

            ui::BeginCard("##account", colw, L(Account));
            {
                const float w = ImGui::GetContentRegionAvail().x;
                const auto& li = license::Current();
                const std::string masked = license::Mask(li.key);
                InfoRow(L(LicenseKey), masked.c_str(), w);
                InfoRow(L(Plan), PlanText(li), w);
                InfoRow(L(Expires), ExpiryText(li), w, false);
                ImGui::Dummy(ImVec2(0, px(4)));

                // Lisans ekranı her anahtarı kabul ettiği için bu kart gerçek bir satın alım
                // aktifmiş gibi okunmamalı.
                ui::Label(theme::fonts.regular,theme::size::Meta, theme::ink::Tertiary, L(DemoLicenseNote));

                if (ui::Button(L(SignOut), ImVec2(w, px(40)), ButtonStyle::Secondary, Icon::Logout))
                    SignOut();

                char ver[96];
                snprintf(ver, sizeof(ver), "%s %s \xC2\xB7 %s", brand::kNameA, DisplayVersion(), L(LicenseLine));
                ui::Label(theme::fonts.regular,theme::size::Meta, theme::ink::Tertiary, ver);
            }
            ui::EndCard();
            ImGui::EndGroup();
        }

        // ------------------------------------------------------------------ hakkında sayfası

        // Bir bağlantı satırı: solda marka ikonu, sağda etiket ve kullanıcı adı.
        //
        // ui::Button yerine InvisibleButton üzerine kuruldu çünkü burada sabit bir sütunda
        // iki satır metin ve bir ikon gerekiyor. Belirgin alternatif — ui::Button çağırıp
        // yerleşimin imleç muhasebesiyle çatışacak şekilde imleci geri alıp üzerine çizmek —
        // üst pencere büyüdüğünü sanıyor ve kendi "SetCursorPos used to extend window
        // boundaries" kontrolünü tetikliyor.
        //
        // InvisibleButton hem yerleşim alanını ayırır hem de dikdörtgeni bildirir; böylece
        // sonradan konum düzeltmeye gerek kalmaz.
        void Line(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float th)
        {
            dl->AddLine(a, b, col, th);
        }

        void LinkButton(Icon icon, const char* label, const char* handle, const wchar_t* url, float w)
        {
            if (ImGui::InvisibleButton(label, ImVec2(w, px(54))))
                OpenUrl(url);

            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            const float hv = ImGui::IsItemHovered() ? 1.0f : 0.0f;
            const float h  = b.y - a.y;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // ButtonStyle::Secondary ile aynı yüzey işlemesi kullanılır; böylece satırlar yapıştırılmış
            // web bağlantıları gibi değil, uygulamanın bir parçası gibi okunur.
            const float r = px(9);
            dl->AddRectFilled(a, b, White(0.04f + 0.04f * hv), r);
            dl->AddRect(a, b, White(0.10f + 0.14f * hv), r, 0, ImMax(1.0f, px(1)));

            const float iconX = a.x + px(28);
            const float midY  = a.y + h * 0.5f;
            icons::Draw(dl, icon, ImVec2(iconX, midY), px(21), Gray(0.80f + 0.18f * hv));

            const float textX = iconX + px(22);
            ui::Text(dl, theme::fonts.medium,theme::size::Body,
                     ImVec2(textX, midY - px(15)), Gray(0.80f + 0.18f * hv), label);
            ui::Text(dl, theme::fonts.regular,theme::size::Meta,
                     ImVec2(textX, midY + px(2)), Gray(0.40f + 0.15f * hv), handle);

            // Sağa bakan chevron, her yerde "bu uygulamadan çıkıyor" işareti.
            const float cx = b.x - px(24);
            Line(dl, ImVec2(cx - px(4), midY - px(6)), ImVec2(cx + px(2), midY), Gray(0.35f + 0.2f * hv), ImMax(1.0f, px(1.4f)));
            Line(dl, ImVec2(cx + px(2), midY), ImVec2(cx - px(4), midY + px(6)), Gray(0.35f + 0.2f * hv), ImMax(1.0f, px(1.4f)));
        }

        void PageAbout(float cw)
        {
            // ---- kimlik ---------------------------------------------------------
            ui::BeginCard("##about_id", cw, L(About));
            {
                const float w = ImGui::GetContentRegionAvail().x;

                char head[96];
                snprintf(head, sizeof(head), "%s %s", brand::kNameA, DisplayVersion());

                ImGui::Dummy(ImVec2(0, px(4)));
                ImGui::TextUnformatted(brand::kNameA);
                ImGui::Dummy(ImVec2(0, px(2)));
                ImGui::TextDisabled("%s", L(Tagline));

                ImGui::Dummy(ImVec2(0, px(10)));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, px(4)));

                InfoRow(L(Version),     head, w);
                InfoRow(L(LicenseShort), "MIT", w, false);
            }
            ui::EndCard();

            ImGui::Dummy(ImVec2(0, px(14)));

            // Bağlantılar her şeyden önce gelir: bu sayfanın tek etkileşimli parçası onlar ve diğer
            // kartların altına yığılırsa varsayılan pencere boyutunda görünmez konuma düşerler.
            ui::BeginCard("##about_links", cw, L(LinksHeading));
            {
                const float lw = ImGui::GetContentRegionAvail().x;
                LinkButton(Icon::Instagram, "Instagram",     brand::kInstagramHandle, brand::kInstagramUrlW, lw);
                ImGui::Dummy(ImVec2(0, px(8)));
                LinkButton(Icon::GitHub,   "GitHub",       brand::kGitHubHandle,    brand::kGitHubUrlW,    lw);
                ImGui::Dummy(ImVec2(0, px(8)));
                LinkButton(Icon::Code,     L(SourceCode), brand::kRepoSlug,        brand::kRepoUrlW,     lw);
            }
            ui::EndCard();

            ImGui::Dummy(ImVec2(0, px(14)));

            // ---- bildirimler ---------------------------------------------------
            ui::BeginCard("##about_notices", cw, L(NoticesHeading));
            {
                ImGui::TextWrapped("%s", L(DemoLicenseNote));
                ImGui::Dummy(ImVec2(0, px(6)));
                ImGui::TextWrapped("%s", L(ElevationNote));
            }
            ui::EndCard();
        }

        // ------------------------------------------------------------------ ana iskelet

        void DrawSidebar(const ImVec2& ds, float sbw)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const double now = ImGui::GetTime();

            dl->AddRectFilled(ImVec2(0, 0), ImVec2(sbw, ds.y), ImGui::GetColorU32(ImVec4(0.03f, 0.03f, 0.035f, 0.80f)));
            dl->AddRectFilledMultiColor(ImVec2(sbw - ImMax(1.0f, px(1)), 0), ImVec2(sbw, ds.y), White(0.10f), White(0.10f), White(0.02f), White(0.02f));

            // marka
            DrawLogo(dl, ImVec2(px(34), px(39)), px(24), 0.8f);
            ui::TextSpaced(dl, F.bold,theme::size::Title, ImVec2(px(58), px(27)), Gray(0.97f), brand::kNameA, px(theme::track::Wide));
            const float vw = ui::SpacedSize(F.bold,theme::size::Title, brand::kNameA, px(theme::track::Wide)).x;
            const ImVec2 bs = ui::SpacedSize(F.medium,theme::size::Micro, L(OpenSource), px(theme::track::Micro));
            const ImVec2 b0(px(58) + vw + px(8), px(31));
            const ImVec2 b1 = b0 + ImVec2(bs.x + px(12), px(16));
            dl->AddRectFilled(b0, b1, White(0.06f), px(5));
            dl->AddRect(b0, b1, White(0.22f), px(5), 0, ImMax(1.0f, px(1)));
            ui::TextSpaced(dl, F.medium,theme::size::Micro, ImVec2(b0.x + px(6), b0.y + (px(16) - bs.y) * 0.5f), Gray(0.85f), L(OpenSource), px(theme::track::Micro));
            const float bsh = fmodf((float)now, 4.0f) / 1.2f;
            if (bsh < 1.0f)
                fx::Shine(dl, b0, b1, bsh, White(0.35f));

            ImGui::SetCursorScreenPos(ImVec2(px(20), px(86)));
            ui::SectionLabel("MENU");

            // sekmeler + kayan gösterge
            const float tabH = px(40), gap = px(4), tx = px(12), tw = sbw - px(24), ty0 = px(108);
            const float iy = ui::Anim(ImGui::GetID("##tabind"), ty0 + g_tab * (tabH + gap), 16.0f);
            dl->AddRectFilled(ImVec2(tx, iy), ImVec2(tx + tw, iy + tabH), White(0.065f), px(9));
            dl->AddRect(ImVec2(tx, iy), ImVec2(tx + tw, iy + tabH), White(0.06f), px(9), 0, ImMax(1.0f, px(1)));
            fx::RadialGradient(dl, ImVec2(0, iy + tabH * 0.5f), px(30), px(26), White(0.22f), White(0.0f), 24);
            dl->AddRectFilled(ImVec2(0, iy + px(11)), ImVec2(px(3), iy + tabH - px(11)), White(0.95f), px(2));

            for (int i = 0; i < kTabCount; ++i)
            {
                ImGui::SetCursorScreenPos(ImVec2(tx, ty0 + i * (tabH + gap)));
                if (ui::Tab(lang::Get(kTabNameKeys[i]), kTabIcons[i], g_tab == i, ImVec2(tw, tabH)) && g_tab != i)
                {
                    g_tab     = i;
                    g_tabTime = now;
                }
            }

            // kullanıcı kartı
            const ImVec2 u0(tx, ds.y - px(76)), u1(tx + tw, ds.y - px(14));
            const float  cy = (u0.y + u1.y) * 0.5f;
            ui::Card(dl, u0, u1, px(12));
            const ImVec2 av(u0.x + px(28), cy);

            // Profil resmi olarak markanın kendisi çizilir. Doku hazır değilse
            // (kaynak eksik) baş harfe düşülür, böylece kart hiçbir koşulda
            // boş kalmaz.
            if (logo::Ready())
            {
                fx::RadialGradient(dl, av, px(28), px(28), White(0.14f), White(0.0f), 24);
                const float as = px(30) * 0.5f;
                logo::Draw(dl, ImVec2(av.x - as, av.y - as), ImVec2(av.x + as, av.y + as),
                           Gray(0.94f));
            }
            else
            {
                const std::string& user = license::Current().user;
                const char initial[2] = { user.empty() ? 'U' : (char)toupper((unsigned char)user[0]), 0 };
                fx::RadialGradient(dl, av, px(26), px(26), White(0.12f), White(0.0f), 24);
                dl->AddCircleFilled(av, px(16), White(0.94f), 32);
                const ImVec2 is = ui::TextSize(F.bold,theme::size::Title, initial);
                ui::Text(dl, F.bold,theme::size::Title, av - is * 0.5f, Gray(0.05f), initial);
            }

            dl->PushClipRect(u0, ImVec2(u1.x - px(42), u1.y), true);
            ui::Text(dl, F.medium,theme::size::Body, ImVec2(u0.x + px(52), cy - px(18)), Gray(0.95f), brand::kNameA);
            dl->AddCircleFilled(ImVec2(u0.x + px(55), cy + px(9)), px(3), White(0.9f), 12);
            ui::Text(dl, F.regular,theme::size::Caption, ImVec2(u0.x + px(63), cy + px(1)), Gray(0.50f), PlanText(license::Current()));
            dl->PopClipRect();

            ImGui::SetCursorScreenPos(ImVec2(u1.x - px(38), cy - px(14)));
            if (ui::IconButton("##signout", Icon::Logout, px(28, 28), px(14)))
                SignOut();
        }

        void DrawMain(const ImVec2& ds, float slide)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const auto& F  = theme::fonts;
            const double now = ImGui::GetTime();
            const float sbw = px(212);

            DrawSidebar(ds, sbw);

            const float hx = sbw + px(28);
            ui::Text(dl, F.bold,theme::size::PageTitle, ImVec2(hx, px(18)), Gray(0.97f), lang::Get(kTabNameKeys[g_tab]));
            ui::Text(dl, F.regular,theme::size::Body, ImVec2(hx, px(50)), Gray(0.48f), lang::Get(kTabSubKeys[g_tab]));
            dl->AddRectFilledMultiColor(ImVec2(sbw, px(80)), ImVec2(ds.x, px(80) + ImMax(1.0f, px(1))), White(0.07f), White(0.0f), White(0.0f), White(0.07f));

            float pa = ImSaturate((float)(now - g_tabTime) / 0.3f);
            pa = 1.0f - (1.0f - pa) * (1.0f - pa) * (1.0f - pa);
            const ImVec2 cmin(hx, px(96) + (1.0f - pa) * px(12) + slide);
            const ImVec2 csize(ds.x - px(14) - hx, ds.y - px(14) - cmin.y);

            ImGui::SetCursorScreenPos(cmin);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * pa);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(14), px(14)));
            char id[16];
            snprintf(id, sizeof(id), "##page%d", g_tab);
            ImGui::BeginChild(id, csize, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            {
                const float cw = ImGui::GetContentRegionAvail().x - px(14);
                switch (g_tab)
                {
                case 0: PageDashboard(cw);  break;
                case 1: PageCleaner(cw);    break;
                case 2: PageTweaks(cw);     break;
                case 3: PageNetwork(cw);    break;
                case 4: PageSystemInfo(cw); break;
                case 5: PageSettings(cw);   break;
                case 6: PageAbout(cw);      break;
                }
                ImGui::Dummy(ImVec2(0, px(4)));
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
        }
    }

    // ====================================================================== genel dışı

    void Init(HWND hwnd, float corner_radius)
    {
        g_hwnd   = hwnd;
        g_corner = corner_radius;
        sys::Update();

        // Herhangi bir dizge kurulmadan önce yüklenir; böylece ilk kare zaten kayıtlı
        // dilde çizilir ve İngilizce bir flaş olmaz.
        lang::Load();

        for (int c = 0; c < kTweakCats; ++c)
            for (int i = 0; i < (int)g_tweaks[c].size(); ++i)
                g_tweaks[c][i].on = tweaks::Read(c, i);
        for (int i = 0; i < (int)g_netTweaks.size(); ++i)
                g_netTweaks[i].on = network::IsSupported(i) && network::ReadTweak(i);

        g_ramPick = ram::CurrentPreset();

        network::StartLatencyProbe();

        wchar_t exePath[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        g_startup = network::GetRunAtStartup(exePath);

        const std::string saved = license::LoadSaved();
        if (!saved.empty())
        {
            strncpy_s(g_key, saved.c_str(), _TRUNCATE);
            g_remember = true;
        }
        

        ui::AnimSet(ImHashStr("##winalpha"), 0.0f); // açılışta soluklaşarak belirir

#ifdef TENGRI_DEV
        // Yalnızca geliştirme derlemesi: demo anahtarıyla otomatik giriş (sekmeler Frame() içinde dönüp durur).
        strncpy_s(g_key, "LIFE-2026-ABCD-WXYZ", _TRUNCATE);
        g_remember = false;
        TryActivate();
#endif
    }

    bool WantsQuit() { return g_quit; }

    // main.cpp'nin WndProc'ı bunu okuyarak küçültmenin pencereyi gizleyip gizlemeyeceğine karar verir.
    bool TrayEnabled() { return g_tray; }

    void Shutdown()
    {
        // Join burada, statik yıkıcıda değil: uzun tarama kapanış yolundan uzak tutulur ve
        // worker thread'in kapanış sırasında serbest bırakılmış global'lere dokunması engellenir.
        JoinAll();
        network::StopLatencyProbe();
    }

    void Frame()
    {
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 ds = io.DisplaySize;
        if (ds.x <= 0.0f || ds.y <= 0.0f)
            return;

        const double now = ImGui::GetTime();
        UpdateData(now);

#ifdef TENGRI_DEV
        if (g_screen == Screen::Main && !g_switching)
        {
            const int devTab = (int)((now - g_screenStart) / 2.5) % kTabCount;
            if (devTab != g_tab)
            {
                g_tab     = devTab;
                g_tabTime = now;
                if (g_tab == 1 && g_cleanState == CleanState::Idle)
                    StartScan();
            }
        }
#endif

        const float winA = ui::Anim(ImHashStr("##winalpha"), g_closing ? 0.0f : 1.0f, g_closing ? 12.0f : 4.0f);
        if (g_closing && winA < 0.02f)
            g_quit = true;

        const float scrA = ui::Anim(ImHashStr("##screen"), g_switching ? 0.0f : 1.0f, g_switching ? 14.0f : 8.0f);
        if (g_switching && scrA < 0.03f)
        {
            g_screen      = g_next;
            g_switching   = false;
            g_screenStart = now;
            if (g_screen == Screen::Main)
            {
                g_tab     = 0;
                g_tabTime = now;
            }
        }

        fx::DrawBackground(ImGui::GetBackgroundDrawList(), ImVec2(0, 0), ds, winA);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ds);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const float slide = (1.0f - scrA) * px(14);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, winA * scrA);
        switch (g_screen)
        {
        case Screen::Login:   DrawLogin(ds, slide);   break;
        case Screen::Loading: DrawLoading(ds, slide); break;
        case Screen::Main:    DrawMain(ds, slide);    break;
        }
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, winA);
        DrawWindowControls(ds);
        ImGui::PopStyleVar();

        HandleDrag();
        ImGui::End();

        // Işık süpürmesi arayüzün üzerinden de geçer, bildirimler en üstte kalır, sonra pencere çerçevesi çizilir
        fx::DrawSweep(ImGui::GetForegroundDrawList(), ImVec2(0, 0), ds, winA * 0.5f);
        ui::RenderNotifications(ds);
        ImGui::GetForegroundDrawList()->AddRect(ImVec2(0.5f, 0.5f), ds - ImVec2(0.5f, 0.5f),
                                                IM_COL32(255, 255, 255, (int)(26.0f * winA)), g_corner, 0, 1.0f);
    }
}
