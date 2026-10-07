#pragma once
#include "imgui.h"

// Gövde yazı tipi: Inter'in bu uygulama için kırpılmış hâli.
//
// tools\make_font_data.ps1 tarafından üretilir; elle düzenlenmemelidir.
// Baytlar exe içinde yaşar, böylece yazı tipi dosyası yanında olmadan da arayüz
// doğru çizilir.
namespace fontdata
{
    // Uygulamanın çizebileceği kod noktaları. GetGlyphRangesDefault yalnızca
    // Latin-1'i kapsar; ğ, İ, ş gibi Türkçe harfler Latin Extended-A'da
    // (U+0100–U+017F) ve varsayılan aralığın dışında. Aralık burada tam
    // verildiği için bu karakterler ilk rasterlemeden itibaren hazırdır.
    extern const ImWchar kRanges[];

    // Sayılar üreticiden gelir; diziler extern bildirildiği için eksik türlü bir
    // dizi üzerinde sizeof derlenmez.
    extern const unsigned int  kFontRegularSize;
    extern const unsigned int  kFontMediumSize;
    extern const unsigned int  kFontBoldSize;

    extern const unsigned char kFontRegular[];
    extern const unsigned char kFontMedium[];
    extern const unsigned char kFontBold[];
}