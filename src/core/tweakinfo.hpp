#pragma once

#include "regtext.hpp"

#include <wchar.h>

// Ayar tablosunun arayuz ve profil tarafindan okunan yuzu.
//
// Yedekleme (backup.cpp) tabloyu "hangi ciftlerin yazilabilecegi" olarak
// kullanir; arayuz ayni tabloyu "bu ayar neyi degistiriyor" olarak gosterir.
// Ikisi ayri liste olsaydi biri digerinden kopardi ve kullaniciya gösterilen
// kapsam ile gerçekten yazılan kapsam farklı olurdu.
namespace tweakinfo
{
    struct Item
    {
        const wchar_t* key;                 // tabloda yazan kisaltma ("HKLM\...")
        const wchar_t* const* valueNames;   // nullptr ile biten deger adi listesi
    };

    // Bir ayarin dokundugu anahtar kaydi sayisi. Sifir demek: ayar registry
    // degerlerini biz yazmiyoruz (guc plani, hizmet durumu, hazirlik komutu);
    // bu ayarlarin yedegi de alinamaz.
    int  Count(int cat, int idx);
    bool Get(int cat, int idx, int i, Item* out);

    // Bir deger adini kaç ayarin sahiplendigi. Paylasilan degerler (bkz.
    // tweaks::IsShared) profil ice aktariminda tek bir cifte birden çok ayarin
    // karsilik gelmesi demek; arayuz bunu soylemek zorunda.
    int OwnersOf(const wchar_t* expandedKey, const wchar_t* name,
                 int* cats, int* idxs, int max);

    // Tablodaki (kategori,sira) kaydi sayisi.
    int EntryCount();

    // Tablonun yazabilecegi HER (anahtar,deger) cifti. Yedek kapisi denetimi
    // tek bir ayarin listesine karsi yapiyor; oysa bir profilde hangi ayarin
    // bulunugu ancak dosya okunduktan sonra anlasiliyor, bu yuzden burada
    // bastan tum tabloya karsi denetleniyor. Arayuzde gorunen kapsam ile
    // yazilabilir kapsam ayni tablodan geldigi icin ikisi ayrisamiyor.
    void AllAllowed(std::vector<regtext::Allowed>* out);
}
