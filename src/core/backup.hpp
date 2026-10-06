#pragma once

// Uygulama öncesi registry yedeği.
//
// Bu araç HKLM'ye yazıyor ve "kapat" işlemi kullanıcının eski değerini değil kodlanmış
// varsayılanı geri yükler. Yani geri dönüş, gerçek bir geri alma değil. Kullanıcının
// özelleştirdiği bir değer varsa kapatınca kaybolur.
//
// Buradaki çözüm: dokunulacak anahtarlar uygulamadan hemen önce reg.exe ile disa
// aktarılır, ve aynı anahtarlar "geri al" ile geri içe aktarılabilir. Bu, varsayılanı
// geri yükleyen geri almayı gerçek bir geri almaya dönüştürür.
//
// Yedekleme için yönetici yetkisi GEREKMEZ (reg export yalnızca okur). Geri alma
// gerektirir (reg import yazar), o yüzden geri alma düğmesi de yükseltilmiş sürece
// devredilir.

#include <string>
#include <vector>

namespace backup
{
    // Uygulanacak kategorilerin dokunacağı tüm registry anahtarlarını dışa aktarır.
    // `cats` boşsa hiçbir şey yapılmaz. Başarılıysa yedek klasörünü `outDir`'a yazar.
    //
    // Anahtar listesi tweaks.cpp ve regpack.cpp'den üretilen tweak_keys.h'den gelir;
    // elle bir liste tutulsaydı yedek kapsamı ayarların yazdığı yerlerden ayrılabilirdi.
    bool ExportForCategories(const int* cats, int count, std::wstring* outDir);

    // Mevcut yedekleri yeniden eskiden yeniye doğru sıralar (dizin adı zaman damgasıdır).
    std::vector<std::wstring> List();

    // En son yedek; yoksa boş dize.
    std::wstring Latest();

    // Verilen yedek klasöründeki tüm .reg dosyalarını içe aktarır. Dizin
    // kullanıcı verisinden geldiği için içindeki dosyalar sadece *.reg olarak alınır.
    bool Restore(const std::wstring& dir);

    // Yedeklerin tutulduğu kök klasör.
    std::wstring RootDirectory();
}