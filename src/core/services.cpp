#include "services.hpp"

#include <windows.h>
#include <winsvc.h>

#pragma comment(lib, "Advapi32.lib")

namespace services
{
    namespace
    {
        // Oyun profili beyaz listesi. Her satır:
        //   { ad, gerekçe, risk sınıfı, oyun oturumunda hedef durum }
        //
        // Hedef "Disabled" değil "Manual": bazı hizmetler ilk gereksinim
        // anında yeniden başlatılır; Manual onlar için güvenli olandır.
        // Kritik hizmetler (RpcSs, Dhcp, LanmanServer, Winlogon, ...) burada
        // KASITLI olarak yoktur ve çalışma zamanında bir ada eşleştirilemez.
        constexpr Policy kPolicy[] = {
            { L"SysMain",          L"Önbellekleme (SuperFetch) — oyunlarda disk I/O yaratır",
              Severity::Safe,     StartType::Manual },
            { L"DiagTrack",        L"Birleşik telemetri",
              Severity::Safe,     StartType::Disabled },
            { L"dmwappushservice", L"WAP Push İleti Yönlendirici (telemetri yardımcı)",
              Severity::Safe,     StartType::Disabled },
            { L"WSearch",          L"Windows arama (dosya indeksleyici)",
              Severity::Caution,  StartType::Manual },
            { L"XblGameSave",      L"Xbox Live oyun kaydı",
              Severity::Caution,  StartType::Manual },
            { L"XblAuthManager",   L"Xbox Live kimlik doğrulama",
              Severity::Caution,  StartType::Manual },
            { L"XboxGipSvc",       L"Xbox aksesuar yönetimi",
              Severity::Caution,  StartType::Manual },
            { L"XboxNetApiSvc",    L"Xbox Live ağ servisi",
              Severity::Caution,  StartType::Manual },
            { L"WbioSrvc",         L"Windows Biyometrik Servisi",
              Severity::Caution,  StartType::Manual },
            { L"MapsBroker",       L"İndirilen haritalar yöneticisi",
              Severity::Safe,     StartType::Manual },
            { L"RetailDemo",       L"Mağaza demo modu",
              Severity::Safe,     StartType::Disabled },
            { L"RemoteRegistry",   L"Uzaktan registry — ağ saldırı yüzeyi",
              Severity::Safe,     StartType::Disabled },
            { L"Fax",              L"Faks hizmeti",
              Severity::Safe,     StartType::Disabled },
            { L"PrintNotify",      L"Yazdırma bildirimleri (yazıcı yoksa gereksiz)",
              Severity::Risky,    StartType::Manual },
            { L"WerSvc",           L"Windows Hata Raporlama",
              Severity::Caution,  StartType::Manual },
            { L"DoSvc",            L"Teslimat Optimizasyonu — oyun oturumunda bant genişliği çalar",
              Severity::Caution,  StartType::Manual },
        };

        constexpr int kPolicyCount = static_cast<int>(sizeof(kPolicy) / sizeof(kPolicy[0]));

        bool IsCurrentProcessElevated()
        {
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
            TOKEN_ELEVATION elev = {};
            DWORD size = sizeof(elev);
            BOOL ok = ::GetTokenInformation(token, TokenElevation, &elev, size, &size);
            ::CloseHandle(token);
            return ok && elev.TokenIsElevated;
        }

        StartType DecodeStartType(DWORD w, bool delayed)
        {
            switch (w)
            {
                case SERVICE_BOOT_START:   return StartType::Boot;
                case SERVICE_SYSTEM_START: return StartType::System;
                case SERVICE_AUTO_START:   return delayed ? StartType::AutomaticDelayed : StartType::Automatic;
                case SERVICE_DEMAND_START: return StartType::Manual;
                case SERVICE_DISABLED:     return StartType::Disabled;
            }
            return StartType::Unknown;
        }

        DWORD EncodeStartType(StartType t)
        {
            switch (t)
            {
                case StartType::Boot:             return SERVICE_BOOT_START;
                case StartType::System:           return SERVICE_SYSTEM_START;
                case StartType::Automatic:        return SERVICE_AUTO_START;
                case StartType::AutomaticDelayed: return SERVICE_AUTO_START;
                case StartType::Manual:           return SERVICE_DEMAND_START;
                case StartType::Disabled:         return SERVICE_DISABLED;
                default:                          return SERVICE_NO_CHANGE;
            }
        }

        const Policy* FindPolicy(const std::wstring& name)
        {
            for (int i = 0; i < kPolicyCount; ++i)
                if (name == kPolicy[i].serviceName) return &kPolicy[i];
            return nullptr;
        }

