#pragma once

// Ada, uygulamanın cihazı üzerinde kendi swap zincirini açar; bu yüzden
// başlığa d3d11 gerekir. Sadece ileri bildirim yetmez: CreateSwapChainForHwnd
// çağrılıyor.
//
// NOMINMAX burada tanımlanmalı: d3d11.h -> windows.h bu başlıktan ÖNCE gelirse
// min/max makroları std::min/std::max'i bozar (clang-cl hatası C2589 verir).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>

// Masaüstünün üstünde yüzen medya adası.
//
// Kurgu
// -----
// Ada, uygulamanın kendi penceresinden BAĞIMSIZ bir üstte-görünür (topmost)
// penceredir. Yüzeyi şeffaftır, arkasından geçer ve fare dışındayken tıklamayı
// geçirir; yalnızca adanın üzerine gelindiğinde etkinleşir.
//
// Neden ayrı pencere: tek pencereye çizilen bir ada, uygulama penceresi
// taşındığında, küçültüldüğünde ya da tepsiye gizlendiğinde onunla birlikte
// kaybolur. Buradaki tek istisna masaüstü modudur (bkz. DesktopOnly).
//
// Neden kendi penceresi ama ORTAK ImGui bağlamı ve ORTAK D3D11 cihazı:
// uygulamanın kendi karesi çizilirken de ada kendi karesini çizmeye devam
// etmelidir; iki kare aynı iş parçacığında sırayla (NewFrame -> Render ->
// RenderDrawData -> Present) yürütülür.
//
// ÖNEMLİ (geçmişte burada iki hata vardı, ikisi de uygulamayı öldürüyordu):
//   1) Ada ikinci bir ImGui bağlamı (ImGui::CreateContext) kuruyordu. ImGui'de
//      "geçerli bağlam" TEK bir globaldir; CreateContext onu değiştirir. Böylece
//      ana pencerenin karesi adanın bağlamında çiziliyor, ana pencere
//      DisplaySize'ı adanın boyutundan alıyor ve ekran siyah kalıyordu.
//   2) Ada ikinci bir D3D11 CIHAZI kurup ImGui_ImplDX11_Init/ImGui_ImplWin32_Init
//     'i tekrar çağırıyordu. İki backend de TEK bir global durum bloğu tutar
//      (imgui_impl_dx11.cpp içindeki bd). İkinci Init o bloğu adanın cihazıyla
//      ezdiği için ana pencerenin vertex buffer'ları yanlış cihaza yazılıyor,
//      ada kapatıldığında Shutdown ana pencerenin kaynaklarını serbest
//      bırakıyor ve uygulama erişim ihlaliyle (0xC0000005) çöküyordu.
// Doğrusu: tek bağlam, tek cihaz, tek backend. Ada yalnızca kendi swap
// zincirini o cihaz üzerinde açar.
//
// Yalnızca SMTC (medya oturumu) bilgisi bir arka plan iş parçacığında toplanır;
// o iş parçacığı ImGui'ye hiç dokunmaz.
namespace island
{
    // ---- yaşam döngüsü ------------------------------------------------------

    // Uygulamanın D3D11 cihazı ve bağlamı. Ada kendi swap zincirini bu
    // cihazın üzerinde açar; CİHAZ SAHİPLİĞİ ADAYA AİT DEĞİLDİR, ada
    // serbest bırakmaz. main.cpp, CreateDeviceD3D'den hemen sonra çağırır.
    void SetSharedD3D(ID3D11Device* device, ID3D11DeviceContext* context);

    // Tercihleri okur. D3D/ImGui kurulumu burada yapılmaz; ilk karede, ada
    // açıksa kurulur. Ana iş parçacığından, uygulamanın kendi ImGui bağlamı
    // kurulduktan hemen sonra çağrılır.
    void Startup(float dpi_scale);

    // Pencereyi, kendi swap zincirini ve iş parçacıklarını serbest bırakır.
    // Paylaşılan ImGui bağlamına, cihaza ve backend'lere DOKUNMAZ; onlar
    // ana pencerenin sahibidir.
    void Shutdown();

    // Ana döngünün HER turunda çağrılır. Ada kapalıyken, kare atlanan her yolda
    // (örtülü pencere, tepsi) yine çağrılmalıdır; aksi hâlde donmuş görünür.
    // UYGULAMANIN KARESİNDEN SONRA çağrılmalı: ada kendi ImGui karesini açmaz
    // (açsaydı paylaşılan bağlamın pencere/ActiveId defterini bozup ana
    // pencerenin hiçbir yerini tıklanamaz yapardı), yazı tipi atlasını ana
    // pencerenin karesi yüklediği için adanın çizimi o karenin sonrasına
    // bırakılır.
    void Frame();

    // ---- tercihler ----------------------------------------------------------

    // Ayar sayfası bu değişkenlere doğrudan bağlanır; ada bir sonraki karede
    // yeni durumu görür. Enabled() kapalıp açıldığında pencere o an kurulur
    // ya da yıkılır, yeniden başlatma gerekmez.
    extern bool enabled;
    extern bool desktopOnly;
    extern bool lyrics;
    extern int  theme;

    // INSERT ile açılıp kapanır ve hatırlanır. enabled=false ise bu anahtar
    // ölüdür: pencere hiç kurulmaz.
    extern bool visible;

    // Tema sayısı ve adları. Ayarlar kartındaki seçici bunları okur.
    int         ThemeCount();
    const char* ThemeName(int index);

    // Değişiklikleri diske yazar. Ayar sayfası bir anahtarı değiştirdiği anda
    // çağırılır.
    void Save();
}