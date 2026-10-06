// Kayıt defteri ayar gövdeleri.
//
// Her ayar bir çift olarak tutulur: { etkin olduğundaki gövde, kapalı olduğundaki gövde }.
// Kapalı gövde hiçbir şey yapmaz; belgelenmiş varsayılanı geri yükler ya da değeri
// anahtardan siler. Böylece geri alma tahminle değil, gerçek bir ters işlemle yapılır.

#include "regpack.hpp"
#include "brand.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <cstring>
#include <cstdio>

namespace regpack
{
    namespace
    {
        const char* kHeader = "Windows Registry Editor Version 5.00\r\n\r\n";

        bool RunRegImport(const wchar_t* path)
        {
            // reg.exe gerçek bir çıkış kodu döndürür; regedit /s ise her zaman 0 döner ve hatalı
            // bir içe aktarmayı bile başarı gibi gösterir.
            std::wstring cmd = L"reg.exe import \"";
            cmd += path;
            cmd += L"\"";

            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags     = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};

            std::wstring buf = cmd;
            if (!CreateProcessW(nullptr, &buf[0], nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                return false;

            // Zaman aşımında çıkış kodu hâlâ STILL_ACTIVE olur ve içe aktarma denetimsiz
            // çalışmayı sürdürürdü; bu yüzden alt süreç sonlandırılır ve ayar
            // başarısız olarak bildirilir.
            if (WaitForSingleObject(pi.hProcess, 20000) == WAIT_TIMEOUT)
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
    }

    bool Import(const char* text)
    {
        if (!text || !*text) return false;

        wchar_t dir[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, dir)) return false;

        // HKLM'ye dokunan her ayarda uygulama yükseltilmiş yetkiyle çalışır, dolayısıyla geçici
        // dosya herkesin yazabildiği bir dizine düşer. Yalnızca pid+tick'ten türetilen bir ad
        // yerel bir saldırgan tarafından önceden oluşturulup bir junction'a yönlendirilebilecek
        // kadar tahmin edilebilirdi ve bu, `reg import`'u keyfi bir yazma işlemine dönüştürürdü.
        // Rastgele ad + CREATE_NEW + OPEN_REPARSE_POINT bu pencereyi kapatıyor.
        unsigned char rnd[16] = {};
        // CryptGenRandom üç argüman alır: (hProv, dwLen, pbBuffer) — iki değil, ilk parametre
        // sağlayıcıdır ve NULL verildiğinde varsayılan sağlayıcı kullanılır. HCRYPTPROV_LEGACY
        // sabitinin kendisi her SDK'da derlenmediği için NULL tercih ediliyor.
        if (!CryptGenRandom(0, (DWORD)sizeof(rnd), rnd)) return false;

        unsigned h[4] = {};
        memcpy_s(h, sizeof(h), rnd, sizeof(rnd));

        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s%s%lu_%08X%08X%08X%08X.reg", dir, brand::kTempPrefixW,
                   GetCurrentProcessId(), h[0], h[1], h[2], h[3]);

        // reg.exe satır sonlarını CRLF bekler, gömülü gövdeler ise düz LF ile yazılmıştır;
        // dönüştürmezsek anahtarlar tek satırda birleşir ve içe aktarma sessizce bozulur.
        std::string norm;
        norm.reserve(strlen(text) + 128);
        for (const char* p = text; *p; ++p)
        {
            if (*p == '\n' && (p == text || p[-1] != '\r')) norm += '\r';
            norm += *p;
        }

        const int wn = MultiByteToWideChar(CP_UTF8, 0, norm.c_str(), -1, nullptr, 0);
        if (wn <= 1) return false;
        std::wstring w((size_t)wn - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, norm.c_str(), -1, &w[0], wn);

        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW | FILE_FLAG_OPEN_REPARSE_POINT,
                               FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;

