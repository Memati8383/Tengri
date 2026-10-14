#pragma once

// Uretilmis ayar tablosunu (build\obj\tweak_keys.h) tek noktadan include eder.
//
// Tablo derlemeden once tools\make_tweak_table.ps1 tarafindan tweaks.cpp ve
// regpack.cpp govdenlerinden cikarilir, yani "bu ayar hangi anahtara dokunur"
// sorusunun cevabi elle tutulan bir liste degildir. Turetilmedigi derlemelerde
// (tabloya ihtiyaci olmayan test paketleri) bos bir yedek devreye girer: sorgu
// hicbir sey dondurur, kapsamlari sinirlayan kapilar bu yuzden kapanmaz.

#ifdef TENGRI_HAS_TWEAK_KEYS
#include "tweak_keys.h"
#else
namespace tweakkeys
{
    struct Entry { int cat; int idx; const wchar_t* path;
                   const wchar_t* const* values; };
    static const Entry kEntries[1] = { { 0, 0, nullptr, nullptr } };
    static const int   kCount      = 0;
}
#endif
