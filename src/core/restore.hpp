#pragma once

// Toplu tweak uygulamadan ÖNCE Windows System Restore noktası oluşturur.
//
// Mevcut backup.cpp registry anahtarlarını .reg olarak dışa aktarıyor —
// tweak kapsamında geri alma mümkün. Ancak kullanıcı bütün Windows'u eski
// hâline döndürmek isterse (ör. sürücü sorunu, servis bozulması) bu yetmez.
// SRSetRestorePointW tam olarak bu durumu çözer: VSS üzerinden tüm sistem
// durumunun bir anlık görüntüsü alınır ve "Sistem Geri Yükleme" ekranından
// seçilebilir.
//
// Çağrı 10-30 saniye sürer (VSS yavaş); ÇAĞRIDAN ÖNCE arka iş parçacığına
// taşınmalıdır. Yetki gerektirir — mevcut elevate akışının içinde kullanılır.

namespace restore
{
    enum class Result
    {
        Created,            // başarılı — noktayı Windows kaydetti
        Disabled,           // Sistem Koruma kapalı (ör. SSD üstünde varsayılan)
        NotElevated,        // yönetici yetkisi yok
        ServiceUnavailable, // srclient.dll yok (bazı Server SKU'lar)
        Throttled,          // Windows 24 saat kuralı: son nokta yakın
        Failed              // genel hata (VSS kilidi, disk dolu vb.)
    };

    // description: Geri yükleme listesinde görünecek metin. 256 karakterden kısa.
    //              "TENGRI: <kategori>" biçimi tavsiye edilir.
    //
    // Çağrı yetki yoksa NotElevated döner — elevate yoluna sarılarak
    // kullanılmalıdır. Zaten yükseltilmiş süreçte uygulanırsa doğrudan çalışır.
    Result CreatePoint(const wchar_t* description);

    // Loglama ve bildirim için insan okunur metin. İngilizce — i18n yerine
    // sabit; nokta oluşturma dilden bağımsız bir sistem olayıdır ve
    // log/bildirim metni çevirilmemiş halde daha teşhis edilebilir.
    const wchar_t* ResultText(Result r);

    // C: için Sistem Koruma etkin mi? Etkin değilse CreatePoint sessizce
    // Disabled döner; UI önceden bunu okuyup kullanıcıyı uyarabilir.
    bool IsProtectionEnabled();

    // İsteğe bağlı: en son başarıyla oluşturulan noktanın zaman damgası.
    // 0 dönüşü "hiç oluşturulmamış". UI "Son nokta: 2 saat önce" göstermek
    // isterse.
    unsigned long long LastCreatedTick();
}
