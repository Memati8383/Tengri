#pragma once
#include "imgui.h"

// Monokrom (siyah / beyaz) tasarım belirteçleri, yazı tipi takımı ve tipografi
// ölçeği.
namespace theme
{
    extern float scale;                       // monitör DPI ölçeği
    inline float  px(float v)          { return v * scale; }
    inline ImVec2 px(float x, float y) { return ImVec2(x * scale, y * scale); }

    // ---- yazı tipi ------------------------------------------------------------
    // Inter'in bu uygulama için kırpılmış hâli: Latin + Türkçe + noktalama.
    // Gömülü gelir (src\gui\font_data.cpp), yanında dosya aramaz.
    struct Fonts
    {
        ImFont* regular = nullptr;
        ImFont* medium  = nullptr;   // SemiBold
        ImFont* bold    = nullptr;
    };
    extern Fonts fonts;

    // ---- tipografi ölçeği ------------------------------------------------------
    // Nokta boyutları (DPI ölçeği uygulanmadan önceki değerler). Kaç farklı punto
    // olduğu hatırlanamayacağı için burada tek liste var; çağrı yerleri sayı
    // yazmaz, rol yazar. Yarım punto adımları özellikle kullanılmaz: "12.5 mi
    // 13 mü" ayrımı kimse tutamaz ve her yeni eklemede yeni bir değer doğar.
    namespace size
    {
        constexpr float Micro     =  8.5f;   // büyük harf eyebrow etiketleri (MENÜ)
        constexpr float Meta      = 11.0f;   // alt başlık, zaman damgası, HWID
        constexpr float Caption   = 12.0f;   // ikincil açıklama, devre dışı
        constexpr float Body      = 12.5f;   // gövde metni, liste satırı
        constexpr float Label     = 14.0f;   // düğme, toggle, segment etiketi
        constexpr float Title     = 15.0f;   // kart başlığı, sidebar markası
        constexpr float Heading   = 18.0f;   // bölüm başlığı
        constexpr float PageTitle = 22.0f;   // sekme başlığı, büyük değer
        constexpr float Display   = 26.0f;   // giriş ekranı ana başlık
    }

    // Harf aralığı. İki değer yeter: büyük harfli mikro etiketler sıkışık,
    // display metni geniş. Piksel değeri, px() ile ölçeklenir.
    namespace track
    {
        constexpr float Micro = 1.2f;
        constexpr float Wide  = 3.0f;
    }

    // ---- metin renkleri --------------------------------------------------------
    // Kontrast jetonları. Arka plan koyu (yaklaşık rgb 0.04), WCAG oranı:
    //   0.40 -> 3.45:1   0.45 -> 4.16:1   0.48 -> 4.64:1   0.52 -> 5.33:1
    // 18pt altı her şey 4.5:1 istir; 0.48'in altı bilgi metni için yetersiz.
    namespace ink
    {
        constexpr float Primary   = 0.96f;   // başlık, değer, vurgu
        constexpr float Secondary = 0.62f;   // gövde, açıklama
        constexpr float Tertiary  = 0.52f;   // meta, etiket, zaman damgası
        constexpr float Disabled  = 0.42f;   // placeholder, kapalı durum; bilgi değil
    }

    void Init(float dpi_scale);                // yazı tiplerini yükler ve ImGui stilini uygular

    // Bunların hepsi ImGui'nin o anki stil alfasına uyar, yani solma her yerde çalışır.
    ImU32 White(float a);
    ImU32 Gray(float v, float a);
    ImU32 Gray(float v);
    ImU32 Black(float a);
}