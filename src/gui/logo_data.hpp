#pragma once

// Arayüzde çizilen marka resminin gömülü hâli.
//
// tools\make_logo_data.ps1 tarafından üretilir; elle düzenlenmemelidir.
// PNG baytları burada yaşar, böylece çalışma anında kaynak araması, dosya okuma
// ya da çözücü kurulumu gerekmez.
namespace logodata
{
    // Sayılar üreticiden gelir; diziler extern bildirildiği için eksik türlü bir
    // dizi üzerinde sizeof derlenmez.
    extern const unsigned int  kLogoPngSize;
    extern const unsigned int  kLogoWidth;
    extern const unsigned int  kLogoHeight;
    extern const unsigned char kLogoPng[];
}