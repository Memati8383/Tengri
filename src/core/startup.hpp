#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Başlangıç öğeleri yöneticisi.
//
// Windows'un otomatik başlatma mekanizmaları çok sayıdadır (Zamanlanmış
// görevler, hizmetler, WMI tetikleyicileri, kabuk klasörleri). TENGRI
// yalnızca oturum açıldığında doğrudan ÇALIŞAN kodu listeleyebilen yolları
// yönetir; dolaylı yollar (GPO, betik, hizmet tetikleyicisi) kapsam dışıdır.
//
//   Registry — devre dışı bırakılabilir:
//     HKCU/HKLM\...\Run, \RunOnce
//     HKCU/HKLM\...\Policies\Explorer\Run   (GPO'nun uyguladığı Run yolu)
//   Registry — YALNIZCA GÖSTERİLİR:
//     HKCU/HKLM\...\Winlogon                (Shell, Userinit, Taskman, ...)
//   Dosya sistemi — devre dışı bırakılabilir:
//     %APPDATA%\...\Start Menu\Programs\Startup
//     %PROGRAMDATA%\...\Start Menu\Programs\Startup
//
// Winlogon neden yönetilmiyor: Shell ve Userinit değerleri oturum açmanın
// kendisidir. Userinit'in devre dışı bırakılması Windows'un bu değeri
// kullanıcı oturumunda yeniden yazmasıyla "düzelir", ama Shell'in boşalması
// oturumun açılmamasına yol açar — düzeltilebilmesi de başka bir oturum
// açmayı gerektirir. Bu yüzden Winlogon girdileri listelenir, standart
// değerden sapmaları işaretlenir, ancak düğmeleri kapalı gelir. Bir
// yarım kalmış temizlikten iyidir.
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
        UserRun = 0,        // HKCU Run
        UserRunOnce = 1,
        MachineRun = 2,     // HKLM Run — yazma UAC ister
        MachineRunOnce = 3,
        PolicyExplorerRun = 4,      // HKCU ...\Policies\Explorer\Run
        PolicyExplorerRunMachine = 5,// HKLM ...\Policies\Explorer\Run
        StartupFolderUser = 6,      // %APPDATA%\...\Startup
        StartupFolderCommon = 7,    // %PROGRAMDATA%\...\Startup

        WinlogonUser = 8,           // HKCU ...\Winlogon — salt-okunur
        WinlogonMachine = 9,        // HKLM ...\Winlogon — salt-okunur

        Count = 10,
    };

    // Bir kapsamın devre dışı bırakılıp bırakılamayacağı. Winlogon için false:
    // Shell/Userinit oturumun kendisidir, kapatmak oturumu açılmaz yapar.
    bool IsToggleable(Scope s);

    // HKLM yazma yetkisi isteyen kapsamlar. Arayüz, yetki yoksa bu girdileri
    // pasif gösterip nedenini söyler; işlem denemeden önce bilmek daha iyidir.
    bool NeedsElevation(Scope s);

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
        bool         abnormal;     // Winlogon/Winlogon dışı: standarttan sapan
                                   // değer (Shell/Userinit ele geçirilmiş olabilir)
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
