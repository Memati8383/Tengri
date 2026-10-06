#include "theme.hpp"
#include <windows.h>
#include <string>

namespace theme
{
    float scale = 1.0f;
    Fonts fonts;

    static ImFont* LoadSystemFont(const char* file, float size)
    {
        char dir[MAX_PATH] = {};
        GetWindowsDirectoryA(dir, MAX_PATH);
        const std::string path = std::string(dir) + "\\Fonts\\" + file;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return nullptr;
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), size);
    }

    static void ApplyStyle()
    {
        ImGuiStyle& s = ImGui::GetStyle();

        s.WindowPadding     = ImVec2(0, 0);
        s.WindowRounding    = 0.0f;
        s.WindowBorderSize  = 0.0f;
        s.ChildRounding     = 12.0f;
        s.ChildBorderSize   = 0.0f;
        s.PopupRounding     = 10.0f;
        s.PopupBorderSize   = 1.0f;
        s.FrameRounding     = 8.0f;
        s.FrameBorderSize   = 0.0f;
        s.FramePadding      = ImVec2(10, 8);
        s.ItemSpacing       = ImVec2(10, 10);
        s.ItemInnerSpacing  = ImVec2(8, 6);
        s.ScrollbarSize     = 6.0f;
        s.ScrollbarRounding = 8.0f;
        s.GrabRounding      = 8.0f;
        s.DisabledAlpha     = 0.38f;
        s.AntiAliasedLines  = true;
        s.AntiAliasedFill   = true;

        ImVec4* c = s.Colors;
        auto W = [](float a) { return ImVec4(1, 1, 1, a); };
        auto G = [](float v, float a = 1.0f) { return ImVec4(v, v, v, a); };

        c[ImGuiCol_Text]                 = G(0.95f);
        c[ImGuiCol_TextDisabled]         = G(0.40f);
        c[ImGuiCol_WindowBg]             = G(0.025f);
        c[ImGuiCol_ChildBg]              = W(0.0f);
        c[ImGuiCol_PopupBg]              = G(0.06f, 0.98f);
        c[ImGuiCol_Border]               = W(0.08f);
        c[ImGuiCol_BorderShadow]         = W(0.0f);
        c[ImGuiCol_FrameBg]              = W(0.04f);
        c[ImGuiCol_FrameBgHovered]       = W(0.06f);
        c[ImGuiCol_FrameBgActive]        = W(0.08f);
        c[ImGuiCol_TitleBg]              = G(0.03f);
        c[ImGuiCol_TitleBgActive]        = G(0.05f);
        c[ImGuiCol_TitleBgCollapsed]     = G(0.03f);
        c[ImGuiCol_MenuBarBg]            = G(0.04f);
        c[ImGuiCol_ScrollbarBg]          = W(0.0f);
        c[ImGuiCol_ScrollbarGrab]        = W(0.10f);
        c[ImGuiCol_ScrollbarGrabHovered] = W(0.18f);
        c[ImGuiCol_ScrollbarGrabActive]  = W(0.26f);
        c[ImGuiCol_CheckMark]            = G(0.97f);
        c[ImGuiCol_SliderGrab]           = G(0.97f);
        c[ImGuiCol_SliderGrabActive]     = W(1.0f);
        c[ImGuiCol_Button]               = W(0.05f);
        c[ImGuiCol_ButtonHovered]        = W(0.09f);
        c[ImGuiCol_ButtonActive]         = W(0.13f);
        c[ImGuiCol_Header]               = W(0.05f);
        c[ImGuiCol_HeaderHovered]        = W(0.08f);
        c[ImGuiCol_HeaderActive]         = W(0.11f);
        c[ImGuiCol_Separator]            = W(0.07f);
        c[ImGuiCol_SeparatorHovered]     = W(0.15f);
        c[ImGuiCol_SeparatorActive]      = W(0.25f);
        c[ImGuiCol_ResizeGrip]           = W(0.0f);
        c[ImGuiCol_ResizeGripHovered]    = W(0.0f);
        c[ImGuiCol_ResizeGripActive]     = W(0.0f);
        c[ImGuiCol_TextSelectedBg]       = W(0.22f);
        c[ImGuiCol_ModalWindowDimBg]     = G(0.0f, 0.6f);

        s.FontSizeBase = 15.0f;
        s.ScaleAllSizes(scale);
        s.FontScaleDpi = scale;
    }

    void Init(float dpi_scale)
    {
        scale = dpi_scale;

        fonts.regular = LoadSystemFont("segoeui.ttf", 15.0f);
        if (!fonts.regular)
            fonts.regular = ImGui::GetIO().Fonts->AddFontDefault();

        fonts.medium = LoadSystemFont("seguisb.ttf", 15.0f);
        if (!fonts.medium)
            fonts.medium = fonts.regular;

        fonts.bold = LoadSystemFont("segoeuib.ttf", 15.0f);
        if (!fonts.bold)
            fonts.bold = fonts.medium;

        ApplyStyle();
    }

    ImU32 White(float a)          { return ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, a)); }
    ImU32 Black(float a)          { return ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, a)); }
    ImU32 Gray(float v, float a)  { return ImGui::GetColorU32(ImVec4(v, v, v, a)); }
    // Alfa verilmediği yerde tam opak gri: çağıranların çoğu yalnızca parlaklık seviyesi
    // geçirip opaklığı stil alfasından bekliyor.
    ImU32 Gray(float v)           { return Gray(v, 1.0f); }
}