        bool ReadDelayedAutostart(SC_HANDLE service)
        {
            DWORD needed = 0;
            ::QueryServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, nullptr, 0, &needed);
            if (needed == 0) return false;
            std::vector<unsigned char> buf(needed);
            if (!::QueryServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                                        buf.data(), needed, &needed))
                return false;
            auto info = reinterpret_cast<SERVICE_DELAYED_AUTO_START_INFO*>(buf.data());
            return info->fDelayedAutostart != FALSE;
        }

        bool QueryOne(SC_HANDLE scm, const Policy& p, Entry& out)
        {
            out.serviceName = p.serviceName;
            out.displayName = p.serviceName;
            out.current = StartType::Unknown;
            out.running = false;
            out.policy = &p;

            SC_HANDLE svc = ::OpenServiceW(scm, p.serviceName,
                                           SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
            if (!svc) return false;

            DWORD needed = 0;
            ::QueryServiceConfigW(svc, nullptr, 0, &needed);
            if (needed == 0)
            {
                ::CloseServiceHandle(svc);
                return false;
            }
            std::vector<unsigned char> buf(needed);
            if (!::QueryServiceConfigW(svc, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data()),
                                       needed, &needed))
            {
                ::CloseServiceHandle(svc);
                return false;
            }

            auto cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
            if (cfg->lpDisplayName && *cfg->lpDisplayName) out.displayName = cfg->lpDisplayName;
            const bool delayed = ReadDelayedAutostart(svc);
            out.current = DecodeStartType(cfg->dwStartType, delayed);

            SERVICE_STATUS st = {};
            if (::QueryServiceStatus(svc, &st))
                out.running = (st.dwCurrentState == SERVICE_RUNNING ||
                               st.dwCurrentState == SERVICE_START_PENDING);

            ::CloseServiceHandle(svc);
            return true;
        }

        // Yedek anahtar: HKLM\SOFTWARE\TENGRI\ServicesBackup\<svc>
        //   Start = DWORD (orijinal başlangıç türü, ham SCM değeri)
        //   Delayed = DWORD (0/1)
        //
        // HKLM altında olmasının nedeni: hizmetlerin kendisi de HKLM
        // altında yaşıyor ve ApplyGameProfile zaten yükseltilmiş süreçte
        // çağrılıyor. HKCU'ya koysaydık profil, kullanıcılar arası tutarsız
        // olurdu.
        constexpr const wchar_t* kBackupSubkey = L"SOFTWARE\\TENGRI\\ServicesBackup";

        bool BackupStart(const std::wstring& service, DWORD startType, bool delayed)
        {
            std::wstring path = kBackupSubkey;
            path += L"\\";
            path += service;

            HKEY key = nullptr;
            if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0,
                                  KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
                return false;

            // Yedek zaten varsa dokunma — ikinci kez profil uygulamak orijinal
            // değeri değil "oyun profili" değerini yakalayıp kalıcı hâle
            // getirirdi.
            DWORD existing = 0;
            DWORD size = sizeof(existing);
            if (::RegQueryValueExW(key, L"Start", nullptr, nullptr,
                                   reinterpret_cast<BYTE*>(&existing), &size) == ERROR_SUCCESS)
            {
                ::RegCloseKey(key);
                return true;
            }

            LONG r1 = ::RegSetValueExW(key, L"Start", 0, REG_DWORD,
                                       reinterpret_cast<const BYTE*>(&startType), sizeof(startType));
            DWORD delayedDw = delayed ? 1u : 0u;
            LONG r2 = ::RegSetValueExW(key, L"Delayed", 0, REG_DWORD,
                                       reinterpret_cast<const BYTE*>(&delayedDw), sizeof(delayedDw));
            ::RegCloseKey(key);
            return r1 == ERROR_SUCCESS && r2 == ERROR_SUCCESS;
        }

        bool ReadBackup(const std::wstring& service, DWORD& startType, bool& delayed)
        {
            std::wstring path = kBackupSubkey;
            path += L"\\";
            path += service;

            HKEY key = nullptr;
            if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
                return false;

            DWORD value = 0;
            DWORD size = sizeof(value);
            LONG r = ::RegQueryValueExW(key, L"Start", nullptr, nullptr,
                                        reinterpret_cast<BYTE*>(&value), &size);
            if (r != ERROR_SUCCESS)
            {
                ::RegCloseKey(key);
                return false;
            }
            startType = value;

            size = sizeof(value);
            value = 0;
            ::RegQueryValueExW(key, L"Delayed", nullptr, nullptr,
                               reinterpret_cast<BYTE*>(&value), &size);
            delayed = value != 0;

            ::RegCloseKey(key);
            return true;
        }

        void DeleteBackup(const std::wstring& service)
        {
            std::wstring path = kBackupSubkey;
            path += L"\\";
            path += service;
            ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, path.c_str());
        }

        bool ChangeServiceStart(SC_HANDLE scm, const std::wstring& name, StartType t)
        {
            SC_HANDLE svc = ::OpenServiceW(scm, name.c_str(),
                                           SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG);
            if (!svc) return false;

            const DWORD encoded = EncodeStartType(t);
            BOOL ok = ::ChangeServiceConfigW(svc,
                                             SERVICE_NO_CHANGE,
                                             encoded,
                                             SERVICE_NO_CHANGE,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                             nullptr);

            if (ok && t == StartType::AutomaticDelayed)
            {
                SERVICE_DELAYED_AUTO_START_INFO info = { TRUE };
                ::ChangeServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &info);
            }
            else if (ok && t == StartType::Automatic)
            {
                SERVICE_DELAYED_AUTO_START_INFO info = { FALSE };
                ::ChangeServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &info);
            }

            ::CloseServiceHandle(svc);
            return ok != FALSE;
        }
    }

    void GetPolicy(const Policy** out, int* count)
    {
        if (out) *out = kPolicy;
        if (count) *count = kPolicyCount;
    }

    const wchar_t* StartTypeLabel(StartType t)
    {
        switch (t)
        {
            case StartType::Boot:             return L"Boot";
            case StartType::System:           return L"System";
            case StartType::Automatic:        return L"Automatic";
            case StartType::AutomaticDelayed: return L"Automatic (Delayed)";
            case StartType::Manual:           return L"Manual";
            case StartType::Disabled:         return L"Disabled";
            default:                          return L"Unknown";
        }
    }

    std::vector<Entry> Query()
    {
        std::vector<Entry> out;
        out.reserve(kPolicyCount);

        SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm)
        {
            for (int i = 0; i < kPolicyCount; ++i)
            {
                Entry e = {};
                e.serviceName = kPolicy[i].serviceName;
                e.displayName = kPolicy[i].serviceName;
                e.policy = &kPolicy[i];
                out.push_back(std::move(e));
            }
            return out;
        }

        for (int i = 0; i < kPolicyCount; ++i)
        {
            Entry e = {};
            if (!QueryOne(scm, kPolicy[i], e))
            {
                e.serviceName = kPolicy[i].serviceName;
                e.displayName = kPolicy[i].serviceName;
                e.policy = &kPolicy[i];
            }
            out.push_back(std::move(e));
        }

        ::CloseServiceHandle(scm);
        return out;
    }

    bool SetStartType(const std::wstring& serviceName, StartType t)
    {
        // Beyaz liste dışı adlara izin verme. Bu kontrol kritik hizmetleri
        // yanlışlıkla devre dışı bırakmayı imkânsız kılar.
        if (!FindPolicy(serviceName)) return false;
        if (!IsCurrentProcessElevated()) return false;

        SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) return false;
        bool ok = ChangeServiceStart(scm, serviceName, t);
        ::CloseServiceHandle(scm);
        return ok;
    }

    int ApplyGameProfile()
    {
        if (!IsCurrentProcessElevated()) return 0;

        SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) return 0;

        int changed = 0;
        for (int i = 0; i < kPolicyCount; ++i)
        {
            const Policy& p = kPolicy[i];

            // Önce mevcut durumu oku; yedeği ancak bir değişiklik yapılacaksa yaz.
            SC_HANDLE svc = ::OpenServiceW(scm, p.serviceName,
                                           SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
            if (!svc) continue;

            DWORD needed = 0;
            ::QueryServiceConfigW(svc, nullptr, 0, &needed);
            if (needed == 0)
            {
                ::CloseServiceHandle(svc);
                continue;
            }
            std::vector<unsigned char> buf(needed);
            if (!::QueryServiceConfigW(svc, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data()),
                                       needed, &needed))
            {
                ::CloseServiceHandle(svc);
                continue;
            }
            auto cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
            const DWORD currentRaw = cfg->dwStartType;
            const bool  currentDelayed = ReadDelayedAutostart(svc);
            ::CloseServiceHandle(svc);

            const DWORD targetRaw = EncodeStartType(p.gameProfile);
            if (currentRaw == targetRaw) continue;  // zaten istenen durumda

            if (!BackupStart(p.serviceName, currentRaw, currentDelayed)) continue;
            if (ChangeServiceStart(scm, p.serviceName, p.gameProfile)) ++changed;
        }

        ::CloseServiceHandle(scm);
        return changed;
    }

    int RestoreGameProfile()
    {
        if (!IsCurrentProcessElevated()) return 0;

        SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm) return 0;

        int restored = 0;
        for (int i = 0; i < kPolicyCount; ++i)
        {
            const Policy& p = kPolicy[i];

            DWORD rawStart = 0;
            bool delayed = false;
            if (!ReadBackup(p.serviceName, rawStart, delayed)) continue;

            StartType t = DecodeStartType(rawStart, delayed);
            if (ChangeServiceStart(scm, p.serviceName, t))
            {
                DeleteBackup(p.serviceName);
                ++restored;
            }
        }

        ::CloseServiceHandle(scm);
        return restored;
    }
}
