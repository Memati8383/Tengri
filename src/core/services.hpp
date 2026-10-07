#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Oyun Modu hizmet yöneticisi.
//
// Windows arka planda bir dizi "istisnai durumlar için" hizmet çalıştırır:
// SysMain (SuperFetch), DiagTrack (telemetri), WSearch (dosya indeksleme),
// Xbl* (Xbox entegrasyonu), WbioSrvc (biyometri) gibi. Her biri çoğu
// kullanıcı için faydalıdır ama bir oyun oturumunda disk I/O, bellek ve
// CPU'yu çalar.
//
// Burası bu hizmetleri DURDURMAZ; başlangıç türünü değiştirir. Durdurma
// anlıktır ve bir sonraki oturumda unutulur; başlangıç türü değişikliği
// kalıcıdır ve "oyun profili" ile "varsayılan profil" arasında tekrarlanabilir
// geçişe izin verir.
//
// Beyaz liste yaklaşımı: dokunulabilecek hizmetler kodda listelenmiştir
// (`kPolicy`). Kullanıcı serbest bir hizmet adı giremez; bu, Windows'un
// kritik hizmetlerini yanlışlıkla devre dışı bırakmayı imkânsız kılar.

namespace services
{
    enum class StartType : uint8_t
    {
        Unknown = 0,
        Boot,            // SERVICE_BOOT_START
        System,          // SERVICE_SYSTEM_START
        Automatic,       // SERVICE_AUTO_START
        AutomaticDelayed,// SERVICE_AUTO_START + DelayedAutostart=1
        Manual,          // SERVICE_DEMAND_START
        Disabled,        // SERVICE_DISABLED
    };

    enum class Severity : uint8_t
    {
        Safe = 0,       // çoğu masaüstünde kapatılabilir
        Caution,        // bazı özellikler kaybolabilir (örn. arama)
        Risky,          // yalnızca bilinçli kullanıcı için
    };

    struct Policy
    {
        const wchar_t* serviceName;  // registry adı (DisplayName değil)
        const wchar_t* reason;       // kısa gerekçe (log/tooltip)
        Severity       severity;
        StartType      gameProfile;  // oyun oturumunda hedef durum
    };

    struct Entry
    {
        std::wstring serviceName;
        std::wstring displayName;
        StartType    current;
        bool         running;
        const Policy* policy;        // kPolicy içindeki girdi; sahip olunmaz
    };

    // Beyaz listeyi uygulamanın çalışma zamanında da sorgulayabilmek için
    // dışa açılır (çağıran kopya üretir, sahiplik kodda kalır).
    void GetPolicy(const Policy** out, int* count);

    // Beyaz listedeki her hizmet için mevcut durumu okur. Sırası `kPolicy`
    // ile aynı; bir hizmet yoksa (örn. XblGameSave bazı SKU'larda) entry
    // yine döner ama current=Unknown olur.
    std::vector<Entry> Query();

    // Tek bir hizmetin başlangıç türünü değiştirir. HKLM'e yazdığı için
    // yükseltilmiş süreç ister; değilse false döner.
    bool SetStartType(const std::wstring& serviceName, StartType t);

    // Beyaz listedeki tüm hizmetleri `gameProfile` değerine çeker. Her
    // değişiklik önce `HKLM\SOFTWARE\TENGRI\ServicesBackup\<name>\Start`
    // altına yedeklenir ki `RestoreGameProfile` gerçek bir geri alma olsun.
    //
    // Dönen değer "başarıyla değiştirilen hizmet sayısı".
    int ApplyGameProfile();

    // Yedekten geri yükler. Yedeği olmayan hizmet dokunulmaz.
    int RestoreGameProfile();

    // İnsan okunur başlangıç türü etiketi.
    const wchar_t* StartTypeLabel(StartType t);
}
