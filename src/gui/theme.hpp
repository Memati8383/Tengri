#pragma once
#include "imgui.h"

// Monokrom (siyah / beyaz) tasarım belirteçleri ve font takımı.
namespace theme
{
    extern float scale;                       // monitör DPI ölçeği
    inline float  px(float v)          { return v * scale; }
    inline ImVec2 px(float x, float y) { return ImVec2(x * scale, y * scale); }

    struct Fonts
    {
        ImFont* regular = nullptr;
        ImFont* medium  = nullptr;
        ImFont* bold    = nullptr;
    };

    extern Fonts fonts;
    void Init(float dpi_scale);                // fontları yükler ve ImGui stilini uygular

    // Bunların hepsi ImGui'nin o anki stil alfasına uyar, yani solma her yerde çalışır.
    ImU32 White(float a);
    ImU32 Gray(float v, float a);
    ImU32 Gray(float v);
    ImU32 Black(float a);
}