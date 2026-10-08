#pragma once

// Yetki yükseltme ve "yazan işi yükseltilmiş sürece devretme".
//
// Manifest asInvoker olduğu için uygulama normal kullanıcı olarak açılır. Registry
// yazan hiçbir iş o yetkiyle yapılamaz (HKLM'ye yazmak yönetici ister), dolayısıyla
// yazan bir iş çalıştırılmadan önce süreç kendini "runas" ile yeniden başlatır, aynı
// işi yüksek yetkiyle yapar ve çıkar. Kullanıcı UAC sorusunu bir kez görür, sonra
// ayarının uygulanmış olduğunu görür.
//
// Neden ayrı bir yardımcı exe değil: yardımcı exe ikinci bir uygulama demek —
// ayrı derleme, ayrı sürüm, ayrı ikon, ayrı antivirüs tetiklemesi. Aynı exe'yi
// yeniden başlatmak hepsini ortadan kaldırır; tek taşınan varlık exe'nin kendisidir.
//
// Uygulama normal açılışta sistem bilgisini okur, temizlik taraması yapar, registry
// değerlerini listeler — hiçbiri yetki istemez. Yalnızca bir şey yazılacağı anda UAC
// çıkar.

#include <string>
#include <vector>

namespace elevate
{
    // Bekleyen yazma işleri. Arayüz bunları doldurur, IsElevated() doğruysa
    // hemen uygulanır; değilse komut satırına kodlanıp yeniden başlatmaya verilir.
    struct Pending
    {
        // categories[c] içindeki satırlar için istenen durum (true = açık).
        // Boyutlar lang.hpp'teki kTweakCatCount ile eşleşmeli; komut satırı
        // kodlaması da aynı sınırı taşıyor.
        static constexpr int kMaxCats = 16;
        bool                 categories[kMaxCats][8] = {};
        bool                 anyChange[kMaxCats]    = {};   // kategori başına en az bir değişiklik
        int                  ramPreset = -1;         // >= 0 ise RAM profili uygula
        int                  dnsProvider = -1;       // >= 0 ise DNS ayarla
        int                  startupToggle = -1;     // 0 = kapat, 1 = aç, -1 = dokunma

        // Boş değilse ayarlar uygulanmaz, bunun yerine bu yedek geri yüklenir.
        // Yalnızca klasör ADI taşınır (tarih damgası, ASCII); tam yol yükseltilmiş
        // süreçte yeniden kurulur, böylece komut satırına kullanıcıdan gelen bir
        // yol sızmaz.
        std::string          restoreFrom;
    };

    void  CacheExecutablePath();
    bool  IsElevated();

    // Pending işleri uygular. Yükseltilmiş değilse exe'yi "runas" ile yeniden
    // başlatıp işi ona devreder ve true döner; çağıran bunu görünce kendi penceresini
    // kapatıp çıkmalıdır (DevrettimMi sarmalayıcısı bunu yapar). Kullanıcı UAC'i
    // reddederse false döner ve iş uygulanmadan çağırana döner.
    //
    // Geri dönüş değeri:
    //   IsInherited  -- süreç yükseltilmiş değil, iş devredildi (çağıran çıkmalı)
    //   Applied      -- iş bu süreçte uygulandı, sonuç çağıranın kendi bildiriminde
    //   Failed       -- uygulanamadı (yeti / hata), çağıran bildirim gösterebilir
    enum class Result { IsInherited, Applied, Failed };
    Result ApplyOrDelegate(const Pending& p);

    // Yükseltilmiş başlatmanın komut satırını kodlar. Test edilebilirlik için dışa
    // açıktır; normalde ApplyOrDelegate içinden çağrılır.
    std::wstring EncodeCommandLine(const Pending& p);

    // Komut satırından Pending'i çözer. --apply= yoksa false döner.
    bool DecodeCommandLine(int argc, wchar_t** argv, Pending& out);
}