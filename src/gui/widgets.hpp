#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "icons.hpp"

namespace ui
{
    enum class ButtonStyle { Primary, Secondary, Ghost };
    enum class Toast       { Success, Info, Warning, Error };

    // Animasyon durumu ---------------------------------------------------------
    // Hedefe üstel yumuşatma. Statik tablo yerine ImGui kimliğiyle saklanır, böylece
    // aynı düğme birden çok yerde kullanılabilir ve durumlar birbirine karışmaz.
    float  Anim(ImGuiID id, float target, float speed = 12.0f);
    void   AnimSet(ImGuiID id, float value);
    ImGuiID Key(ImGuiID id, const char* suffix);

    // Metin yardımcıları (boyut, ölçeklenmemiş font boyutudur) ---------------
    ImVec2 TextSize(ImFont* font, float size, const char* text, const char* end = nullptr);
    void   Text(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 col, const char* text, const char* end = nullptr);
    ImVec2 SpacedSize(ImFont* font, float size, const char* text, float spacing);
    void   TextSpaced(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 col, const char* text, float spacing);
    void   Label(ImFont* font, float size, float gray, const char* text, float alpha = 1.0f);
    void   SectionLabel(const char* text);

    // Yüzeyler ----------------------------------------------------------------
    void Card(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rounding = -1.0f, float hover = 0.0f);
    // Yüksekliği içeriğe göre ayarlanan, sıradan yerleşim bileşenleri barındıran kart.
    void BeginCard(const char* id, float width, const char* title = nullptr, const char* subtitle = nullptr);
    void EndCard();

    // Denetimler --------------------------------------------------------------
    bool Button(const char* label, const ImVec2& size, ButtonStyle style = ButtonStyle::Primary, Icon icon = Icon::None, bool loading = false);
    bool IconButton(const char* id, Icon icon, const ImVec2& size, float icon_size, bool danger = false);
    bool Tab(const char* label, Icon icon, bool selected, const ImVec2& size);
    bool ToggleCard(const char* label, const char* desc, bool* v, float width, bool card = true);
    bool CheckRow(const char* label, const char* desc, const char* right, bool* v, float width);
    bool InputField(const char* id, const char* hint, char* buf, size_t buf_size, Icon icon, bool* reveal, float width, int flags = 0);
    bool Slider(const char* label, float* v, float vmin, float vmax, const char* fmt, float width);
    bool SliderInt(const char* label, int* v, int vmin, int vmax, float width);
    bool Segmented(const char* id, const char* const* items, int count, int* current, float width);
    void ProgressBar(const char* id, float fraction, const ImVec2& size);

    // Yalnızca çizim parçaları -------------------------------------------------
    void DrawSwitch(ImDrawList* dl, const ImVec2& pos, float on, float hover);
    void Spinner(ImDrawList* dl, const ImVec2& center, float radius, float thickness, ImU32 col);
    void Ring(ImDrawList* dl, const ImVec2& center, float radius, float thickness, float fraction);
    void Graph(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const float* values, int count, float vmin, float vmax, float scroll);

    // Bildirimler -------------------------------------------------------------
    extern bool notificationsEnabled;
    void Notify(Toast type, const char* title, const char* message, float duration = 3.6f);
    void RenderNotifications(const ImVec2& display_size);
}