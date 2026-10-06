#pragma once
#include "imgui.h"

// İleri bildirimler: çağıran yalnızca bir işaretçi geçirir, DX11 başlıklarını
// bu başlığa sürüklemek gereksiz. D3D11 bilgisi olmayan bir dosya (örneğin
// app.cpp) yalnızca Draw* çağırır.
struct ID3D11Device;
struct ID3D11ShaderResourceView;

// Arayüzde çizilen marka resmi.
//
// Resim exe içine RT_RCDATA olarak gömülür ve çalışma anında WIC ile çözülüp
// bir DX11 dokusuna çevrilir. Dosyayı ayrı taşımak yerine kaynak olarak
// bağlamak, exe tek başına kopyalandığında markanın kaybolmasını önler.
//
// Doku hazır değilse çağıran taraf kendi yedeğini çizmelidir: Init başarısız
// olursa Ready() false döner ve yüklenen her şey null'dır. Uygulama yalnızca
// pencere oluşturulduktan sonra, D3D11 ayakta olduğunda Init çağırır.
namespace logo
{
    // Cihaz üzerinden dokuyu hazırlar. Dönüş değeri başarıyı bildirir; başarısızsa
    // uygulama çizim yedeğine düşer, bu yüzden çağıran taraf hatayı gizlemekle
    // yükümlüdür. Bir kez başarılı olursa ikinci çağrı no-op'tur.
    bool Init(ID3D11Device* device);

    // Dokuyu serbest bırakır. Init hiç çağrılmamışsa güvenlidir.
    void Shutdown();

    bool Ready();                 // doku hazır mı
    ID3D11ShaderResourceView* Tex();

    // Duz dörtgen içine UV'siz olarak çizer. min/max ekran koordinatıdır.
    void Draw(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 tint = ~0u);

    // Bir dikdörtgenin içine, kenar boşluğu korunacak biçimde sığdırır.
    void DrawFitted(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float inset, ImU32 tint = ~0u);
}