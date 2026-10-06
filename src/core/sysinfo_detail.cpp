#include "sysinfo_detail.hpp"
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <dxgi.h>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "dxgi.lib")

namespace sysdetail
{
    namespace
    {
        Info g_info;
        bool g_gathered = false;

        std::string RegStr(HKEY root, const wchar_t* path, const wchar_t* name)
        {
            HKEY key;
            if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
                return {};
            wchar_t buf[512] = {};
            DWORD sz = sizeof(buf), type = 0;
            RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)buf, &sz);
            RegCloseKey(key);
            if (type != REG_SZ && type != REG_EXPAND_SZ) return {};
            char out[512];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
            return out;
        }

        DWORD RegDword(HKEY root, const wchar_t* path, const wchar_t* name)
        {
            HKEY key;
            if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
                return 0;
            DWORD val = 0, sz = sizeof(val), type = 0;
            RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)&val, &sz);
            RegCloseKey(key);
            return val;
        }

        // Ekran adaptörü sınıfını gezer ve DXGI'ın 0. indeks olarak bildirdiği adaptörle
        // eşleşen ilk sürücü adı taşıyan alt anahtarı döndürür. Ucuz bir yöntemdir ve
        // başarısızlığı gizlemez: çağıran, DirectX sürümünü ödünç almak yerine N/A gösterir.
        std::string DriverVersionForPrimaryAdapter()
        {
            static const wchar_t* kClass =
                L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

            HKEY cls;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kClass, 0, KEY_READ, &cls) != ERROR_SUCCESS)
                return {};

            std::string fallback;
            for (DWORD i = 0; i < 64; ++i)
            {
                wchar_t sub[32];
                swprintf_s(sub, L"%04u", i);
                HKEY h;
                if (RegOpenKeyExW(cls, sub, 0, KEY_READ, &h) != ERROR_SUCCESS)
                    continue;

                wchar_t ver[64] = {};
                DWORD vsz = sizeof(ver);
                const bool have = RegQueryValueExW(h, L"DriverVersion", nullptr, nullptr, (LPBYTE)ver, &vsz) == ERROR_SUCCESS &&
                                 ver[0] != 0;

                // "Primary Adapter", DXGI'ın ilk sırada gösterdiği adaptörü işaretler; ancak hiç
                // sürücü sürümü yokken hiçbir şey göstermemektense bir tane yedek
                // olarak saklamak daha iyidir.
                DWORD primary = 0;
                DWORD psz = sizeof(primary);
                RegQueryValueExW(h, L"Primary Adapter", nullptr, nullptr, (LPBYTE)&primary, &psz);

                if (have)
                {
                    char out[64];
                    WideCharToMultiByte(CP_UTF8, 0, ver, -1, out, sizeof(out), nullptr, nullptr);
                    if (primary == 1) { RegCloseKey(h); RegCloseKey(cls); return out; }
                    if (fallback.empty()) fallback = out;
                }
                RegCloseKey(h);
            }
            RegCloseKey(cls);
            return fallback;
        }

        
    }

    void Gather()
    {
        if (g_gathered) return;
        g_gathered = true;

        // İşlemci çekirdekleri ve iş parçacıkları: çekirdek sayısı ancak mantıksal işlemci
        // bilgisinden türetilebilir, iş parçacığı sayısı ise doğrudan gelir.
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_info.cpuThreads = (int)si.dwNumberOfProcessors;

        DWORD len = 0;
        GetLogicalProcessorInformation(nullptr, &len);
        if (len > 0)
        {
            auto* buf = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(len);
            if (buf && GetLogicalProcessorInformation(buf, &len))
            {
                int cores = 0;
                DWORD count = (DWORD)(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
                for (DWORD i = 0; i < count; ++i)
                    if (buf[i].Relationship == RelationProcessorCore)
                        cores++;
                g_info.cpuCores = cores;
            }
            free(buf);
        }
        if (g_info.cpuCores == 0) g_info.cpuCores = g_info.cpuThreads;

        // İşlemci saati yalnızca kayıt defterindeki ~MHz değeriyle bilinebilir; bu yüzden
        // MHz okunup GHz olarak yeniden biçimlendiriliyor, değer yoksa N/A deniyor.
        DWORD mhz = RegDword(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
        if (mhz > 0)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f GHz", mhz / 1000.0);
            g_info.cpuClock = buf;
        }
        else
            g_info.cpuClock = "N/A";

        // GPU video belleği: DXGI'ın ilk adaptöründen DedicatedVideoMemory alınır, 1024 MB'ı
        // aşınca GB, altında kalınca MB olarak gösterilir.
        {
            IDXGIFactory* factory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory)))
            {
                IDXGIAdapter* adapter = nullptr;
                if (SUCCEEDED(factory->EnumAdapters(0, &adapter)))
                {
                    DXGI_ADAPTER_DESC desc;
                    adapter->GetDesc(&desc);
                    SIZE_T mb = desc.DedicatedVideoMemory / (1024 * 1024);
                    char buf[32];
                    if (mb >= 1024)
                        snprintf(buf, sizeof(buf), "%.0f GB", mb / 1024.0);
                    else
                        snprintf(buf, sizeof(buf), "%zu MB", mb);
                    g_info.gpuVram = buf;
                    adapter->Release();
                }
                factory->Release();
            }
            if (g_info.gpuVram.empty()) g_info.gpuVram = "N/A";
        }

        // GPU sürücü sürümü: `HKLM\SOFTWARE\Microsoft\DirectX\Version` DirectX çalışma
        // zamanı sürümüdür, sürücü sürümü değildir. Gerçek DriverVersion dizesi ekran
        // sınıfı anahtarında durur ve DXGI'ın ilk saydığı adaptörün alt anahtarı kullanılır.
        g_info.gpuDriver = DriverVersionForPrimaryAdapter();
        if (g_info.gpuDriver.empty())
            g_info.gpuDriver = "N/A";

        // RAM toplamı: ullTotalPhys tek seferde alınabilen tek güvenilir değerdir.
        {
            MEMORYSTATUSEX ms = { sizeof(ms) };
            GlobalMemoryStatusEx(&ms);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f GB", ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0));
            g_info.ramTotal = buf;
        }

        // RAM hızı ve yuva sayısı yalnızca SMBIOS tip 17 tablolarında bulunur; kayıt defteri
        // bunları yansıtmaz. Daha önce BIOSReleaseDate ramSpeed alanına okunuyor ve iki
        // satır sonra "N/A" ile eziliyordu; hiç okunmamış bir değeri göstermemek için
        // iki alan da boş bırakılıyor ve panel o satırları hiç çizmiyor.

        // Anakart: BaseBoardProduct her makinede dolu değildir, bu yüzden eksikse ürün adına
        // düşülür; ikisi de yoksa N/A gösterilir.
        g_info.motherboard = RegStr(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardProduct");
        if (g_info.motherboard.empty())
            g_info.motherboard = RegStr(HKEY_LOCAL_MACHINE,
                L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName");
        if (g_info.motherboard.empty())
            g_info.motherboard = "N/A";

        // BIOS sürümü: BIOSVersion ve SystemBiosVersion farklı üreticilerde farklı
        // doldurulduğu için ikisi sırayla deniyor.
        g_info.biosVersion = RegStr(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSVersion");
        if (g_info.biosVersion.empty())
            g_info.biosVersion = RegStr(HKEY_LOCAL_MACHINE,
                L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemBiosVersion");
        if (g_info.biosVersion.empty())
            g_info.biosVersion = "N/A";

        // BIOS kipi metin olarak saklanmıyor, yalnızca bir indeks olarak tutuluyor: ifade
        // panel çizilirken çözülür, böylece dil değişimi ekrana yeniden yansır.
        {
            SetLastError(0);
            GetFirmwareEnvironmentVariableA("", "{00000000-0000-0000-0000-000000000000}", nullptr, 0);
            const DWORD err = GetLastError();
            g_info.biosLegacy = (err == ERROR_INVALID_FUNCTION) ? 1 : 0;
        }

        // Secure Boot, yalnızca UEFI sağlayıcısının yanıtladığı durumda okunabilir; çağrı
        // başarısız olursa hatanın kendisi ayrımı taşır (desteklenmiyor / bilinmiyor).
        {
            BYTE val = 0;
            DWORD result = GetFirmwareEnvironmentVariableA(
                "SecureBoot", "{8be4df61-93ca-11d2-aa0d-00e098032b8c}",
                &val, sizeof(val));
            if (result > 0)
                g_info.secureBoot = val ? SecureBootState::Enabled : SecureBootState::Disabled;
            else
            {
                const DWORD err = GetLastError();
                g_info.secureBoot = (err == ERROR_INVALID_FUNCTION) ? SecureBootState::Unsupported
                                                                   : SecureBootState::Unknown;
            }
        }

        // Sanallaştırma: gerçek yanıt firmware bayrağıdır. `...\Virtualization` anahtarının
        // varlığı yalnızca Windows'un bu anahtarı taşıdığı anlamına gelir; varlığa
        // bakmak Win10 ve sonrası her makinede "Etkin" sonucu üretirdi. Bunun yerine
        // hangi katmanın açık olduğu (VBS ya da Hyper-V) ayrı ayrı bildiriliyor.
        {
            const DWORD vbs = RegDword(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard", L"EnableVirtualizationBasedSecurity");

            // Hyper-V ana makinesi her zaman vmcompute servisini kaydeder; Start değerinin ne
            // olduğu önemsiz, yalnızca anahtarın var olması hipervizörün hazır olduğunu söyler.
            HKEY vm;
            const bool hyperV = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Services\\vmcompute", 0, KEY_READ, &vm) == ERROR_SUCCESS;
            if (hyperV) RegCloseKey(vm);

            if (hyperV)
                g_info.virtState = VirtState::HyperV;
            else if (vbs == 1)
                g_info.virtState = VirtState::Vbs;
            else if (IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED))
                g_info.virtState = VirtState::Firmware;
            else
                g_info.virtState = VirtState::Disabled;
        }

        // Kurulum tarihi: Unix zaman damgası yerel saate çevrilir; localtime_s hata döndürürse
        // değer uydurmak yerine N/A yazılır.
        {
            DWORD ts = RegDword(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"InstallDate");
            if (ts > 0)
            {
                time_t t = (time_t)ts;
                struct tm tm = {};
                errno_t e = ::localtime_s(&tm, &t);
                char buf[32];
                if (e == 0)
                    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
                else
                    snprintf(buf, sizeof(buf), "N/A");
                g_info.installDate = buf;
            }
            else
                g_info.installDate = "N/A";
        }

        // DirectX sürümü: InstalledVersion, işletim sisteminin desteklediği çalışma zamanı
        // dizesidir; aynı anahtardaki `Version` değeri ise d3d9 döneminin eski sayısıdır
        // ve önceden elle yazılmış "DirectX 12" ikisini de yok sayıyordu. Adaptör yalnızca
        // yazılımsal rasterleştiriciyi etiketlemek için sorgulanır: DXGI_ADAPTER_DESC'te
        // Flags üyesi yoktur, yazılım biti DXGI_ADAPTER_DESC1 üzerindedir; bu yüzden
        // IDXGIFactory1 kullanılıyor.
        {
            IDXGIFactory1* factory = nullptr;
            const char* renderer = "DirectX 11+";
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
            {
                IDXGIAdapter1* adapter = nullptr;
                if (SUCCEEDED(factory->EnumAdapters1(0, &adapter)) && adapter)
                {
                    DXGI_ADAPTER_DESC1 desc1;
                    if (SUCCEEDED(adapter->GetDesc1(&desc1)) &&
                        (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                        renderer = "WARP (software)";
                    adapter->Release();
                }
                factory->Release();
            }

            const std::string installed = RegStr(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\DirectX", L"InstalledVersion");
            g_info.directX = installed.empty() ? std::string(renderer) : ("DirectX " + installed);
        }

        // Ekran çözünürlüğü: SM_CXSCREEN/SM_CYSCREEN birincil ekranı verir, çoklu
        // monitörlü kurulumda toplam genişlik değil yalnızca bu ekran okunur.
        {
            int w = GetSystemMetrics(SM_CXSCREEN);
            int h = GetSystemMetrics(SM_CYSCREEN);
            char buf[32];
            snprintf(buf, sizeof(buf), "%d x %d", w, h);
            g_info.displayRes = buf;
        }

        // Sistem dili: kullanıcının varsayılan yerel adı alınır ve arayüzün gösterdiği
        // metin diliyle karıştırılmaması için sistem bilgisi olarak raporlanır.
        {
            wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
            GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH);
            char out[64];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
            g_info.systemLocale = out;
        }
    }

    const Info& Get()
    {
        if (!g_gathered) Gather();
        return g_info;
    }
}
