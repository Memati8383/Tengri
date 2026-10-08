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
        // Rastgele ad + CREATE_NEW bu pencereyi kapatıyor: CREATE_NEW, dosya zaten
        // varsa BAŞARISIZ olur, yani saldırganın yolu önceden doldurması işe yaramaz.
        unsigned char rnd[16] = {};
        // CryptGenRandom'in ilk argümanı bir sağlayıcı TUTAMAĞIDIR; NULL "varsayılan
        // sağlayıcı" demek DEĞİLDİR ve bu çağrı ERROR_INVALID_PARAMETER (87) ile
        // başarısız olur. Buraya NULL verildiği için Import, geçici dosyayı hiç
        // yazmadan false dönüyordu — yani Oyunlar/FiveM/Gecikme dahil BÜTÜN regpack
        // kategorileri hiçbir zaman registry'ye ulaşmıyordu.
        //
        // Düzeltme: gerçek bir sağlayıcı açılır. Düşen yol (sağlayıcı açılamazsa)
        // içe aktarmayı sessizce atlamak yerine hata olarak yüzeye çıkar, çünkü
        // rastgele ad üretilemiyorsa güvenlik açığı da kapatılmış sayılmaz.
        HCRYPTPROV hProv = 0;
        if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                                  CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
            return false;
        const BOOL gen = CryptGenRandom(hProv, (DWORD)sizeof(rnd), rnd);
        CryptReleaseContext(hProv, 0);
        if (!gen) return false;

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

        // FILE_FLAG_OPEN_REPARSE_POINT yalnizca ZATEN VAR OLAN bir dosyayi
        // acarken anlamlidir: amacı, yolun sonundaki bileşenin bir junction
        // olmasina izin vermemektir. CREATE_NEW ile birlikte bu bayrak
        // ERROR_INVALID_PARAMETER (87) dondurur ve dosya hic acilmaz.
        //
        // Burada koruma zaten iki katmanli ve bayraksiz calisir:
        //   1) Ad 128 bit CryptGenRandom ile uretilir; pid eklenir ama
        //      belirlenebilir olmaktan cikar.
        //   2) CREATE_NEW, dosya ZATEN varsa basarisiz olur. Yani saldirgan
        //      yolu once doldurup bizi bir junction'a yonlendiremez; acik
        //      olan tek yol bizim yazdigimiz dosyadir.
        // Bu yuzden OPEN_REPARSE_POINT burada hem gerekli degil hem de
        // Import'u tamamen basarisiz kiliyordu.
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
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
"IRPStackSize"=-
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

    // ------------------------------------------------------------------ Valorant
    //
    // Riot'un kendi yayınladığı .reg'lerinde de aynı yol kullanılır: öncelik
    // IFEO altında PerfOptions üzerinden verilir, ekran davranışı ise
    // AppCompatFlags\Layers'ta. Oyun dosyasının yolu kuruluma göre değiştiği
    // için katman değeri yol yerine yalnızca uygulama adıyla yazılır; anahtarın
    // kendisi zaten ada göre açılır.
    const char* kValorant[8][2] = {
        {   // 0 - oyun sürecine yüksek CPU/IO/sayfa önceliği
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\VALORANT-Win64-Shipping.exe\PerfOptions]
"CpuPriorityClass"=dword:00000001
"IoPriority"=dword:00000001
"PagePriority"=dword:00000006
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\VALORANT-Win64-Shipping.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
"PagePriority"=-
)"  },
        {   // 1 - Riot istemci süreçlerini arka plana it
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientServices.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientUx.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientUxRender.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientServices.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientUx.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\RiotClientUxRender.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
)"  },
        {   // 2 - Vanguard gözcüsü ve tepsi simgesini sessize al
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\vgc.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\vgtray.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\vgc.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\vgtray.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
)"  },
        {   // 3 - tam ekran optimizasyonu kapalı
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers\VALORANT-Win64-Shipping.exe]
"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE"=""
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers\VALORANT-Win64-Shipping.exe]
"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE"=-
)"  },
        {   // 4 - oyun görevi zamanlaması
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Priority"=dword:00000006
"Scheduling Category"="High"
"SFIO Priority"="High"
"GPU Priority"=dword:00000008
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Priority"=dword:00000002
"Scheduling Category"="Medium"
"SFIO Priority"="Normal"
"GPU Priority"=dword:00000002
)"  },
        {   // 5 - kanca zaman aşımı
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"LowLevelHooksTimeout"="1000"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Desktop]
"LowLevelHooksTimeout"="5000"
)"  },
        {   // 6 - ağ kısıtlaması kapalı
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"NetworkThrottlingIndex"=dword:ffffffff
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"NetworkThrottlingIndex"=dword:0000000a
)"  },
        {   // 7 - sistem duyarlılığı 0
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"SystemResponsiveness"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"SystemResponsiveness"=dword:00000014
)"  },
    };

    // ------------------------------------------------------------------ CS2
    //
    // Source 2 kendi iş parçacıklarını yönetir; işaretlenen ":threads" gibi
    // başlatma seçeneklerinin artık işe yaramadığı 2023 itibarıyla belgelendi.
    // Bu yüzden profil yalnızca Windows tarafındaki öncelikleri değiştirir:
    // oyuna yüksek öncelik, Steam'a düşük.
    const char* kCS2[8][2] = {
        {   // 0 - cs2.exe yüksek öncelik
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\cs2.exe\PerfOptions]
"CpuPriorityClass"=dword:00000001
"IoPriority"=dword:00000001
"PagePriority"=dword:00000006
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\cs2.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
"PagePriority"=-
)"  },
        {   // 1 - steam.exe arka planda
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\steam.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\steam.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
)"  },
        {   // 2 - steamwebhelper.exe arka planda
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\steamwebhelper.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\steamwebhelper.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
)"  },
        {   // 3 - Steam geç yükleme kapat
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Valve\Steam]
"NoLazyLoading"="1"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Valve\Steam]
"NoLazyLoading"=-
)"  },
        {   // 4 - oyun görevi zamanlaması
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Priority"=dword:00000006
"Scheduling Category"="High"
"SFIO Priority"="High"
"GPU Priority"=dword:00000008
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Priority"=dword:00000002
"Scheduling Category"="Medium"
"SFIO Priority"="Normal"
"GPU Priority"=dword:00000002
)"  },
        {   // 5 - oyun modu
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\GameBar]
"AutoGameModeEnabled"="1"
"AllowAutoGameMode"="1"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\GameBar]
"AutoGameModeEnabled"="0"
"AllowAutoGameMode"="0"
)"  },
        {   // 6 - oyun kaydı kapat
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\System\GameConfigStore]
"GameDVR_Enabled"="0"
"GameDVR_FSEBehaviorMode"="2"

