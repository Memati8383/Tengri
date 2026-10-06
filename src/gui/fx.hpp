#pragma once
#include "imgui.h"

// Arka plan efektlerinin genel ayarları. Uygulama bunları doğrudan yazar, çizim katmanı
// okur; bu yüzden tek bir tanım ve tek bir gerçek vardır.
struct Settings
{
    int   count     = 90;
    float speed     = 1.0f;
    bool  particles = true;
    bool  lines     = true;
    bool  glow      = true;
    bool  sweep     = true;
    bool  mouse     = true;
};

// Düşen parımcıklar, yıldız geçişi çizgileri, yukarıdan gelen ışık ve periyodik
// yukarıdan aşağı ışık süpürmesi.
namespace fx
{
    extern Settings settings;

    // Sahne. alpha = genel solma (pencere açılış/kapanış).
    void DrawBackground(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float alpha);

    // İlkel çizim parçaları
    void RadialGradient(ImDrawList* dl, const ImVec2& c, float rx, float ry, ImU32 inner, ImU32 outer, int seg);
    void GradientQuad(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d,
                      ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd);
    void Shine(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float t, ImU32 col);
    void DrawSweep(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float alpha);
}