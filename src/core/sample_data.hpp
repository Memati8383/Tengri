#pragma once

// Ekran görüntüsü verisi.
//
// docs/screenshots altında duran görseller gerçek bir makinede üretiliyor.
// Sistem bilgisi, başlatma listesi, ağ bağdaştırıcısı, yerel IP ve HWID gibi
// alanlar doğrudan o makineyi tanımladığı için, yakalama derlemesi bu alanları
// sabit örnek değerlerle çiziyor. Değerlerin gerçek kaynağı yine okunuyor;
// yalnızca gösterilen metin değişiyor.
//
// Kip TENGRI_SHOT ortam değişkeniyle açılıyor ve betiğin dışında hiçbir
// kurulumda devreye girmiyor.
//
// Neden dosya üstüne boyama (retouch) değil: üstüne çizilen metin arayüzün
// kendi yazı tipiyle birebir aynı çıkmaz ve "gerçek derlemeden alındı" denen
// bir görsel artık gerçek olmazdı. Burada pikseller gerçekten çiziliyor.
//
// Neden yalnızca gösterim: örnek başlatma girdileri uydurma adlar taşıyor ama
// devre dışı bırak / kaldır düğmeleri gerçek registry değerlerini hedeflerdi —
// sahte etiket, doğru olmayan bir değeri silerdi. O yüzden bu kipte yazma
// yolları kapalı (startup.cpp ve app.cpp tarafında denetleniyor).

#include <string>
#include <vector>
#include <windows.h>

#include "sysinfo_detail.hpp"
#include "sysinfo_wmi.hpp"
#include "startup.hpp"

namespace sample
{
    // Bayrak bir kez belirlenir ve süreç boyunca değişmez: InitFromEnv() uygulamanın
    // en başında, hiçbir bilgi okunmadan çağrılır. Okunan değerler önbelleklendiği
    // için kipin ortada değişmesi aynı oturumun yarısını gerçek, yarısını örnek
    // veri yapardı. Ayrı bir işlev olması testin de aynı kapıdan geçebilmesi için.
    inline bool& Flag()
    {
        static bool on = false;
        return on;
    }

    inline bool Active() { return Flag(); }

    inline void InitFromEnv()
    {
        wchar_t buf[8] = {};
        DWORD n = GetEnvironmentVariableW(L"TENGRI_SHOT", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        Flag() = (n > 0 && buf[0] == L'1' && buf[1] == L'\0');
    }

    inline const char* Hwid()         { return "1A2B-3C4D-5E6F-7A8B"; }
    inline const char* ComputerName() { return "DEMO-MAKINE"; }
    inline const char* UserName()     { return "Demo Kullanici"; }
    inline const char* CpuName()      { return "AMD Ryzen 5 5600 6-Core Processor"; }
    inline const char* GpuName()      { return "NVIDIA GeForce RTX 3060"; }
    inline const char* AdapterName()  { return "Intel(R) Ethernet Connection (17) I219-V"; }
    inline const char* LocalIp()      { return "192.168.1.50"; }
    inline const char* GatewayIp()    { return "192.168.1.1"; }

    // Ayarlar → Hesap kartındaki maske: gerçek anahtarın ilk dört ve son üç
    // hanesi göründüğü için o satır da örnek değerle çizilir.
    inline const char* LicenseMask()  { return "DEMO-\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2-\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2-260"; }

    // Toplanan yapının donanıma özel alanları örnek değerlerle değiştirilir.
    // Kurulum tarihi, ekran çözünürlüğü ve dil olduğu gibi bırakılır: makineyi
    // tanımlamıyorlar.
    inline void ApplyTo(sysdetail::Info& d)
    {
        d.cpuCores    = 6;
        d.cpuThreads  = 12;
        d.cpuClock    = "3.50 GHz";
        d.gpuVram     = "12 GB";
        d.gpuDriver   = "32.0.15.6094";
        d.ramTotal    = "16 GB";
        d.motherboard = "MSI MAG B550M MORTAR";
        d.biosVersion = "1.8.0";
        d.secureBoot  = sysdetail::SecureBootState::Enabled;
        d.virtState   = sysdetail::VirtState::Disabled;
    }

    // WMI sonucu tarama sırasıyla doldurulur; modül listesi seri numarası ve
    // parça numarası taşıdığı için tamamen kaldırılır, yerinde yalnızca özet
    // satırlar kalır.
    inline void ApplyTo(syswmi::Result& w)
    {
        w.ram.clear();
        w.ramSpeed    = "3200 MHz (JEDEC 2133 MHz)";
        w.ramSlots    = "2 / 4";
        w.monitorName = "U2722DX";
        w.monitorVendor = "Dell";
        w.tpmPresent  = "Yes";
        w.tpmReady    = "Yes";
        w.tpmSpec     = "2.0";
    }

    // Başlatma sayfasının listesi. Bir kullanıcı, bir makine ve bir de kapalı
    // durumdaki girdi: üç sekmenin de dolu görünmesi için.
    inline std::vector<startup::Entry> StartupEntries()
    {
        std::vector<startup::Entry> v;

        startup::Entry a;
        a.scope        = startup::Scope::UserRun;
        a.name         = L"Discord";
        a.command      = L"\"C:\\Users\\Demo\\AppData\\Local\\Discord\\Update.exe\" --processStart Discord.exe";
        a.resolvedPath = L"C:\\Users\\Demo\\AppData\\Local\\Discord\\Update.exe";
        a.publisher    = L"Discord Inc.";
        a.enabled      = true;
        a.isTengri     = false;
        a.impact       = startup::Impact::Medium;
        a.abnormal     = false;
        v.push_back(a);

        startup::Entry b;
        b.scope        = startup::Scope::UserRun;
        b.name         = L"Spotify";
        b.command      = L"\"C:\\Program Files\\Spotify\\Spotify.exe\" --autostart --minimized";
        b.resolvedPath = L"C:\\Program Files\\Spotify\\Spotify.exe";
        b.publisher    = L"Spotify AB";
        b.enabled      = true;
        b.isTengri     = false;
        b.impact       = startup::Impact::Low;
        b.abnormal     = false;
        v.push_back(b);

        startup::Entry c;
        c.scope        = startup::Scope::MachineRun;
        c.name         = L"TeamsMachineInstaller";
        c.command      = L"\"C:\\Program Files\\Teams Installer\\Teams.exe\" --installSource=systeminstaller";
        c.resolvedPath = L"C:\\Program Files\\Teams Installer\\Teams.exe";
        c.publisher    = L"Microsoft Corporation";
        c.enabled      = false;
        c.isTengri     = false;
        c.impact       = startup::Impact::High;
        c.abnormal     = false;
        v.push_back(c);

        return v;
    }
}