[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\GameDVR]
"AppCaptureEnabled"="0"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\System\GameConfigStore]
"GameDVR_Enabled"="1"
"GameDVR_FSEBehaviorMode"="0"

[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\GameDVR]
"AppCaptureEnabled"="1"
)"  },
        {   // 7 - ham fare
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Mouse]
"MouseSpeed"="0"
"MouseThreshold1"="0"
"MouseThreshold2"="0"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Mouse]
"MouseSpeed"="1"
"MouseThreshold1"="6"
"MouseThreshold2"="10"
)"  },
    };

    // ------------------------------------------------------------------ Fortnite
    const char* kFortnite[8][2] = {
        {   // 0 - istemci sürecine yüksek öncelik
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\FortniteClient-Win64-Shipping.exe\PerfOptions]
"CpuPriorityClass"=dword:00000001
"IoPriority"=dword:00000001
"PagePriority"=dword:00000006
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\FortniteClient-Win64-Shipping.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
"PagePriority"=-
)"  },
        {   // 1 - Epic Games Launcher arka planda
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicGamesLauncher.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicPortalLauncher.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicGamesLauncher.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicPortalLauncher.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
)"  },
        {   // 2 - Epic yardımcı süreçleri arka planda
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicWebHelper.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EOS.exe\PerfOptions]
"CpuPriorityClass"=dword:00000004
"IoPriority"=dword:00000003
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EpicWebHelper.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\EOS.exe\PerfOptions]
"CpuPriorityClass"=-
"IoPriority"=-
)"  },
        {   // 3 - tam ekran optimizasyonu kapalı
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers\FortniteClient-Win64-Shipping.exe]
"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE"=""
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers\FortniteClient-Win64-Shipping.exe]
"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE"=-
)"  },
        {   // 4 - oyun görevi zamanlaması
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Priority"=dword:00000006
"Scheduling Category"="High"
"SFIO Priority"="High"
"GPU Priority"=dword:00000008
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\Games]
"Priority"=dword:00000002
"Scheduling Category"="Medium"
"SFIO Priority"="Normal"
"GPU Priority"=dword:00000002
)"  },
        {   // 5 - oyun modu
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\GameBar]
"AutoGameModeEnabled"="1"
"AllowAutoGameMode"="1"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Microsoft\GameBar]
"AutoGameModeEnabled"="0"
"AllowAutoGameMode"="0"
)"  },
        {   // 6 - ham fare
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Mouse]
"MouseSpeed"="0"
"MouseThreshold1"="0"
"MouseThreshold2"="0"
)",
R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Control Panel\Mouse]
"MouseSpeed"="1"
"MouseThreshold1"="6"
"MouseThreshold2"="10"
)"  },
        {   // 7 - sistem duyarlılığı 0
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"SystemResponsiveness"=dword:00000000
)",
R"(Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile]
"SystemResponsiveness"=dword:00000014
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
        case 7: return kValorant[idx][k];
        case 8: return kCS2[idx][k];
        case 9: return kFortnite[idx][k];
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
