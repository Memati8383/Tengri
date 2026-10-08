#include "tweaks.hpp"
#include "regpack.hpp"
#include <windows.h>
#include <string>

namespace tweaks
{
    namespace
    {
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

        bool RegPutStr(HKEY root, const wchar_t* path, const wchar_t* name, const wchar_t* val)
        {
            HKEY k;
            if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
                return false;
            DWORD cb = (DWORD)((wcslen(val) + 1) * sizeof(wchar_t));
            bool ok = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)val, cb) == ERROR_SUCCESS;
            RegCloseKey(k);
            return ok;
        }

        // Anahtarın varsayılan değerine yazar (ad parametresi boş bırakılır).
        // Geri alma yolunda tek değer silinemez, çünkü değer adı olmayan kayıtları
        // silme çağrısı kabul etmez; bu yüzden eski hâle dönmek için anahtar
        // bütünüyle kaldırılır.
        bool RegPutDefault(HKEY root, const wchar_t* path)
        {
            HKEY k;
            if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
                return false;
            const bool ok = RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t)) == ERROR_SUCCESS;
            RegCloseKey(k);
            return ok;
        }

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

            // Zaman aşımı bir çıkış kodu sanılmamalı: süreç hâlâ ayakta olduğu için
            // kod okuma çağrısı "hâlâ çalışıyor" değeri döner ve alt süreç kimse
            // gözetmeden arka planda kalmaya devam ederdi.
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

        #define HKCU HKEY_CURRENT_USER
        #define HKLM HKEY_LOCAL_MACHINE

        // --------------------------------------------------------- Performans
        bool ReadPerf(int i)
        {
            switch (i)
            {
            case 0: { // En yüksek performans planı (GUID 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c)
                // Aktif plan GUID'i metin olarak saklanır; tam sayı gibi okuyup sonucu
                // atan kalan kod, aşağıdaki karşılaştırmanın neden tutmadığını
                // açıklayacak kadar sessiz bir hataydı.
                wchar_t buf[128] = {};
                HKEY k;
                if (RegOpenKeyExW(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes", 0, KEY_READ, &k) == ERROR_SUCCESS)
                {
                    DWORD sz = sizeof(buf);
                    RegQueryValueExW(k, L"ActivePowerScheme", nullptr, nullptr, (LPBYTE)buf, &sz);
                    RegCloseKey(k);
                }
                return wcsstr(buf, L"8c5e7fda") != nullptr;
            }
            case 1: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications",
                                  L"GlobalUserDisabled", 0) == 1;
            case 2: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
                                  L"VisualFXSetting", 0) == 2;
            case 3: // SysMain'in kapalı olduğu anlamına gelir. Değer 4 ise devre dışı,
                    // 2 (otomatik) ise açık demektir.
                return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Services\\SysMain", L"Start", 2) == 4;
            case 4: return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\Power", L"HibernateEnabled", 1) == 0;
            case 5: return RegGet(HKLM, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                                  L"SystemResponsiveness", 20) == 0;
            case 6: return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling",
                                  L"PowerThrottlingOff", 0) == 1;
            case 7: return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                                  L"DisablePagingExecutive", 0) == 1;
            }
            return false;
        }

        bool ApplyPerf(int i, bool on)
        {
            switch (i)
            {
            case 0: return on ? RunCmd(L"powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c")
                              : RunCmd(L"powercfg /setactive 381b4222-f694-41f0-9685-ff5bb260df2e");
            case 1: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications",
                                  L"GlobalUserDisabled", on ? 1 : 0);
            case 2: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
                                  L"VisualFXSetting", on ? 2 : 0);
            case 3: return on ? RunCmd(L"sc config SysMain start= disabled") && RunCmd(L"sc stop SysMain")
                              : RunCmd(L"sc config SysMain start= auto");
            case 4: return on ? RunCmd(L"powercfg /hibernate off") : RunCmd(L"powercfg /hibernate on");
            case 5: return RegPut(HKLM, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                                  L"SystemResponsiveness", on ? 0 : 20);
            case 6: return RegPut(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling",
                                  L"PowerThrottlingOff", on ? 1 : 0);
            case 7: return RegPut(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                                  L"DisablePagingExecutive", on ? 1 : 0);
            }
            return false;
        }

        // --------------------------------------------------------- Oyun
        bool ReadGame(int i)
        {
            switch (i)
            {
            case 0: return RegGet(HKCU, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1) == 1;
            case 1: return RegGet(HKCU, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 0) == 2;
            case 2: return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 1) == 2;
            case 3: return RegGet(HKCU, L"System\\GameConfigStore", L"GameDVR_Enabled", 1) == 0;
            case 4: return RegGet(HKCU, L"Control Panel\\Mouse", L"MouseSpeed", 1) == 0;
            case 5: return RegGet(HKLM, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                                  L"SystemResponsiveness", 20) == 0;
            case 6: return RegGet(HKLM, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                                  L"Priority", 2) == 6;
            case 7: // Kabuğun hâlâ eski yoldan geçirdiği oyunlar için oyun modu;
                return RegGet(HKCU, L"Software\\Microsoft\\GameBar", L"AllowAutoGameMode", 1) == 1;
            }
            return false;
        }

        bool ApplyGame(int i, bool on)
        {
            switch (i)
            {
            case 0: return RegPut(HKCU, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", on ? 1 : 0);
            case 1: return RegPut(HKCU, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", on ? 2 : 0);
            case 2: return RegPut(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", on ? 2 : 1);
            case 3: return RegPut(HKCU, L"System\\GameConfigStore", L"GameDVR_Enabled", on ? 0 : 1) &&
                           RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", on ? 0 : 1);
            case 4:
                if (on)
                {
                    RegPut(HKCU, L"Control Panel\\Mouse", L"MouseSpeed", 0);
                    RegPutStr(HKCU, L"Control Panel\\Mouse", L"MouseThreshold1", L"0");
                    return RegPutStr(HKCU, L"Control Panel\\Mouse", L"MouseThreshold2", L"0");
                }
                else
                {
                    RegPut(HKCU, L"Control Panel\\Mouse", L"MouseSpeed", 1);
                    RegPutStr(HKCU, L"Control Panel\\Mouse", L"MouseThreshold1", L"6");
                    return RegPutStr(HKCU, L"Control Panel\\Mouse", L"MouseThreshold2", L"10");
                }
            case 5: return RegPut(HKLM, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                                  L"SystemResponsiveness", on ? 0 : 20);
            case 6: return RegPut(HKLM, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                                  L"Priority", on ? 6 : 2);
            case 7: return RegPut(HKCU, L"Software\\Microsoft\\GameBar", L"AllowAutoGameMode", on ? 1 : 0);
            }
            return false;
        }

        // --------------------------------------------------------- Gizlilik
        bool ReadPriv(int i)
        {
            switch (i)
            {
            case 0: return RegGet(HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 3) == 0;
            case 1: return RegGet(HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableActivityFeed", 1) == 0;
            case 2: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 1) == 0;
            case 3: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location",
                                  L"Value", 0) != 0;
            case 4: return RegGet(HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search", L"AllowCortana", 1) == 0;
            case 5: return RegGet(HKCU, L"Software\\Microsoft\\Siuf\\Rules", L"NumberOfSIUFInPeriod", 1) == 0;
            case 6: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy",
                                  L"TailoredExperiencesWithDiagnosticDataEnabled", 1) == 0;
            case 7: return RegGet(HKLM, L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting", L"Disabled", 0) == 1;
            }
            return false;
        }

        bool ApplyPriv(int i, bool on)
        {
            switch (i)
            {
            case 0: return RegPut(HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", on ? 0 : 3);
            case 1: return RegPut(HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableActivityFeed", on ? 0 : 1) &&
                           RegPut(HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"PublishUserActivities", on ? 0 : 1);
            case 2: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", on ? 0 : 1);
            case 3: return on ? RegPutStr(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location",
                                          L"Value", L"Deny")
                              : RegPutStr(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location",
                                          L"Value", L"Allow");
            case 4: return RegPut(HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search", L"AllowCortana", on ? 0 : 1);
            case 5: return RegPut(HKCU, L"Software\\Microsoft\\Siuf\\Rules", L"NumberOfSIUFInPeriod", on ? 0 : 1);
            case 6: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy",
                                  L"TailoredExperiencesWithDiagnosticDataEnabled", on ? 0 : 1);
            case 7: return RegPut(HKLM, L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting", L"Disabled", on ? 1 : 0);
            }
            return false;
        }

        // --------------------------------------------------------- Görünüm
        bool ReadVis(int i)
        {
            switch (i)
            {
            case 0: return RegGet(HKCU, L"Control Panel\\Desktop\\WindowMetrics", L"MinAnimate", 1) == 0;
            case 1: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                  L"EnableTransparency", 1) == 0;
            case 2: {
                HKEY k;
                return RegOpenKeyExW(HKCU, L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32",
                                     0, KEY_READ, &k) == ERROR_SUCCESS && (RegCloseKey(k), true);
            }
            case 3: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt", 1) == 0;
            case 4: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"Hidden", 2) == 1;
            case 5: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Search", L"SearchboxTaskbarMode", 1) == 0;
            case 6: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
                                  L"RotatingLockScreenOverlayEnabled", 1) == 0;
            case 7: return RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 1) == 0;
            }
            return false;
        }

        bool ApplyVis(int i, bool on)
        {
            switch (i)
            {
            case 0: {
                RegPut(HKCU, L"Control Panel\\Desktop\\WindowMetrics", L"MinAnimate", on ? 0 : 1);
                return RegPutStr(HKCU, L"Control Panel\\Desktop", L"UserPreferencesMask",
                                 on ? L"\x90\x12\x03\x80\x10\x00\x00\x00" : L"\x9E\x1E\x07\x80\x12\x00\x00\x00");
            }
            case 1: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                  L"EnableTransparency", on ? 0 : 1);
            case 2: {
                // Kabuk eklentisini kapatmanın yolu, InprocServer32 anahtarının
                // varsayılan değerini boş bırakmaktır.
                const wchar_t* kCtx = L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32";
                if (on) return RegPutDefault(HKCU, kCtx);
                // Geri alma: oluşturduğumuz anahtarı siliyoruz. Varsayılan değerin
                // adı boş olduğu için tek tek silinemiyor; anahtarı kaldırmak tek
                // geri dönüş yolumuz.
                const LONG r = RegDeleteKeyW(HKCU, kCtx);
                return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
            }
            case 3: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt", on ? 0 : 1);
            case 4: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"Hidden", on ? 1 : 2);
            case 5: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Search", L"SearchboxTaskbarMode", on ? 0 : 1);
            case 6: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
                                  L"RotatingLockScreenOverlayEnabled", on ? 0 : 1);
            case 7: return RegPut(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", on ? 0 : 1);
            }
            return false;
        }

        // --------------------------------------------------------- Oyun ayarları
        const wchar_t* kGamesTask = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
        const wchar_t* kMemMgmt   = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";

        bool ReadGames(int i)
        {
            switch (i)
            {
            case 0: return RegGet(HKLM, kGamesTask, L"GPU Priority", 0) == 8 &&
                           RegGet(HKLM, kGamesTask, L"Priority", 0) == 6;
            case 1: return RegGet(HKCU, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 0) == 2;
            case 2: return RegGet(HKLM, kGamesTask, L"Clock Rate", 0) == 0x2710;
            case 3: return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
                                  L"RmGpsPsEnablePerCpuCoreDpc", 0) == 1;
            case 4: return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\Parameters",
                                  L"ThreadPriority", 0) == 0x1F;
            case 5: return RegGet(HKLM, kMemMgmt, L"DisablePagingExecutive", 0) == 1;
            case 6: return RegGet(HKLM, kMemMgmt, L"LargeSystemCache", 0) == 1;
            case 7: return RegGet(HKLM, kMemMgmt, L"IoPageLockLimit", 0) == 0x100000;
            }
            return false;
        }


        // --------------------------------------------------------- FiveM özel
        const wchar_t* kSysProfile = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
        const wchar_t* kDesktop    = L"Control Panel\\Desktop";

        bool RegGetStrIs(HKEY root, const wchar_t* path, const wchar_t* name, const wchar_t* want)
        {
            HKEY k;
            if (RegOpenKeyExW(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
            wchar_t buf[64] = {};
            DWORD sz = sizeof(buf);
            LONG r = RegQueryValueExW(k, name, nullptr, nullptr, (LPBYTE)buf, &sz);
            RegCloseKey(k);
            return r == ERROR_SUCCESS && wcscmp(buf, want) == 0;
        }

        bool ReadFiveM(int i)
        {
            switch (i)
            {
            case 0: return RegGetStrIs(HKLM, kGamesTask, L"Background Only", L"False") &&
                           RegGet(HKLM, kGamesTask, L"Clock Rate", 0) == 0x2710;
            case 1: return RegGet(HKLM, kSysProfile, L"SystemResponsiveness", 20) == 0;
            case 2: return RegGet(HKLM, kSysProfile, L"NetworkThrottlingIndex", 10) == 0xFFFFFFFF;
            case 3: return RegGetStrIs(HKCU, kDesktop, L"MenuShowDelay", L"0");
            case 4: return RegGetStrIs(HKCU, kDesktop, L"AutoEndTasks", L"1");
            case 5: return RegGetStrIs(HKCU, kDesktop, L"LowLevelHooksTimeout", L"1000");
            case 6: return RegGetStrIs(HKCU, kDesktop, L"WaitToKillServiceTimeout", L"1000");
            case 7: return RegGetStrIs(HKCU, kDesktop, L"DragFullWindows", L"0");
            }
            return false;
        }

        // --------------------------------------------------------- Gecikme azaltma
        const wchar_t* kKernel  = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";
        const wchar_t* kLanman  = L"SYSTEM\\CurrentControlSet\\services\\LanmanServer\\Parameters";

        bool ReadDelay(int i)
        {
            switch (i)
            {
            case 0: return RegGet(HKLM, kKernel, L"GlobalTimerResolutionRequests", 0) == 1;
            case 1: return RegGet(HKLM, kKernel, L"DpcWatchdogProfileOffset", 1) == 0;
            case 2: return RegGet(HKLM, kKernel, L"DisableExceptionChainValidation", 0) == 1;
            case 3: return RegGet(HKLM, kKernel, L"InterruptSteeringDisabled", 0) == 1;
            case 4: return RegGet(HKLM, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
                                  L"Win32PrioritySeparation", 2) == 0x28;
            case 5: return RegGet(HKLM, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Reliability",
                                  L"TimeStampInterval", 1) == 0;
            case 6: return RegGet(HKLM, kLanman, L"SharingViolationDelay", 1) == 0;
            case 7: return RegGet(HKLM, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad",
                                  L"AAPThreshold", 0) == 10;
            }
            return false;
        }

        // --------------------------------------------------------- Oyun profilleri
        //
        // Valorant / CS2 / Fortnite profilleri. Üçünün de kendine ait tek
        // yazma yolu yok; hepsi aşağıdaki IFEO ve oyun görevi anahtarlarını
        // paylaşıyor. Okuma tarafı bu yüzden temsilî değere bakar: bir anahtarı
        // "açık" saymak için yeterli sayıda bağımsız değerin hepsinin yerinde
        // olması gerekir, aksi hâlde anahtar kısmen yazılmış bir sistemde
        // "kapalı" görünür ve kullanıcı bozuk ayarı görmez.
        const wchar_t* kIfeo    = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";
        const wchar_t* kLayers  = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";

        bool RegIfeoHigh(HKEY root, const wchar_t* exe)
        {
            wchar_t path[256];
            swprintf_s(path, L"%s\\%s\\PerfOptions", kIfeo, exe);
            return RegGet(root, path, L"CpuPriorityClass", 2) == 1 &&
                   RegGet(root, path, L"IoPriority", 2) == 1 &&
                   RegGet(root, path, L"PagePriority", 5) == 6;
        }

        bool RegIfeoLow(HKEY root, const wchar_t* exe)
        {
            wchar_t path[256];
            swprintf_s(path, L"%s\\%s\\PerfOptions", kIfeo, exe);
            return RegGet(root, path, L"CpuPriorityClass", 2) == 4 &&
                   RegGet(root, path, L"IoPriority", 2) == 3;
        }

        bool RegLayersFullscreenOff(HKEY root, const wchar_t* exe)
        {
            wchar_t path[256];
            swprintf_s(path, L"%s\\%s", kLayers, exe);
            HKEY k;
            if (RegOpenKeyExW(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
            // Değer adı boş bir REG_SZ'dir; adı olmayan kayıtları okumak da
            // mümkündür, ama okuma başarısızlığını "kapalı" saymıyoruz.
            DWORD type = 0, sz = 0;
            LONG r = RegQueryValueExW(k, L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE",
                                      nullptr, &type, nullptr, &sz);
            RegCloseKey(k);
            return r == ERROR_SUCCESS && type == REG_SZ && sz > 0;
        }

        // Üç oyun da aynı "oyun görevi" yazısını paylaşıyor.
        bool GamesTaskHigh()
        {
            return RegGet(HKLM, kGamesTask, L"Priority", 2) == 6 &&
                   RegGet(HKLM, kGamesTask, L"GPU Priority", 0) == 8;
        }

        bool MouseRaw()
        {
            return RegGet(HKCU, L"Control Panel\\Mouse", L"MouseSpeed", 1) == 0 &&
                   RegGetStrIs(HKCU, L"Control Panel\\Mouse", L"MouseThreshold1", L"0");
        }

        bool GameModeOn()
        {
            return RegGet(HKCU, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 0) == 1;
        }

        bool DvrOff()
        {
            return RegGet(HKCU, L"System\\GameConfigStore", L"GameDVR_Enabled", 1) == 0 &&
                   RegGet(HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                           L"AppCaptureEnabled", 1) == 0;
        }

        bool ReadValorant(int i)
        {
            switch (i)
            {
            case 0: return RegIfeoHigh(HKLM, L"VALORANT-Win64-Shipping.exe");
            case 1: return RegIfeoLow(HKLM, L"RiotClientServices.exe") &&
                           RegIfeoLow(HKLM, L"RiotClientUx.exe");
            case 2: return RegIfeoLow(HKLM, L"vgc.exe") &&
                           RegIfeoLow(HKLM, L"vgtray.exe");
            case 3: return RegLayersFullscreenOff(HKCU, L"VALORANT-Win64-Shipping.exe");
            case 4: return GamesTaskHigh();
            case 5: return RegGetStrIs(HKCU, kDesktop, L"LowLevelHooksTimeout", L"1000");
            case 6: return RegGet(HKLM, kSysProfile, L"NetworkThrottlingIndex", 10) == 0xFFFFFFFF;
            case 7: return RegGet(HKLM, kSysProfile, L"SystemResponsiveness", 20) == 0;
            }
            return false;
        }

        bool ReadCS2(int i)
        {
            switch (i)
            {
            case 0: return RegIfeoHigh(HKLM, L"cs2.exe");
            case 1: return RegIfeoLow(HKLM, L"steam.exe");
            case 2: return RegIfeoLow(HKLM, L"steamwebhelper.exe");
            case 3: return RegGetStrIs(HKCU, L"Software\\Valve\\Steam", L"NoLazyLoading", L"1");
            case 4: return GamesTaskHigh();
            case 5: return GameModeOn();
            case 6: return DvrOff();
            case 7: return MouseRaw();
            }
            return false;
        }

        bool ReadFortnite(int i)
        {
            switch (i)
            {
            case 0: return RegIfeoHigh(HKLM, L"FortniteClient-Win64-Shipping.exe");
            case 1: return RegIfeoLow(HKLM, L"EpicGamesLauncher.exe") &&
                           RegIfeoLow(HKLM, L"EpicPortalLauncher.exe");
            case 2: return RegIfeoLow(HKLM, L"EpicWebHelper.exe") &&
                           RegIfeoLow(HKLM, L"EOS.exe");
            case 3: return RegLayersFullscreenOff(HKCU, L"FortniteClient-Win64-Shipping.exe");
            case 4: return GamesTaskHigh();
            case 5: return GameModeOn();
            case 6: return MouseRaw();
            case 7: return RegGet(HKLM, kSysProfile, L"SystemResponsiveness", 20) == 0;
            }
            return false;
        }

        #undef HKCU
        #undef HKLM
    }

    bool Read(int cat, int idx)
    {
        switch (cat)
        {
        case 0: return ReadPerf(idx);
        case 1: return ReadGame(idx);
        case 2: return ReadPriv(idx);
        case 3: return ReadVis(idx);
        case 4: return ReadGames(idx);
        case 5: return ReadFiveM(idx);
        case 6: return ReadDelay(idx);
        case 7: return ReadValorant(idx);
        case 8: return ReadCS2(idx);
        case 9: return ReadFortnite(idx);
        }
        return false;
    }

    bool Apply(int cat, int idx, bool enable)
    {
        switch (cat)
        {
        case 0: return ApplyPerf(idx, enable);
        case 1: return ApplyGame(idx, enable);
        case 2: return ApplyPriv(idx, enable);
        case 3: return ApplyVis(idx, enable);
        // Oyunlar, FiveM, gecikme ve oyun profilleri gömülü .reg gövdeleri
        // olarak gelir ve reg.exe üzerinden içe aktarılır.
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        {
            const char* body = regpack::Body(cat, idx, enable);
            return body && regpack::Import(body);
        }
        }
        return false;
    }

    bool IsRegPack(int cat) { return cat >= 4 && cat <= 9; }

    bool IsShared(int cat, int idx)
    {
        // Birden fazla anahtarın aynı registry değerini sahiplendiği yerler.
        // Bunlardan biri geri alındığında diğeri de sessizce kapanırdı; bu yüzden
        // uygulayıcı, açık olanın değerini yeniden yazar:
        //   SystemResponsiveness   -> Perf[5], Game[5], FiveM[1]
        //   DisablePagingExecutive -> Perf[7], Games[5]
        //   GameDVR_FSEBehaviorMode-> Game[1], Games[1]
        //   GPU Priority, Priority, Scheduling Category, SFIO Priority
        //                         -> Games[0], FiveM[0]
        //   Affinity, Background Only, Clock Rate
        //                         -> Games[2], FiveM[0]
        //
        // Oyunlar, FiveM ve oyun profilleri (Valorant/CS2/Fortnite) aynı "Games"
        // görev anahtarına yazıyor; ayrıca profil kategorileri arasında da ortak
        // değerler var:
        //   Games görevi (Priority, GPU Priority, Scheduling Category, SFIO)
        //        -> Games[0], FiveM[0], Valorant[4], CS2[4], Fortnite[4]
        //   SystemResponsiveness
        //        -> Perf[5], Game[5], FiveM[1], Valorant[7], Fortnite[7]
        //   NetworkThrottlingIndex -> FiveM[2], Valorant[6]
        //   LowLevelHooksTimeout   -> FiveM[5], Valorant[5]
        //   GameDVR / FSE          -> Game[1], Games[1], CS2[6]
        //   Oyun Modu               -> Game[0], Game[7], CS2[5], Fortnite[5]
        //   Ham fare                -> Game[4], CS2[7], Fortnite[6]
        //
        // Liste elle tutuluyor ve bu yüzden yeni bir ayar eklendiğinde gözden
        // geçirilmeli. make_tweak_table.ps1 hangi değerlerin paylaşıldığını
        // hesaplayıp tabloya yazıyor.
        if (cat == 0 && (idx == 5 || idx == 7)) return true;
        if (cat == 1 && (idx == 0 || idx == 1 || idx == 4 || idx == 5 || idx == 7)) return true;
        if (cat == 4 && (idx == 0 || idx == 1 || idx == 2 || idx == 5)) return true;
        if (cat == 5 && (idx == 0 || idx == 1)) return true;
        if (cat == 7 && (idx == 4 || idx == 5 || idx == 6 || idx == 7)) return true;
        if (cat == 8 && (idx == 4 || idx == 5 || idx == 6 || idx == 7)) return true;
        if (cat == 9 && (idx == 4 || idx == 5 || idx == 6 || idx == 7)) return true;
        return false;
    }

    bool ApplyCategory(int cat, const bool* states, const bool* mask, int count)
    {
        if (!IsRegPack(cat) || !states) return false;

        const char* bodies[16];
        int n = 0;
        for (int i = 0; i < count && n < 16; ++i)
        {
            if (mask && !mask[i]) continue;
            if (const char* b = regpack::Body(cat, i, states[i]))
                bodies[n++] = b;
        }

        return n == 0 || regpack::ImportMany(bodies, n);
    }
}
