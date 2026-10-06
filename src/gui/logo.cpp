#include "logo.hpp"
#include "brand.hpp"
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
        bool                     g_tried = false;

        // Kaynaktaki PNG'yi kaynak sırasında BGRA olarak çözer.
        //
        // WIC kendi decoder'ını taşır, bu yüzden projeye üçüncü taraf bir görüntü
        // kitaplığı eklemeye gerek kalmaz. Sonuç üst satırdan başlayan sıralı
        // piksel verisidir; DX11'in istediği biçimle birebir aynıdır.
        bool DecodePng(std::vector<BYTE>* out, UINT* outW, UINT* outH)
        {
            out->clear();
            *outW = *outH = 0;

            HRSRC res = ::FindResourceW(nullptr, MAKEINTRESOURCEW(brand::kLogoResId), RT_RCDATA);
            HGLOBAL glob = res ? ::LoadResource(nullptr, res) : nullptr;
            if (!glob) return false;

            const SIZE_T bytes = ::SizeofResource(nullptr, res);
            const BYTE*  data  = static_cast<const BYTE*>(::LockResource(glob));
            if (!data || bytes == 0) return false;

            IWICImagingFactory*     factory = nullptr;
            IWICStream*             stream  = nullptr;
            IWICBitmapDecoder*      decoder = nullptr;
            IWICBitmapFrameDecode*  frame   = nullptr;
            IWICFormatConverter*    conv    = nullptr;
            // goto etiketi yalnızca ileri atladığı için, aradaki her yerel
            // değişken burada tanımlanmalı; yoksa etiket geçişi onları atlar.
            std::vector<BYTE> pixels;
            UINT w = 0, h = 0;
            bool ok = false;

            auto cleanup = [&] {
                if (conv)    conv->Release();
                if (frame)   frame->Release();
                if (decoder) decoder->Release();
                if (stream)  stream->Release();
                if (factory) factory->Release();
            };

            if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&factory))))               goto done;
            if (FAILED(factory->CreateStream(&stream)))                        goto done;
            if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data),
                                                    static_cast<DWORD>(bytes)))) goto done;
            if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                        &decoder)))          goto done;
            if (FAILED(decoder->GetFrame(0, &frame)))                         goto done;

            // wincodec.h dönüştürücü türlerini GUID olarak vermez, elle kurulur.
            if (FAILED(factory->CreateFormatConverter(&conv)))                 goto done;
            if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                        nullptr, 0.0, WICBitmapPaletteTypeCustom))) goto done;

            UINT w2 = 0, h2 = 0;
            if (FAILED(conv->GetSize(&w2, &h2)) || w2 == 0 || h2 == 0)        goto done;
            w = w2; h = h2;

            pixels.resize(static_cast<SIZE_T>(w) * h * 4);
            if (FAILED(conv->CopyPixels(nullptr, w * 4, w * 4, pixels.data()))) goto done;

            out->swap(pixels);
            *outW = w;
            *outH = h;
            ok = true;

        done:
            cleanup();
            return ok;
        }
    }

    bool Init(ID3D11Device* device)
    {
        // Başarısızlık da kalıcıdır: her karede yeniden denemek, eksik bir kaynağın
        // her saniyede bir kez hata üretmesi demek. Bir kez sorup bırakılır.
        if (g_tried) return g_tex != nullptr;
        g_tried = true;
        if (!device) return false;

        std::vector<BYTE> pixels;
        UINT w = 0, h = 0;
        if (!DecodePng(&pixels, &w, &h)) return false;

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
        if (FAILED(device->CreateTexture2D(&td, &init, &tex2d))) return false;

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
        if (FAILED(device->CreateSamplerState(&sd, &samp))) { tex2d->Release(); return false; }

        const HRESULT hr = device->CreateShaderResourceView(tex2d, nullptr, &g_tex);
        samp->Release();
        tex2d->Release();
        return SUCCEEDED(hr);
    }

    void Shutdown()
    {
        if (g_tex) { g_tex->Release(); g_tex = nullptr; }
        g_tried = false;
    }

    bool Ready() { return g_tex != nullptr; }
    ID3D11ShaderResourceView* Tex() { return g_tex; }

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
}