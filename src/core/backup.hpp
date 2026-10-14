#pragma once

// Uygulama öncesi registry yedeği.
//
// Bu araç HKLM'ye yazıyor ve "kapat" işlemi kullanıcının eski değerini değil kodlanmış
// varsayılanı geri yükler. Yani geri dönüş, gerçek bir geri alma değil. Kullanıcının
// özelleştirdiği bir değer varsa kapatınca kaybolur.
//
// Buradaki çözüm: dokunulacak (anahtar, değer) çiftlerinin o anki baytları uygulamadan
// hemen önce okunup kendi biçimimizde bir .reg dosyasına yazılır; "geri al" o dosyayı
// çözüp RegSetValueExW ile geri yazar. reg.exe iki tarafta da kullanılmıyor.
//
// Kapsam değerin ADINA kadar iniyor ve geri yükleme sırasında üretilmiş tabloya
// (tweak_keys.h) karşı denetleniyor. Anahtar adına kadar daraltmak yetmezdi: yükseltme
// anahtarda değil değer adında duruyor — HKLM\SYSTEM\CurrentControlSet\Control meşru bir
// yedek anahtarıdır ama içindeki Session Manager\BootExecute SYSTEM kodkoşumuydu.
//
// Yedekleme için yönetici yetkisi GEREKMEZ (değer okuma yalnızca KEY_READ ister). Geri
// alma yazar, o yüzden geri alma düğmesi de yükseltilmiş sürece devredilir.

#include <string>
#include <vector>

namespace backup
{
    // Uygulanacak kategorilerin dokunacağı tüm (anahtar,değer) çiftlerinin mevcut
    // değerlerini dışa aktarır. `cats` boşsa hiçbir şey yapılmaz. Başarılıysa yedek
    // klasörünü `outDir`'a yazar.
    //
    // Anahtar/değer listesi tweaks.cpp ve regpack.cpp'den üretilen tweak_keys.h'den
    // gelir; elle bir liste tutulsaydı yedek kapsamı ayarların yazdığı yerlerden
    // ayrılabilirdi.
    bool ExportForCategories(const int* cats, int count, std::wstring* outDir);

    // Seçili ayarların (kategori, sıra) dokunduğu (anahtar,değer) çiftlerinin
    // CANLI değerlerini TEK bir .reg dosyasına yazar: profil dışa aktarımı.
    // Yalnızca OKUMA yapar, yetki istemez.
    //
    // Aynı çifti birkaç ayar sahiplenebildiği için (bkz. tweaks::IsShared)
    // kayıtlar tekil yazılır. Tabloda kaydı olmayan ayar — güç planı, hizmet
    // durumu gibi komut aracıyla değişenler — dosyaya giremez; sayısı
    // `toolOut`'ta döner, çünkü kullanıcıya "profil hepsini taşıdı" denemez.
    bool ExportPairs(const std::vector<std::pair<int, int>>& sel,
                     const std::wstring& file, int* pairsOut, int* toolOut);

    // Bir yedek klasörünün arayüzde gösterilebilmesi için gerekenler.
    struct Snapshot
    {
        std::wstring dir;          // tam yol
        std::wstring leaf;         // zaman damgası klasör adı
        std::vector<int> cats;     // içinde yedeği bulunan kategoriler (artan)
    };

    // Yedek klasörlerini yeniden eskige dogru listeler. Bicimi bozuk adlar
    // (Latest'in de reddettigi) buraya da girmez: arayuz secilemeyen bir yedegi
    // "var" diye gosteremez.
    std::vector<Snapshot> Scan();

    // Mevcut yedekleri yeniden eskiden yeniye doğru sıralar (dizin adı zaman damgasıdır).
    std::vector<std::wstring> List();

    // En son yedek; yoksa boş dize.
    std::wstring Latest();

    // Verilen yedek klasöründeki dosyaları çözüp registry'ye uygular. Bir dosya ancak
    // şu üç koşulu birden sağlarsa uygulanır: adı üretici tablosundaki gerçek bir
    // (kategori,sıra) çiftine karşılık gelmeli, içeriği kendi biçimimizde okunabilmeli,
    // ve İÇİNDEKİ HER (anahtar,değer) kaydı o çiftin izin listesinde bulunmalı. Bir
    // kayıt bile liste dışıysa dosyanın tamamı reddedilir.
    //
    // Klasör kullanıcı yetkisiyle yazılabilir bir yerde durduğu için bu denetim
    // zorunlu: oraya bırakılmış yabancı bir dosya, yönetici yetkisiyle rastgele bir
    // HKLM anahtarına ya da değerine yazamıyor — en fazla uygulamanın kendisinin
    // yazdığı değerlere yazabiliyor.
    //
    // Doğrulanan baytlar bellekte tutulup oradan yazılır; dosya denetimden sonra bir
    // daha açılmaz. Denetimle yazma arasında aynı yolun ikinci kez açılmasına
    // dayalı bir pencere yok.
    //
    // Hiçbir dosya uygulanmazsa ya da bir kayıt yazılamazsa false döner.
    bool Restore(const std::wstring& dir);

    // Yedeklerin tutulduğu kök klasör.
    std::wstring RootDirectory();
}
