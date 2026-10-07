#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Başlangıç öğeleri yöneticisi.
//
// Windows'un otomatik başlatma mekanizmaları çok sayıdadır (Zamanlanmış
// görevler, hizmetler, WMI tetikleyicileri, kabuk klasörleri). TENGRI bu
// aşamada yalnızca EN YAYGIN olanı yönetir:
//
//   HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//   HKCU\Software\Microsoft\Windows\CurrentVersion\RunOnce
//   HKLM\Software\Microsoft\Windows\CurrentVersion\Run
//   HKLM\Software\Microsoft\Windows\CurrentVersion\RunOnce
//
// Devre dışı bırakma: değeri silmek yerine kendi yedek anahtarımıza
// taşınır. Böylece açıp kapamak gerçek bir ters işlemdir ve kullanıcı
// değeri kaybetmez. Windows'un StartupApproved anahtarına dokunmuyoruz;
// o anahtarın anlambilimi sürüm başına değişiyor ve çakışma riski var.
//
// HKLM yazma yetki ister; mevcut elevate kanalı kullanılır (ayrı
// yardımcı exe YOK).

namespace startup
{
    enum class Scope : uint8_t
    {
        UserRun = 0,       // HKCU Run
        UserRunOnce = 1,
        MachineRun = 2,    // HKLM Run — yazma UAC ister
        MachineRunOnce = 3,
    };

    enum class Impact : uint8_t
    {
        Unknown = 0,
        Low,
        Medium,
        High,
    };

    struct Entry
    {
        Scope        scope;
        std::wstring name;         // registry değer adı
        std::wstring command;      // ham değer (tırnaklı yol + argümanlar)
        std::wstring resolvedPath; // ayrıştırılmış exe yolu (tırnaklar kaldırıldı)
        std::wstring publisher;    // VERSIONINFO.CompanyName (yoksa boş)
        bool         enabled;      // true: aktif değer; false: yedekte duruyor
        bool         isTengri;     // TENGRI'nin kendi otobaşlangıç kaydı
        Impact       impact;       // kaba tahmin (dosya boyutu + imzalı mı)
    };

    // Dört kapsamın tümünü tarar — hem canlı girdiler hem yedekteki
    // devre dışı girdiler. Pahalı değildir ama disk I/O içerir (publisher
    // için VerQueryValue); arka iş parçacığında çağrılması önerilir.
    std::vector<Entry> Enumerate();

    // enable=true: yedekten geri yazar. enable=false: değeri yedek anahtara
    // taşıyıp orijinalden siler.
    //
    // HKLM için yükseltilmiş süreç gerekir. Normal süreçte false çağrılırsa
    // Return değeri false olur; çağıran elevate yolunu seçmelidir.
    bool SetEnabled(const Entry& e, bool enable);

    // Hem canlı değeri hem yedeği kalıcı siler. HKLM için elevate.
    bool Remove(const Entry& e);

    // İnsan okunur kapsam adı (log/UI için).
    const wchar_t* ScopeLabel(Scope s);
}
