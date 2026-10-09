#include "logo.hpp"
#include "logo_data.hpp"
#include "brand_icons.hpp"
#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "ole32.lib")   // CoCreateInstance (WIC factory)

namespace logo
{
    namespace
    {
        ID3D11ShaderResourceView* g_tex   = nullptr;
        ID3D11ShaderResourceView* g_mark  = nullptr;
        bool                     g_tried = false;

        // COM'yi bu dosya başlatmışsa kapatmak da bu dosyanın işidir. Uygulamanın geri
        // kalanı COM kullanmıyor, bu yüzden sahiplik bayrağı tutulur: bir başkası
        // zaten başlatmışsa bizim kapatmamız o işi bozar.
        bool g_comOwned = false;

        void EnsureCom()
        {
            if (g_comOwned) return;
            // RPC_E_CHANGED_MODE: başka biri COM'u farklı bir apartment modeliyle
            // başlatmış. Bu bizim hatamız değil; WIC yine de çalışır, yalnızca
            // şu çağrı S_FALSE döner ve bu da "zaten başlatılmış" demektir.
            g_comOwned = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                            COINIT_DISABLE_OLE1DDE));
        }

        void ReleaseCom()
        {
            if (!g_comOwned) return;
            ::CoUninitialize();
            g_comOwned = false;
        }

        // Gömülü PNG'yi kaynak sırasında BGRA olarak çözer.
        //
        // WIC kendi decoder'ını taşır, bu yüzden projeye üçüncü taraf bir görüntü
        // kitaplığı eklemeye gerek kalmaz. Sonuç üst satırdan başlayan sıralı
        // piksel verisidir; DX11'in istediği biçimle birebir aynıdır.
        bool DecodePng(const BYTE* data, size_t size, std::vector<BYTE>* out, UINT* outW, UINT* outH)
        {
            out->clear();
            *outW = *outH = 0;

            IWICImagingFactory*        factory = nullptr;
            IWICStream*                stream  = nullptr;
            IWICBitmapDecoder*         decoder = nullptr;
            IWICBitmapFrameDecode*     frame   = nullptr;
            IWICFormatConverter*       conv    = nullptr;
            std::vector<BYTE>          pixels;
            HRESULT hr = S_OK;
            bool ok = false;

            auto cleanup = [&] {
                if (conv)    conv->Release();
                if (frame)   frame->Release();
                if (decoder) decoder->Release();
                if (stream)  stream->Release();
                if (factory) factory->Release();
            };

            // Veriyi kopyalamadan WIC'e açmak için sabit bir arabelleğe ihtiyaç
            // var: InitializeFromMemory işaretçi tutar ve baytların hayatta kalmasını
            // ister. Baytlar statik olduğu için bu güvenli.
            if (FAILED(hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&factory))))               goto done;
            if (FAILED(hr = factory->CreateStream(&stream)))                   goto done;
            if (FAILED(hr = stream->InitializeFromMemory(const_cast<unsigned char*>(data),
                                                         static_cast<UINT>(size)))) goto done;
            if (FAILED(hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                             &decoder)))        goto done;
            if (FAILED(hr = decoder->GetFrame(0, &frame)))                     goto done;

            // wincodec.h dönüştürücü türlerini GUID olarak vermez, elle kurulur.
            if (FAILED(hr = factory->CreateFormatConverter(&conv)))            goto done;
            if (FAILED(hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                              nullptr, 0.0, WICBitmapPaletteTypeCustom))) goto done;

            UINT w = 0, h = 0;
            if (FAILED(hr = conv->GetSize(&w, &h)) || w == 0 || h == 0)       goto done;

            const UINT stride = w * 4;
            pixels.resize(static_cast<SIZE_T>(stride) * h);

            // cbStride satır adımı, cbBufferSize ise arabelleğin TOPLAM boyutudur;
            // ikisi karıştırıldığında CopyPixels WINCODEC_ERR_INVALIDPARAMETER verir.
            if (FAILED(hr = conv->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()),
                                            pixels.data())))                    goto done;

            out->swap(pixels);
            *outW = w;
            *outH = h;
            ok = true;

        done:
            cleanup();
            return ok;
        }

        // Gömülü PNG'yi DX11 dokusuna çevirir; başarısızsa nullptr.
        ID3D11ShaderResourceView* CreateTexture(ID3D11Device* device, const BYTE* png, size_t n)
        {
            std::vector<BYTE> pixels;
            UINT w = 0, h = 0;
            if (!DecodePng(png, n, &pixels, &w, &h)) return nullptr;

            D3D11_TEXTURE2D_DESC td = {};
            td.Width           = w;
            td.Height          = h;
            td.MipLevels       = 1;
            td.ArraySize       = 1;
            td.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage           = D3D11_USAGE_DEFAULT;
            td.BindFlags       = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem     = pixels.data();
            init.SysMemPitch = w * 4;

            ID3D11Texture2D* tex2d = nullptr;
            HRESULT hr = device->CreateTexture2D(&td, &init, &tex2d);
            if (FAILED(hr)) return nullptr;

            // Varsayılan nokta örneklemesi 16px'te kurtu gözle okunmaz hâle getiriyor;
            // doku küçültülürken doğrusal ara değer şart.
            D3D11_SAMPLER_DESC sd = {};
            sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MaxLOD = D3D11_FLOAT32_MAX;

            ID3D11SamplerState* samp = nullptr;
            if (FAILED(hr = device->CreateSamplerState(&sd, &samp))) { tex2d->Release(); return nullptr; }

            ID3D11ShaderResourceView* srv = nullptr;
            hr = device->CreateShaderResourceView(tex2d, nullptr, &srv);
            samp->Release();
            tex2d->Release();
            return SUCCEEDED(hr) ? srv : nullptr;
        }
    }

    bool Init(ID3D11Device* device)
    {
        // Başarısızlık da kalıcıdır: her karede yeniden denemek, eksik bir kaynağın
        // her saniyede bir kez hata üretmesi demek. Bir kez sorup bırakılır.
        if (g_tried) return g_tex != nullptr;
        g_tried = true;
        if (!device) return false;

        EnsureCom();   // CoCreateInstance bunu olmadan her zaman başarısız olur

        g_tex = CreateTexture(device, logodata::kLogoPng, logodata::kLogoPngSize);

        // Marka işareti logosuz da ayakta durabilir: ikisi ayrı ayrı denenir, böylece
        // birinin eksik kaynağı diğerini düşürmez.
        g_mark = CreateTexture(device, brandicons::kGitHubMarkPng,
                               static_cast<size_t>(brandicons::kGitHubMarkPngSize));
        return g_tex != nullptr;
    }

    void Shutdown()
    {
        if (g_tex)  { g_tex->Release();  g_tex  = nullptr; }
        if (g_mark) { g_mark->Release(); g_mark = nullptr; }
        ReleaseCom();
        g_tried = false;
    }

    bool Ready() { return g_tex != nullptr; }
    ID3D11ShaderResourceView* Tex() { return g_tex; }

    bool MarkReady() { return g_mark != nullptr; }

    void Draw(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 tint)
    {
        if (!g_tex) return;
        dl->AddImage(reinterpret_cast<ImTextureID>(g_tex), mn, mx, ImVec2(0, 0), ImVec2(1, 1), tint);
    }

    void DrawFitted(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float inset, ImU32 tint)
    {
        if (!g_tex) return;
        const float x0 = mn.x + inset, y0 = mn.y + inset;
        const float x1 = mx.x - inset, y1 = mx.y - inset;
        const float side = (x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0);
        if (side <= 0.0f) return;

        // Kırpma kutusu yerine kare kutu: doku zaten kare, dikdörtgen bir alana
        // germek onu yatay olarak uzatırdı.
        const float cx = (x0 + x1) * 0.5f;
        const float cy = (y0 + y1) * 0.5f;
        Draw(dl, ImVec2(cx - side * 0.5f, cy - side * 0.5f),
                ImVec2(cx + side * 0.5f, cy + side * 0.5f), tint);
    }

    void DrawMark(ImDrawList* dl, const ImVec2& c, float s, ImU32 tint)
    {
        if (!g_mark) return;
        const float h = s * 0.5f;
        dl->AddImage(reinterpret_cast<ImTextureID>(g_mark), ImVec2(c.x - h, c.y - h),
                     ImVec2(c.x + h, c.y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
    }
}
