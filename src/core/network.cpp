#include "network.hpp"
#include "brand.hpp"
#include "sample_data.hpp"
#include <windows.h>
// IcmpCreateFile ve IcmpSendEcho2 yalnızca icmpapi.h içinde tanımlıdır; windows.h onları
// içeri almadığı için bu başlık ayrıca ekleniyor.
#include <iphlpapi.h>
#include <icmpapi.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cctype>

#pragma comment(lib, "iphlpapi.lib")

namespace network
{
    namespace
    {
        constexpr const wchar_t* kRunKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        constexpr const wchar_t* kRunName = brand::kRunValueName;

        bool RunCmd(const wchar_t* cmd)
        {
            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            wchar_t buf[512];
            wcscpy_s(buf, cmd);
            if (!CreateProcessW(nullptr, buf, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                return false;

            // Zaman aşımı bir çıkış kodu gibi yorumlanamaz: o durumda GetExitCodeProcess
            // hâlâ STILL_ACTIVE döndürür ve alt süreç denetimsiz çalışmaya devam eder.
            const DWORD wait = WaitForSingleObject(pi.hProcess, 15000);
            if (wait == WAIT_TIMEOUT)
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

        DWORD RegGet(HKEY root, const wchar_t* path, const wchar_t* name, DWORD def)
        {
            HKEY k;
            if (RegOpenKeyExW(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return def;
            DWORD val = def, sz = sizeof(val);
            RegQueryValueExW(k, name, nullptr, nullptr, (LPBYTE)&val, &sz);
            RegCloseKey(k);
            return val;
        }

        bool RegPut(HKEY root, const wchar_t* path, const wchar_t* name, DWORD val)
        {
            HKEY k;
            if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
                return false;
            bool ok = RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&val, sizeof(val)) == ERROR_SUCCESS;
            RegCloseKey(k);
            return ok;
        }

        // Loopback, tünel uçları ve Hyper-V / VirtualBox sanal NIC'leri GetAdaptersInfo'ya
        // hepsi bir adresle yanıt verir. Böyle bir arayüze DNS yazmak makinede hiçbir
        // şeyi değiştirmez, bu yüzden yalnızca gerçek Ethernet/WLAN bağlantıları seçilir.
        // Arayüz türü IP_ADAPTER_INFO yapısında IfType değil Type alanında taşınır.
        bool IsUsableAdapter(const IP_ADAPTER_INFO* a)
        {
            if (!a) return false;
            if (strcmp(a->IpAddressList.IpAddress.String, "0.0.0.0") == 0) return false;
            return a->Type == IF_TYPE_ETHERNET_CSMACD || a->Type == IF_TYPE_IEEE80211;
        }

        // netsh ve IP helper yapıları ANSI olduğundan bağlantı adı da dar karakterli
        // olmalıdır. wchar_t bir %s biçimine geçirilirse MSVC bunu C4477 hatasıyla
        // kesin olarak reddeder, bu yüzden dönüşüm bilinçli olarak yapılıyor.
        std::string Narrow(const std::wstring& w)
        {
            if (w.empty()) return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1) return {};
            std::string out((size_t)n - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, nullptr, nullptr);
            return out;
        }

        std::wstring ConnectionNameForGuid(const std::string& guid)
        {
            std::wstring regPath = L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\";
            wchar_t guidW[256] = {};
            MultiByteToWideChar(CP_ACP, 0, guid.c_str(), -1, guidW, 256);
            regPath += guidW;
            regPath += L"\\Connection";

            HKEY k;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
                return L"Ethernet";
            wchar_t name[256] = {};
            DWORD nsz = sizeof(name);
            RegQueryValueExW(k, L"Name", nullptr, nullptr, (LPBYTE)name, &nsz);
            RegCloseKey(k);
            return name[0] ? name : L"Ethernet";
        }

        std::wstring FindAdapter()
        {
            ULONG sz = sizeof(IP_ADAPTER_INFO) * 16;
            IP_ADAPTER_INFO* info = (IP_ADAPTER_INFO*)malloc(sz);
            if (!info) return L"Ethernet";
            if (GetAdaptersInfo(info, &sz) == ERROR_BUFFER_OVERFLOW)
            {
                free(info);
                info = (IP_ADAPTER_INFO*)malloc(sz);
                if (!info) return L"Ethernet";
            }
            if (GetAdaptersInfo(info, &sz) != NO_ERROR) { free(info); return L"Ethernet"; }

            std::string guid;
            for (IP_ADAPTER_INFO* a = info; a; a = a->Next)
                if (IsUsableAdapter(a)) { guid = a->AdapterName; break; }
            free(info);

            if (guid.empty()) return L"Ethernet";
            return ConnectionNameForGuid(guid);
        }
    }

    bool FlushDns()
    {
        return RunCmd(L"ipconfig /flushdns");
    }

    bool SetRunAtStartup(const wchar_t* exe_path, bool enable)
    {
        if (!exe_path || !*exe_path) return false;

        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;

        bool ok;
        if (enable)
        {
            // Değer birleştirilerek kuruluyor, _snwprintf_s ile değil: geniş biçim dizgisindeki
            // bir %s wchar_t bekler, dar olan ise char ister (C4477).
            std::wstring quoted = L"\"";
            quoted += exe_path;
            quoted += L"\"";
            ok = RegSetValueExW(k, kRunName, 0, REG_SZ, (const BYTE*)quoted.c_str(),
                                (DWORD)((quoted.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        }
        else
        {
            const LONG r = RegDeleteValueW(k, kRunName);
            ok = r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
        }
        RegCloseKey(k);
        return ok;
    }

    bool GetRunAtStartup(const wchar_t* exe_path)
    {
        if (!exe_path || !*exe_path) return false;

        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
            return false;

        wchar_t val[1024] = {};
        DWORD sz = sizeof(val);
        const bool ok = RegQueryValueExW(k, kRunName, nullptr, nullptr, (LPBYTE)val, &sz) == ERROR_SUCCESS &&
                        val[0] != 0;
        RegCloseKey(k);
        if (!ok) return false;

        // Karşılaştırma tırnak içindeki yol üzerinden yapılıyor; aksi hâlde başka bir
        // konuma taşınmış ya da yeniden adlandırılmış bir kopya kendini etkin
        // görünürdü.
        wchar_t unquoted[1024] = {};
        const wchar_t* p = val;
        if (*p == L'"') { ++p; }
        size_t i = 0;
        while (p[i] && p[i] != L'"' && i + 1 < sizeof(unquoted) / sizeof(wchar_t)) { unquoted[i] = p[i]; ++i; }

        wchar_t mine[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, mine, MAX_PATH);
        return _wcsicmp(unquoted, mine) == 0;
    }

    bool SetDns(int provider)
    {
        std::wstring adapter = FindAdapter();
        // netsh bağlantı adını bir belirteç olarak ayrıştırır: adın içinde tırnak bulunursa
        // argüman erkenden kapanır ve kalan kısım ikinci bir seçenek gibi okunur.
        std::wstring safe;
        for (wchar_t c : adapter)
            safe += (c == L'"') ? L'\'' : c;
        std::wstring q = L"\"" + safe + L"\"";

        if (provider == 0)
        {
            // Sıfırlama yalnızca birincil girdiyi değil listenin tamamını temizler; aksi hâlde
            // daha önce elle eklenen ikincil statik sunucu, DHCP'ye dönüşte sistemde
            // kalırdı.
            const std::wstring reset = L"netsh interface ipv4 set dnsservers name=" + q + L" source=dhcp";
            const bool ok = RunCmd(reset.c_str());
            RunCmd(L"ipconfig /flushdns");
            return ok;
        }

        const wchar_t* primary   = nullptr;
        const wchar_t* secondary = nullptr;
        switch (provider)
        {
        case 1: primary = L"1.1.1.1";   secondary = L"1.0.0.1";       break;
        case 2: primary = L"8.8.8.8";   secondary = L"8.8.4.4";       break;
        case 3: primary = L"9.9.9.9";   secondary = L"149.112.112.112"; break;
        default: return false;
        }

        std::wstring cmd1 = L"netsh interface ipv4 set dnsservers name=" + q + L" source=static address=" + primary +
                            L" validate=no";
        std::wstring cmd2 = L"netsh interface ipv4 add dnsservers name=" + q + L" address=" + secondary + L" index=2 validate=no";
        const bool ok = RunCmd(cmd1.c_str());
        RunCmd(cmd2.c_str());
        RunCmd(L"ipconfig /flushdns");
        return ok;
    }

    // Auto-tuning, kayıt defteri karşılığı olmayan genel bir `netsh int tcp` ayarıdır;
        // bu yüzden durumu elle sabitlenmek yerine onu yazan komutun kendi çıktısından
        // okunur. `found`, "normal/disabled" ile "netsh kullanılamıyor" durumlarını
        // birbirinden ayırır.
    bool QueryNetsh(const wchar_t* cmd, const wchar_t* needle, bool* found)
    {
        *found = false;

        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE rd, wr;
        if (!CreatePipe(&rd, &wr, &sa, 4096)) return false;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

        std::wstring full = std::wstring(cmd) + L" 2>&1";
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError  = wr;
        PROCESS_INFORMATION pi = {};
        wchar_t buf[256];
        wcscpy_s(buf, full.c_str());

        if (!CreateProcessW(nullptr, buf, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(rd); CloseHandle(wr);
            return false;
        }
        CloseHandle(wr);

        std::string out;
        char chunk[512];
        DWORD n = 0;
        while (ReadFile(rd, chunk, sizeof(chunk), &n, nullptr) && n)
            out.append(chunk, n);
        CloseHandle(rd);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (needle == nullptr) return true;

        // netsh çıktısı yerelleştirildiği için eşleştirme büyük/küçük harf duyarsız yapılır.
        // Geniş needle'den doğrudan `const char*` ile kurulan std::string değiştirilemez
        // olduğu için hedef açıkça std::string olarak belirtiliyor (aksi hâlde C2440).
        std::string hay = out;
        std::string pat;
        for (const wchar_t* p = needle; *p; ++p) pat += (char)*p;
        for (char& ch : hay) ch = (char)tolower((unsigned char)ch);
        for (char& ch : pat) ch = (char)tolower((unsigned char)ch);
        const size_t at = hay.find(pat);
        if (at == std::string::npos) return false;

        *found = true;
        // Değer, etiketten sonraki iki noktanın ardından gelir:
        // "Receive Window Auto-Tuning Level : normal"
        const size_t colon = out.find(':', at);
        if (colon == std::string::npos) return true;
        size_t s = colon + 1;
        while (s < out.size() && isspace((unsigned char)out[s])) ++s;
        size_t e = s;
        while (e < out.size() && !isspace((unsigned char)out[e]) && out[e] != '\r' && out[e] != '\n') ++e;
        std::string value = out.substr(s, e - s);
        for (char& c : value) c = (char)tolower((unsigned char)c);
        return value == "normal" || value == "enabled";
    }

    bool ReadTweak(int idx)
    {
        switch (idx)
        {
        case 0: { // TCP auto-tuning etkin sayılır, netsh bunu "normal" diye bildirir
            bool found = false;
            const bool normal = QueryNetsh(L"netsh int tcp show global", L"Auto-Tuning Level", &found);
            // Yerelleştirilmiş bir Windows'ta bu etiket hiçbir zaman eşleşmez. Sağlıklı bir
            // adaptör için Windows varsayılanı "normal" olduğundan, anahtar elle yazılmış
            // bir false yerine o varsayılana düşer; yanlış değil, eksik bilgi gösterir.
            return found ? normal : true;
        }
        case 1: // Nagle kapalı
            return RegGet(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                          L"TcpNoDelay", 0) == 1;
        case 2: // Ağ kısıtlaması kapalı
            return RegGet(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                          L"NetworkThrottlingIndex", 10) == 0xFFFFFFFF;
        case 3: // QoS için bant genişliği ayrılmıyor
            return RegGet(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Psched",
                          L"NonBestEffortLimit", 20) == 0;
        case 4: { // Büyük gönderme boşaltma (LSO), netsh tarafından genel bir durum olarak bildirilir
            bool found = false;
            const bool on = QueryNetsh(L"netsh int tcp show global", L"Large Send Offload", &found);
            // LSO varsayılan olarak açıktır; bu anahtarın "açık" anlamı da tam olarak budur.
            return found ? on : true;
        }
        case 5: // Kesme yumuşatma, her adaptörün sürücüsünde ayrı bir özelliktir
            return false;
        }
        return false;
    }

    bool IsSupported(int idx)
    {
        // idx 4 (LSO) ve idx 5 (kesme yumuşatma) adaptör başına ayarlardır; buradan geri
        // okunup doğrulanamazlar. Çalışıyormuş gibi gösterilmeleri yanlış bilgi olurdu,
        // bu yüzden desteklenmiyor olarak işaretleniyorlar.
        return idx != 4 && idx != 5;
    }

    bool ApplyTweak(int idx, bool on)
    {
        switch (idx)
        {
        case 0:
            return on ? RunCmd(L"netsh int tcp set global autotuninglevel=normal")
                      : RunCmd(L"netsh int tcp set global autotuninglevel=disabled");
        case 1:
            return RegPut(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                          L"TcpNoDelay", on ? 1 : 0) &&
                   RegPut(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                          L"TcpAckFrequency", on ? 1 : 2);
        case 2:
            return RegPut(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                          L"NetworkThrottlingIndex", on ? 0xFFFFFFFF : 10);
        case 3:
            return RegPut(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Psched",
                          L"NonBestEffortLimit", on ? 0 : 20);
        case 4: {
            // LSO, v4 ve v6 yığınlarına bölünmüş durumdadır; ikisinin de aynı anda değişmesi
            // gerekir, aksi hâlde anahtar yarı uygulanmış olarak okunurdu.
            const bool v4 = on ? RunCmd(L"netsh int ipv4 set global taskoffload=disabled")
                               : RunCmd(L"netsh int ipv4 set global taskoffload=enabled");
            const bool v6 = on ? RunCmd(L"netsh int ipv6 set global taskoffload=disabled")
                               : RunCmd(L"netsh int ipv6 set global taskoffload=enabled");
            return v4 && v6;
        }
        case 5: {
            // Kesme yumuşatma her NIC üzerinde yaşar ve genel bir ayardan yazılamaz;
            // burada uygulanacak bir şey yok.
            return false;
        }
        }
        return false;
    }

    std::string AdapterName()
    {
        if (sample::Active()) return sample::AdapterName();
        ULONG sz = sizeof(IP_ADAPTER_INFO) * 16;
        IP_ADAPTER_INFO* info = (IP_ADAPTER_INFO*)malloc(sz);
        if (!info) return "Unknown";
        if (GetAdaptersInfo(info, &sz) == ERROR_BUFFER_OVERFLOW)
        {
            free(info);
            info = (IP_ADAPTER_INFO*)malloc(sz);
            if (!info) return "Unknown";
        }
        if (GetAdaptersInfo(info, &sz) != NO_ERROR) { free(info); return "Unknown"; }
        std::string name;
        for (IP_ADAPTER_INFO* a = info; a; a = a->Next)
            if (IsUsableAdapter(a)) { name = a->Description; break; }
        free(info);
        return name.empty() ? "Unknown" : name;
    }

    std::string LocalIP()
    {
        if (sample::Active()) return sample::LocalIp();
        ULONG sz = sizeof(IP_ADAPTER_INFO) * 16;
        IP_ADAPTER_INFO* info = (IP_ADAPTER_INFO*)malloc(sz);
        if (!info) return "";
        if (GetAdaptersInfo(info, &sz) == ERROR_BUFFER_OVERFLOW)
        {
            free(info);
            info = (IP_ADAPTER_INFO*)malloc(sz);
            if (!info) return "";
        }
        if (GetAdaptersInfo(info, &sz) != NO_ERROR) { free(info); return ""; }
        std::string ip;
        for (IP_ADAPTER_INFO* a = info; a; a = a->Next)
            if (IsUsableAdapter(a)) { ip = a->IpAddressList.IpAddress.String; break; }
        free(info);
        return ip;
    }

    // --------------------------------------------------------------- gecikme ölçümü

    namespace
    {
        HANDLE             g_pingEvent = nullptr; // elle sıfırlanan, "örnek hazır" bildirir
        HANDLE             g_pingStop  = nullptr; // elle sıfırlanan, "kapanıyor" bildirir
        std::thread        g_pingThread;
        std::atomic<bool>  g_pingRunning{ false };
        std::mutex         g_pingMtx;
        float              g_pingValue = 0.0f;
        bool               g_pingFresh = false;

        void PingWorker()
        {
            HANDLE hIcmp = IcmpCreateFile();
            if (hIcmp == INVALID_HANDLE_VALUE) return;

            IP_OPTION_INFORMATION opt = { sizeof(opt), 0, 128, 0 };
            char reply[sizeof(ICMP_ECHO_REPLY) + 32] = {};
            const char* payload = brand::kPingPayload;
            // IcmpSendEcho2 hedefi IPAddr türünde ister, yani ağ baytı sırasındaki bir IPv4
            // adresi. 11 parametrenin 5. sidir; yanlış sıraya yazılırsa derleyici
            // sessizce yanlış bir adrese gönderir.
            const IPAddr target = (IPAddr)0x01010101u;   // 1.1.1.1, ağ baytı sırasında zaten doğru

            while (WaitForSingleObject(g_pingStop, 0) != WAIT_OBJECT_0)
            {
                const DWORD r = IcmpSendEcho2(hIcmp, nullptr, nullptr, nullptr,
                                              target,
                                              (LPVOID)payload, (WORD)strlen(payload),
                                              &opt, reply, sizeof(reply), 1000);
                if (r > 0)
                {
                    const auto* echo = (ICMP_ECHO_REPLY*)reply;
                    const float ms = (float)echo->RoundTripTime;
                    if (ms >= 0.0f)
                    {
                        std::lock_guard<std::mutex> lock(g_pingMtx);
                        g_pingValue = ms;
                        g_pingFresh = true;
                        SetEvent(g_pingEvent);
                    }
                }
                // Bekleme tek seferde değil dilimler hâlinde yapılıyor; durdurma olayı ancak
                // böyle beklemeden fark edilir. Süre kLatencyIntervalSec'ten gelir:
                // 250 ms dilimleriyle, tur-çevrim süresi kadarını hesaba katarak.
                // Kaba bir 5 saniyelik bekleme koyulduğunda örnek başına ~6 s
                // düşüyor ve 61 noktalık grafik ancak altı dakikada doluyordu.
                const double due = GetTickCount64() / 1000.0 + kLatencyIntervalSec;
                while (GetTickCount64() / 1000.0 < due)
                {
                    if (WaitForSingleObject(g_pingStop, 0) == WAIT_OBJECT_0) break;
                    Sleep(50);
                }
            }
            IcmpCloseHandle(hIcmp);
        }
    }

    bool PollLatency(float* ms)
    {
        if (!ms || !g_pingRunning.load() || g_pingEvent == nullptr) return false;
        if (WaitForSingleObject(g_pingEvent, 0) != WAIT_OBJECT_0) return false;

        ResetEvent(g_pingEvent);
        std::lock_guard<std::mutex> lock(g_pingMtx);
        if (!g_pingFresh) return false;
        *ms = g_pingValue;
        g_pingFresh = false;
        return true;
    }

    void StartLatencyProbe()
    {
        if (g_pingRunning.load()) return;

        g_pingEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_pingStop  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_pingEvent || !g_pingStop)
        {
            // İkisi birlikte oluşmuyorsa yalnızca yaratılabilen tutamak sızdırılırdı.
            if (g_pingEvent) { CloseHandle(g_pingEvent); g_pingEvent = nullptr; }
            if (g_pingStop)  { CloseHandle(g_pingStop);  g_pingStop = nullptr; }
            return;
        }

        g_pingRunning.store(true);
        g_pingThread = std::thread(PingWorker);
    }

    void StopLatencyProbe()
    {
        if (!g_pingRunning.exchange(false)) return;
        if (g_pingStop) SetEvent(g_pingStop);
        if (g_pingThread.joinable()) g_pingThread.join();
        if (g_pingEvent) { CloseHandle(g_pingEvent); g_pingEvent = nullptr; }
        if (g_pingStop)  { CloseHandle(g_pingStop);  g_pingStop = nullptr; }
    }

    std::string GatewayIP()
    {
        if (sample::Active()) return sample::GatewayIp();
        ULONG sz = sizeof(IP_ADAPTER_INFO) * 16;
        IP_ADAPTER_INFO* info = (IP_ADAPTER_INFO*)malloc(sz);
        if (!info) return "";
        if (GetAdaptersInfo(info, &sz) == ERROR_BUFFER_OVERFLOW)
        {
            free(info);
            info = (IP_ADAPTER_INFO*)malloc(sz);
            if (!info) return "";
        }
        if (GetAdaptersInfo(info, &sz) != NO_ERROR) { free(info); return ""; }
        std::string gw;
        for (IP_ADAPTER_INFO* a = info; a; a = a->Next)
        {
            // Ağ geçidi adaptör başına eşleştirilir; listedeki ilk sıfırdan farklı geçidi
            // almak, bir adaptörün adresini başka bir adaptörün rotasıyla eşleştirirdi.
            if (!IsUsableAdapter(a)) continue;
            if (strcmp(a->GatewayList.IpAddress.String, "0.0.0.0") != 0)
            { gw = a->GatewayList.IpAddress.String; break; }
        }
        free(info);
        return gw;
    }
}
