#pragma once

// Hakkında sayfasındaki marka işaretlerinin üçgen verisi.
//
// tools/make_brand_icons.ps1 tarafından üretilir; elle düzenlenmemelidir.
//
// Koordinatlar -1..1 aralığında, işaretin merkezine göre konumlanmış float çiftleridir,
// böylece doğrudan icons::Draw'ın normalleştirilmiş P(x, y) yardımcısına takılır.
namespace brandicons
{
    // Sayılar üreticiden gelir, çünkü diziler burada extern bildiriliyor ve eksik türlü
    // bir dizi üzerinde sizeof derlenmiyor.
    extern const int kGitHubPointCount;
    extern const int kGitHubTriCount;

    extern const float         kGitHubPoints[];
    extern const unsigned short kGitHubTris[];

    // Aynı işaretin kenar yumuşatmalı alpha maskesi (PNG, gömülü). Üçgenler yedek olarak
    // duruyor: doku başlatılamazsa işaret yine basamaklı da olsa görünüyor.
    extern const int kGitHubMarkPngSize;
    extern const unsigned char kGitHubMarkPng[];
}