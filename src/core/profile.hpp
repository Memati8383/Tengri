#pragma once

// Paylasilabilir ayar profili: disa aktarilmis .reg dosyasini okuyup hangi
// ayarlarin geri gelecegini soyleyen saf cozumleme.
//
// Profil, secili ayarlarin CANLI degerlerini tasiyan tek bir .reg dosyasidir;
// yazma isi backup::ExportPairs'te. Ayar adi listesi tasinmadi: bir ad listesi
// geri okundugunda "bu ayar hangi degeri yazmaliydi" sorusu surumden surume
// yeniden cevaplanmali olurdu, yani profil uygulama koduna bagimli kalirdi.
//
// Neden ayri bir modul: Ice aktarim iki farkli soruyu cevapliyor ve ikisi de
// yanlis kullanici mesaji uretebilir. (1) Dosya bizim bicimimizde mi? (2) Bizim
// bicimimizde ama tablonun yazmadigi anahtarlar mi iceriyor? Yedek yolunda bu
// ayrimi yapmaya gerek yoktu: dosya adi tek bir (kategori,sira) ciftine
// karsilik geliyor ve denetim o ciftin listesine karsi yapiliyor. Profilde
// hangi ayarin dosyada oldugu ancak icerik okununca anlasiliyor, bu yuzden
// denetim bastan tum tabloya karsi yapiliyor. Tablonun yazmadigi tek bir cift
// dosyayi tamamiyle reddediyor.
//
// Profil yalnizca ACIK ayarlari tasir. Ice aktarim bu yuzden yalnizca acar:
// dosyada bulunmayan bir ayar "kapali" demek degildir, o ayar hic yazilmamis
// olabilir ve kapali durmak o ayar icin hicbir sey yazmamakla ayni seydir.

#include <string>
#include <utility>
#include <vector>

namespace profile
{
    struct Analysis
    {
        // Dosya okundu ve baslik/satir bicimi bize ait. Degilse kullaniciya
        // "bu bir TENGRI profil dosyasi degil" denir; iceriğe hiç bakılmaz.
        bool parsed = false;

        // Bicim bizim ama tablonun hic yazmadigi bir (anahtar,deger) cifti
        // iceriyor: dosya ya baska bir aractan geldi ya da elle degistirildi.
        bool foreign = false;

        // Tum ciftleri dosyada bulunan ayarlar; arayuz bunlari acar.
        std::vector<std::pair<int, int>> on;

        // Ciftlerinin yalnizca bir kismi bulunan ayarlar; kapali kaliyor ve
        // arayuz sayisini soylemeli, yoksa kullanicinin profilde gordugu bir
        // ayar sessizce acilmamis olur.
        std::vector<std::pair<int, int>> partial;
    };

    // Dosyayi okur ve ayarlar icin karara cevirir. Okuma ya bicim hatasinda
    // false doner; foreign/parsed ayrintisi cagiranin mesaj secimi icin tutulur.
    bool Analyze(const std::wstring& file, Analysis* out);
}