        // "Version 5.00" .reg dosyaları UTF-16LE'dir ve bir BOM ile başlamak zorundadır;
        // BOM olmadan içerik yanlış hizada okunur.
        const wchar_t bom = 0xFEFF;
        DWORD written = 0;
        BOOL ok = WriteFile(f, &bom, sizeof(bom), &written, nullptr);
        ok = ok && WriteFile(f, w.data(), (DWORD)(w.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(f);

        const bool imported = ok && RunRegImport(path);
        DeleteFileW(path);
        return imported;
    }

    bool ImportMany(const char* const* bodies, int count)
    {
        if (!bodies || count <= 0) return false;

        std::string all = kHeader;
        for (int i = 0; i < count; ++i)
        {
            if (!bodies[i]) continue;
            // Her gövdenin kendi başlık satırı var; birleştirilen dosyada ikinci bir başlık
            // hatalı içe aktarmaya yol açardı, bu yüzden yalnızca ilki tutuluyor.
            const char* keys = strchr(bodies[i], '\n');
            all += keys ? keys + 1 : bodies[i];
            all += "\n";
        }
        return Import(all.c_str());
    }

    // ------------------------------------------------------------------ Oyunlar

    const char* kGames[8][2] = {
        {   // 0 - oyun görevi zamanlama önceliği
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"GPU Priority"=dword:00000008
"Priority"=dword:00000006
"Scheduling Category"="High"
"SFIO Priority"="High"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"GPU Priority"=dword:00000002
"Priority"=dword:00000002
"Scheduling Category"="Medium"
"SFIO Priority"="Normal"
)"  },
        {   // 1 - Game DVR / tam ekran optimizasyonları
        // Kapalı gövde, açık gövdenin yazdığı kayıtları silmek yerine Windows'un kendi
        // varsayılan değerlerini geri yükler; oyun kayıtları yeniden yazıldığında
        // kullanıcının elde ettiği sonuç ile ayarı kapatmanın sonucu aynı olsun diye.
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\System\GameConfigStore]
"GameDVR_Enabled"=dword:00000000
"GameDVR_FSEBehaviorMode"=dword:00000002

[HKEY_LOCAL_MACHINE\SOFTWARE\Policies\Microsoft\Windows\GameDVR]
"AllowGameDVR"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\System\GameConfigStore]
"GameDVR_Enabled"=dword:00000001
"GameDVR_FSEBehaviorMode"=dword:00000000

[HKEY_LOCAL_MACHINE\SOFTWARE\Policies\Microsoft\Windows\GameDVR]
"AllowGameDVR"=dword:00000001
)"  },
        {   // 2 - saat hızı / affinity / yalnızca arka plan
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Affinity"=dword:00000000
"Background Only"="False"
"Clock Rate"=dword:00002710
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Affinity"=-
"Background Only"="True"
"Clock Rate"=-
)"  },
        {   // 3 - çekirdek başına GPU DPC
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\GraphicsDrivers]
"RmGpsPsEnablePerCpuCoreDpc"=dword:00000001

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Power]
"RmGpsPsEnablePerCpuCoreDpc"=dword:00000001

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm]
"RmGpsPsEnablePerCpuCoreDpc"=dword:00000001

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm\NVAPI]
"RmGpsPsEnablePerCpuCoreDpc"=dword:00000001

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm\Global\NVTweak]
"RmGpsPsEnablePerCpuCoreDpc"=dword:00000001
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\GraphicsDrivers]
"RmGpsPsEnablePerCpuCoreDpc"=-

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Power]
"RmGpsPsEnablePerCpuCoreDpc"=-

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm]
"RmGpsPsEnablePerCpuCoreDpc"=-

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm\NVAPI]
"RmGpsPsEnablePerCpuCoreDpc"=-

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm\Global\NVTweak]
"RmGpsPsEnablePerCpuCoreDpc"=-
)"  },
        {   // 4 - NVIDIA sürücü iş parçacığı önceliği
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm\Parameters]
"ThreadPriority"=dword:0000001f
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm\Parameters]
"ThreadPriority"=-
)"  },
        {   // 5 - çekirdek kodu bellekte tut
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"DisablePagingExecutive"=dword:00000001
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"DisablePagingExecutive"=dword:00000000
)"  },
        {   // 6 - büyük sistem önbelleği
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"LargeSystemCache"=dword:00000001
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"LargeSystemCache"=dword:00000000
)"  },
        {   // 7 - bellek optimizasyonu
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"IoPageLockLimit"=dword:00100000
"PoolUsageMaximum"=dword:00000060
"SecondLevelDataCache"=dword:00000c00
"SessionPoolSize"=dword:000000c0
"SessionViewSize"=dword:000000c0
"PagedPoolSize"=dword:000000c0
"PagedPoolQuota"=dword:00000000
"NonPagedPoolQuota"=dword:00000000
"NonPagedPoolSize"=dword:00000000
"SystemPages"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"IoPageLockLimit"=-
"PoolUsageMaximum"=-
"SecondLevelDataCache"=-
"SessionPoolSize"=-
"SessionViewSize"=-
"PagedPoolSize"=-
"PagedPoolQuota"=-
"NonPagedPoolQuota"=-
"NonPagedPoolSize"=-
"SystemPages"=-
)"  },
    };

    // ------------------------------------------------------------------ FiveM

    const char* kFiveM[8][2] = {
        {   // 0 - tam oyun görevi boost profili
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Affinity"=dword:00000000
"Background Only"="False"
"Clock Rate"=dword:00002710
"GPU Priority"=dword:00000008
"Priority"=dword:00000006
"Scheduling Category"="High"
"SFIO Priority"="High"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Affinity"=-
"Background Only"=-
"Clock Rate"=-
"GPU Priority"=dword:00000002
"Priority"=dword:00000002
"Scheduling Category"="Medium"
"SFIO Priority"="Normal"
)"  },
        {   // 1 - SystemResponsiveness 0
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"SystemResponsiveness"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"SystemResponsiveness"=dword:00000014
)"  },
        {   // 2 - ağ kısıtlaması kapalı
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"NetworkThrottlingIndex"=dword:ffffffff
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"NetworkThrottlingIndex"=dword:0000000a
)"  },
        {   // 3 - anında açılan menüler
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"MenuShowDelay"="0"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"MenuShowDelay"="400"
)"  },
        {   // 4 - servis ve uygulama sonlandırma süreleri
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"AutoEndTasks"="1"
"HungAppTimeout"="4000"
"WaitToKillAppTimeout"="5000"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"AutoEndTasks"="0"
"HungAppTimeout"="5000"
"WaitToKillAppTimeout"="20000"
)"  },
        {   // 5 - düşük seviye kanca zaman aşımı
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"LowLevelHooksTimeout"="1000"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"LowLevelHooksTimeout"="5000"
)"  },
        {   // 6 - servis kapatma zaman aşımı
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"WaitToKillServiceTimeout"="1000"

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control]
"WaitToKillServiceTimeout"="2000"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"WaitToKillServiceTimeout"="5000"

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control]
"WaitToKillServiceTimeout"="5000"
)"  },
        {   // 7 - daha hafif pencere taşıma
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"DragFullWindows"="0"
"ForegroundLockTimeout"="150000"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"DragFullWindows"="1"
"ForegroundLockTimeout"="200000"
)"  },
    };

    // ------------------------------------------------------------------ Gecikme

    const char* kDelay[8][2] = {
        {   // 0 - genel zamanlayıcı çözünürlüğü
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"GlobalTimerResolutionRequests"=dword:00000001
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"GlobalTimerResolutionRequests"=dword:00000000
)"  },
        {   // 1 - DPC gözetçi profili
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"DpcWatchdogProfileOffset"=dword:00000000
"SeTokenSingletonAttributesConfig"=dword:00000003
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"DpcWatchdogProfileOffset"=-
"SeTokenSingletonAttributesConfig"=-
)"  },
        {   // 2 - özel durum zinciri doğrulaması
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"DisableExceptionChainValidation"=dword:00000001
"KernelSEHOPEnabled"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"DisableExceptionChainValidation"=dword:00000000
"KernelSEHOPEnabled"=dword:00000001
)"  },
        {   // 3 - kesme yönlendirme
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"InterruptSteeringDisabled"=dword:00000001
"obcaseinsensitive"=dword:00000001
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\kernel]
"InterruptSteeringDisabled"=dword:00000000
"obcaseinsensitive"=-
)"  },
        {   // 4 - Win32PrioritySeparation 0x28
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\PriorityControl]
"Win32PrioritySeparation"=dword:00000028
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\PriorityControl]
"Win32PrioritySeparation"=dword:00000002
)"  },
        {   // 5 - güvenilirlik zaman damgası aralığı
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Reliability]
"TimeStampInterval"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Reliability]
"TimeStampInterval"=dword:00000001
)"  },
        {   // 6 - LanmanServer paylaşım ihlali düzeltmesi
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\services\LanmanServer\Parameters]
"autodisconnect"=dword:ffffffff
"Size"=dword:00000003
"EnableOplocks"=dword:00000000
"IRPStackSize"=dword:00000020
"SharingViolationDelay"=dword:00000000
"SharingViolationRetries"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\services\LanmanServer\Parameters]
"autodisconnect"=dword:0000000f
"Size"=dword:00000001
"EnableOplocks"=dword:00000001
"SharingViolationDelay"=-
"SharingViolationRetries"=-
)"  },
        {   // 7 - fare ve giriş gecikmesi düzeltmesi
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PrecisionTouchPad]
"AAPThreshold"=dword:0000000a

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"FeatureSettings"=dword:00000001
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PrecisionTouchPad]
"AAPThreshold"=dword:00000000

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management]
"FeatureSettings"=dword:00000000
)"  },
    };

    const char* Body(int cat, int idx, bool enable)
    {
        if (idx < 0 || idx > 7) return nullptr;
        const int k = enable ? 0 : 1;
        switch (cat)
        {
        case 4: return kGames[idx][k];
        case 5: return kFiveM[idx][k];
        case 6: return kDelay[idx][k];
        }
        return nullptr;
    }

    bool ApplyRamProfile(int gb)
    {
        // SvcHostSplitThresholdInKB, KB cinsinden kurulu RAM'dir; 0 değeri verildiğinde varsayılan 0x380000 kullanılır.
        const unsigned threshold = gb > 0 ? (unsigned)gb * 1024u * 1024u : 0x380000u;

        auto write = [threshold](const char* set) {
            char body[512];
            snprintf(body, sizeof(body),
                     "%s"
                     "[HKEY_LOCAL_MACHINE\\SYSTEM\\%s\\Control]\r\n"
                     "\"SvcHostSplitThresholdInKB\"=dword:%08x\r\n",
                     kHeader, set, threshold);
            return Import(body);
        };

        // Ayrı ayrı içe aktarılıyor: ControlSet001 yok ya da kilitli olabilir ve birleşik bir
        // .reg dosyası, canlı küme başarılı olsa bile tüm profili başarısız olarak bildirirdi.
        const bool live = write("CurrentControlSet");
        write("ControlSet001");
        return live;
    }
}
