#include "widgets.hpp"
#include "theme.hpp"
#include "fx.hpp"
#include "imgui_internal.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>

using theme::px;
using theme::White;
using theme::Black;
using theme::Gray;

namespace ui
{
    // ======================================================================= animasyon

    static std::unordered_map<ImGuiID, float> g_anim;

    float Anim(ImGuiID id, float target, float speed)
    {
        auto it = g_anim.find(id);
        if (it == g_anim.end())
        {
            g_anim.emplace(id, target);
            return target;
        }
        float& v = it->second;
        const float k = 1.0f - expf(-speed * ImGui::GetIO().DeltaTime);
        v = ImLerp(v, target, k);
        if (fabsf(v - target) < 0.0005f)
            v = target;
        return v;
    }

    void AnimSet(ImGuiID id, float value) { g_anim[id] = value; }

    ImGuiID Key(ImGuiID id, const char* suffix) { return ImHashStr(suffix, 0, id); }

    // ======================================================================= metin

    ImVec2 TextSize(ImFont* font, float size, const char* text, const char* end)
    {
        return font->CalcTextSizeA(px(size), FLT_MAX, 0.0f, text, end);
    }

    void Text(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 col, const char* text, const char* end)
    {
        dl->AddText(font, px(size), ImVec2(floorf(pos.x), floorf(pos.y)), col, text, end);
    }

    // c ile başlayan UTF-8 dizisinin bayt sayısını verir. ASCII dışındaki harfler
    // (Türkçe ı/ü/ş/ğ/ö/ç de bunlara dâhil) 2 bayttır; bayt bayt ilerlenirse
    // gliflerin yarısı kendi başına bozuk karakter olarak çizilir.
    static int Utf8Len(unsigned char c)
    {
        if (c < 0x80) return 1;
        if ((c & 0xE0) == 0xC0) return 2;   // 110xxxxx
        if ((c & 0xF0) == 0xE0) return 3;   // 1110xxxx
        if ((c & 0xF8) == 0xF0) return 4;   // 11110xxx
        return 1;                          // devam baytı ya da geçersiz giriş: bir ilerle
    }

    ImVec2 SpacedSize(ImFont* font, float size, const char* text, float spacing)
    {
        float w = 0.0f, hgt = 0.0f;
        int n = 0;
        for (const char* c = text; *c; )
        {
            const char* next = c + Utf8Len((unsigned char)*c);
            const ImVec2 s = font->CalcTextSizeA(px(size), FLT_MAX, 0.0f, c, next);
            w += s.x;
            hgt = ImMax(hgt, s.y);
            ++n;
            c = next;
        }
        return ImVec2(w + spacing * (float)ImMax(0, n - 1), hgt);
    }

    void TextSpaced(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 col, const char* text, float spacing)
    {
        float x = floorf(pos.x);
        for (const char* c = text; *c; )
        {
            // Kod noktasının tamamı tek çağrıda çizilir, ardından aynı aralık ölçülür;
            // böylece ilerleme miktarı gerçekten çizilenle birebir tutar.
            const char* next = c + Utf8Len((unsigned char)*c);
            dl->AddText(font, px(size), ImVec2(x, floorf(pos.y)), col, c, next);
            x += font->CalcTextSizeA(px(size), FLT_MAX, 0.0f, c, next).x + spacing;
            c = next;
        }
    }

    void Label(ImFont* font, float size, float gray, const char* text, float alpha)
    {
        ImGui::PushFont(font, size);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(gray, gray, gray, alpha));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void SectionLabel(const char* text)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 sz  = SpacedSize(theme::fonts.medium,theme::size::Meta, text, px(theme::track::Micro));
        TextSpaced(dl, theme::fonts.medium, theme::size::Meta, pos, theme::ink::Tertiary,  text, px(theme::track::Micro));
        ImGui::Dummy(sz);
    }

    // ======================================================================= yüzeyler

    void Card(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rounding, float hover)
    {
        if (rounding < 0.0f)
            rounding = px(14);

        dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImVec4(0.045f, 0.045f, 0.05f, 0.80f + 0.06f * hover)), rounding);

        // üstten gelen ışık izlenimi veren iç parlama
        const float cx = (mn.x + mx.x) * 0.5f;
        dl->PushClipRect(mn, mx, true);
        fx::RadialGradient(dl, ImVec2(cx, mn.y), (mx.x - mn.x) * 0.48f, ImMin(px(70), (mx.y - mn.y) * 0.6f),
                           White(0.035f + 0.025f * hover), White(0.0f), 40);
        dl->PopClipRect();

        dl->AddRect(mn, mx, White(0.065f + 0.06f * hover), rounding, 0, ImMax(1.0f, px(1)));

        // üst kenarda parlak, ince çizgi; köşe yuvarlatmasına girsin diye kısaltıldı
        const float hw = (mx.x - mn.x) * 0.5f - rounding;
        if (hw > 0.0f)
        {
            const ImU32 c0 = White(0.0f), c1 = White(0.24f + 0.16f * hover);
            const float lh = ImMax(1.0f, px(1));
            dl->AddRectFilledMultiColor(ImVec2(cx - hw, mn.y), ImVec2(cx, mn.y + lh), c0, c1, c1, c0);
            dl->AddRectFilledMultiColor(ImVec2(cx, mn.y), ImVec2(cx + hw, mn.y + lh), c1, c0, c0, c1);
        }
    }

    void BeginCard(const char* id, float width, const char* title, const char* subtitle)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(10), px(6)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(18), px(16)));
        ImGui::BeginChild(id, ImVec2(width, 0.0f),
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(); // WindowPadding

        if (title)
        {
            Label(theme::fonts.bold,theme::size::Title, theme::ink::Primary, title);
            if (subtitle)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
                Label(theme::fonts.regular,theme::size::Body, theme::ink::Secondary, subtitle);
            }
            ImGui::Dummy(ImVec2(0, px(4)));
        }
    }

    void EndCard()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar(); // ItemSpacing
        // Üst pencerenin çizim listesi alt pencereden önce render edildiği için kart, içeriğinin
    // arkasında kalır.
        Card(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

    // ======================================================================= denetimler

    static const char* VisibleEnd(const char* label) { return ImGui::FindRenderedTextEnd(label); }

    bool Button(const char* label, const ImVec2& size, ButtonStyle style, Icon icon, bool loading)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID id  = win->GetID(label);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + size);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (loading)
            pressed = false;
        if (hovered && !loading)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        const float hv = Anim(Key(id, "h"), hovered ? 1.0f : 0.0f, 14.0f);
        const float pr = Anim(Key(id, "p"), held ? 1.0f : 0.0f, 22.0f);

        ImDrawList* dl = win->DrawList;
        ImRect rb = bb;
        rb.Expand(-pr * px(1.5f));
        const float r = px(10);

        ImU32 textCol = 0;
        if (style == ButtonStyle::Primary)
        {
            // imleç üzerindeyken genişleyen yumuşak dış parlama
            for (int i = 4; i >= 1; --i)
            {
                const float e = px(2.5f) * (float)i;
                dl->AddRectFilled(rb.Min - ImVec2(e, e), rb.Max + ImVec2(e, e), White(0.022f * hv), r + e);
            }
            dl->AddRectFilled(rb.Min, rb.Max, Gray(0.86f + 0.12f * hv), r);
            dl->AddRectFilledMultiColor(ImVec2(rb.Min.x + r, rb.Min.y), ImVec2(rb.Max.x - r, rb.Min.y + ImMax(1.0f, px(1))),
                                        White(0.9f), White(0.9f), White(0.0f), White(0.0f));
            const float st = fmodf((float)ImGui::GetTime() * 0.55f, 1.0f);
            fx::Shine(dl, rb.Min, rb.Max, st, Black(0.10f * hv));
            textCol = Gray(0.04f);
        }
        else if (style == ButtonStyle::Secondary)
        {
            dl->AddRectFilled(rb.Min, rb.Max, White(0.04f + 0.04f * hv), r);
            dl->AddRect(rb.Min, rb.Max, White(0.10f + 0.14f * hv), r, 0, ImMax(1.0f, px(1)));
            textCol = Gray(0.78f + 0.2f * hv);
        }
        else
        {
            if (hv > 0.0f)
                dl->AddRectFilled(rb.Min, rb.Max, White(0.05f * hv), r);
            textCol = Gray(0.6f + 0.35f * hv);
        }

        const ImVec2 center = rb.GetCenter();
        if (loading)
        {
            Spinner(dl, center, px(8), px(2), textCol);
            return false;
        }

        const char* end   = VisibleEnd(label);
        const ImVec2 ts   = TextSize(theme::fonts.medium,theme::size::Label, label, end);
        const float  isz  = icon != Icon::None ? px(15) : 0.0f;
        const float  gap  = icon != Icon::None && ts.x > 0 ? px(8) : 0.0f;
        const float  tw   = isz + gap + ts.x;
        float x = center.x - tw * 0.5f;
        if (icon != Icon::None)
        {
            icons::Draw(dl, icon, ImVec2(x + isz * 0.5f, center.y), isz, textCol, px(1.6f));
            x += isz + gap;
        }
        Text(dl, theme::fonts.medium,theme::size::Label, ImVec2(x, center.y - ts.y * 0.5f), textCol, label, end);
        return pressed;
    }

    bool IconButton(const char* str_id, Icon icon, const ImVec2& size, float icon_size, bool danger)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID id  = win->GetID(str_id);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + size);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        const float hv = Anim(Key(id, "h"), hovered ? 1.0f : 0.0f, 16.0f);

        ImDrawList* dl = win->DrawList;
        ImU32 col;
        if (danger)
        {
            dl->AddRectFilled(bb.Min, bb.Max, White(0.92f * hv), px(7));
            col = Gray(ImLerp(0.6f, 0.04f, hv));
        }
        else
        {
            dl->AddRectFilled(bb.Min, bb.Max, White(0.07f * hv), px(7));
            col = Gray(0.55f + 0.4f * hv);
        }
        icons::Draw(dl, icon, bb.GetCenter(), icon_size, col, px(1.5f));
        return pressed;
    }

    bool Tab(const char* label, Icon icon, bool selected, const ImVec2& size)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID id  = win->GetID(label);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + size);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        const float hv  = Anim(Key(id, "h"), hovered ? 1.0f : 0.0f, 14.0f);
        const float sel = Anim(Key(id, "s"), selected ? 1.0f : 0.0f, 12.0f);

        ImDrawList* dl = win->DrawList;
        if (hv > 0.0f)
            dl->AddRectFilled(bb.Min, bb.Max, White(0.03f * hv * (1.0f - sel)), px(9));

        const float v  = 0.46f + 0.5f * ImMax(sel, hv * 0.65f);
        const float cy = bb.GetCenter().y;
        icons::Draw(dl, icon, ImVec2(bb.Min.x + px(22), cy), px(16), Gray(v), px(1.5f));

        const char* end = VisibleEnd(label);
        const ImVec2 ts = TextSize(theme::fonts.medium,theme::size::Label, label, end);
        Text(dl, theme::fonts.medium,theme::size::Label, ImVec2(bb.Min.x + px(42) + sel * px(2), cy - ts.y * 0.5f), Gray(v), label, end);
        return pressed;
    }

    void DrawSwitch(ImDrawList* dl, const ImVec2& p, float on, float hover)
    {
        const float w = px(38), h = px(21), r = h * 0.5f;
        const ImVec2 mx = p + ImVec2(w, h);

        if (on > 0.01f)
            fx::RadialGradient(dl, ImVec2(p.x + w * 0.5f, p.y + r), w * 0.9f, h * 1.1f, White(0.10f * on), White(0.0f), 28);

        dl->AddRectFilled(p, mx, Gray(1.0f - 0.04f * on, ImLerp(0.08f + 0.05f * hover, 1.0f, on)), r);
        if (on < 0.99f)
            dl->AddRect(p, mx, White((0.10f + 0.08f * hover) * (1.0f - on)), r, 0, ImMax(1.0f, px(1)));

        const float kr = r - px(3);
        const ImVec2 kc(p.x + r + on * (w - 2.0f * r), p.y + r);
        dl->AddCircleFilled(kc, kr, Gray(ImLerp(0.72f + 0.15f * hover, 0.05f, on)), 24);
    }

    bool ToggleCard(const char* label, const char* desc, bool* v, float width, bool card)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID id  = win->GetID(label);
        const float   hgt = desc ? px(64) : px(44);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + ImVec2(width, hgt));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (pressed)
        {
            *v = !*v;
            ImGui::MarkItemEdited(id);
        }

        const float hv = Anim(Key(id, "h"), hovered ? 1.0f : 0.0f, 14.0f);
        const float on = Anim(Key(id, "on"), *v ? 1.0f : 0.0f, 14.0f);

        ImDrawList* dl = win->DrawList;
        const float rr = card ? px(11) : px(9);
        if (card)
        {
            dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.055f, 0.72f)), rr);
            dl->AddRectFilled(bb.Min, bb.Max, White(0.015f * hv + 0.015f * on), rr);
            dl->AddRect(bb.Min, bb.Max, White(0.06f + 0.06f * hv + 0.07f * on), rr, 0, ImMax(1.0f, px(1)));
        }
        else if (hv > 0.0f)
        {
            dl->AddRectFilled(bb.Min - ImVec2(px(8), 0), bb.Max + ImVec2(px(8), 0), White(0.025f * hv), rr);
        }

        const float padX    = card ? px(16) : 0.0f;
        const float switchX = bb.Max.x - padX - px(38);
        const float cy      = bb.GetCenter().y;

        dl->PushClipRect(bb.Min, ImVec2(switchX - px(10), bb.Max.y), true);
        const ImU32 titleCol = Gray(0.80f + 0.18f * ImMax(on, hv));
        const char* end = VisibleEnd(label);
        if (desc)
        {
            Text(dl, theme::fonts.medium,theme::size::Label, ImVec2(bb.Min.x + padX, bb.Min.y + px(12)), titleCol, label, end);
            Text(dl, theme::fonts.regular,theme::size::Body, ImVec2(bb.Min.x + padX, bb.Min.y + px(33)), Gray(0.48f), desc);
        }
        else
        {
            const ImVec2 ts = TextSize(theme::fonts.medium,theme::size::Label, label, end);
            Text(dl, theme::fonts.medium,theme::size::Label, ImVec2(bb.Min.x + padX, cy - ts.y * 0.5f), titleCol, label, end);
        }
        dl->PopClipRect();

        DrawSwitch(dl, ImVec2(switchX, cy - px(10.5f)), on, hv);
        return pressed;
    }

    bool CheckRow(const char* label, const char* desc, const char* right, bool* v, float width)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID id  = win->GetID(label);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + ImVec2(width, px(64)));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (pressed)
        {
            *v = !*v;
            ImGui::MarkItemEdited(id);
        }

        const float hv = Anim(Key(id, "h"), hovered ? 1.0f : 0.0f, 14.0f);
        const float on = Anim(Key(id, "on"), *v ? 1.0f : 0.0f, 16.0f);

        ImDrawList* dl = win->DrawList;
        const float rr = px(11);
        dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.055f, 0.72f)), rr);
        dl->AddRectFilled(bb.Min, bb.Max, White(0.018f * hv), rr);
        dl->AddRect(bb.Min, bb.Max, White(0.06f + 0.06f * hv + 0.05f * on), rr, 0, ImMax(1.0f, px(1)));

        // onay kutusu gövdesi
        const float  bs = px(18);
        const ImVec2 b0(bb.Min.x + px(16), bb.GetCenter().y - bs * 0.5f);
        const ImVec2 b1 = b0 + ImVec2(bs, bs);
        if (on > 0.01f)
            fx::RadialGradient(dl, (b0 + b1) * 0.5f, bs * 1.2f, bs * 1.2f, White(0.12f * on), White(0.0f), 20);
        dl->AddRectFilled(b0, b1, White(0.03f + 0.92f * on), px(5));
        dl->AddRect(b0, b1, White((0.20f + 0.12f * hv) * (1.0f - on)), px(5), 0, ImMax(1.0f, px(1.2f)));
        if (on > 0.01f)
        {
            // tik işareti, geçiş değeriyle birlikte yavaş yavaş çizilir
            const ImVec2 p0 = b0 + ImVec2(bs * 0.24f, bs * 0.52f);
            const ImVec2 p1 = b0 + ImVec2(bs * 0.43f, bs * 0.70f);
            const ImVec2 p2 = b0 + ImVec2(bs * 0.77f, bs * 0.31f);
            const float l1 = sqrtf(ImLengthSqr(p1 - p0)), l2 = sqrtf(ImLengthSqr(p2 - p1));
            const float d = on * (l1 + l2);
            const ImU32 cc = Gray(0.04f, on);
            const float th = px(1.8f);
            dl->AddLine(p0, ImLerp(p0, p1, ImMin(d / l1, 1.0f)), cc, th);
            if (d > l1)
                dl->AddLine(p1, ImLerp(p1, p2, ImMin((d - l1) / l2, 1.0f)), cc, th);
        }

        const float tx = bb.Min.x + px(48);
        float rightW = 0.0f;
        if (right && *right)
        {
            const ImVec2 rs = TextSize(theme::fonts.medium,theme::size::Body, right);
            rightW = rs.x + px(12);
            Text(dl, theme::fonts.medium,theme::size::Body, ImVec2(bb.Max.x - px(16) - rs.x, bb.GetCenter().y - rs.y * 0.5f), Gray(0.55f + 0.4f * on), right);
        }

        dl->PushClipRect(bb.Min, ImVec2(bb.Max.x - px(16) - rightW, bb.Max.y), true);
        Text(dl, theme::fonts.medium,theme::size::Label, ImVec2(tx, bb.Min.y + px(12)), Gray(0.78f + 0.2f * ImMax(on, hv)), label, VisibleEnd(label));
        if (desc)
            Text(dl, theme::fonts.regular,theme::size::Body, ImVec2(tx, bb.Min.y + px(33)), theme::ink::Secondary, desc);
        dl->PopClipRect();
        return pressed;
    }

    static std::unordered_map<ImGuiID, bool> g_inputActive;

    bool InputField(const char* str_id, const char* hint, char* buf, size_t buf_size, Icon icon, bool* reveal, float width, int flags)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID wid = win->GetID(str_id);
        const float   hgt = px(46);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + ImVec2(width, hgt));

        const float focus = Anim(Key(wid, "f"), g_inputActive[wid] ? 1.0f : 0.0f, 12.0f);
        const float hv    = Anim(Key(wid, "h"), ImGui::IsMouseHoveringRect(bb.Min, bb.Max) ? 1.0f : 0.0f, 12.0f);

        ImDrawList* dl = win->DrawList;
        const float r = px(11);
        if (focus > 0.01f)
            dl->AddRect(bb.Min - px(3, 3), bb.Max + px(3, 3), White(0.07f * focus), r + px(3), 0, px(3));
        dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.06f, 0.065f, 0.85f)), r);
        dl->AddRectFilled(bb.Min, bb.Max, White(0.012f * hv + 0.02f * focus), r);
        dl->AddRect(bb.Min, bb.Max, White(0.08f + 0.06f * hv + 0.26f * focus), r, 0, ImMax(1.0f, px(1)));

        const float cy = bb.GetCenter().y;
        float textX = bb.Min.x + px(16);
        if (icon != Icon::None)
        {
            icons::Draw(dl, icon, ImVec2(bb.Min.x + px(22), cy), px(16), Gray(0.40f + 0.52f * focus), px(1.5f));
            textX = bb.Min.x + px(42);
        }
        const float rightPad = reveal ? px(44) : px(14);

        const float fontSize = px(14.5f);
        ImGui::SetCursorScreenPos(ImVec2(textX, bb.Min.y));
        ImGui::PushFont(theme::fonts.medium, 14.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, (hgt - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::SetNextItemWidth(bb.Max.x - rightPad - textX);

        ImGuiInputTextFlags f = (ImGuiInputTextFlags)flags;
        if (reveal && !*reveal)
            f |= ImGuiInputTextFlags_Password;
        const bool result = ImGui::InputText(str_id, buf, buf_size, f);
        const bool active = ImGui::IsItemActive();
        g_inputActive[wid] = active;

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();

        if (buf[0] == 0 && hint)
        {
            const ImVec2 hs = TextSize(theme::fonts.medium,theme::size::Label, hint);
            Text(dl, theme::fonts.medium,theme::size::Label, ImVec2(textX, cy - hs.y * 0.5f), Gray(0.32f + 0.06f * focus), hint);
        }
        (void)fontSize;

        if (reveal)
        {
            ImGui::SetCursorScreenPos(ImVec2(bb.Max.x - px(38), cy - px(15)));
            ImGui::PushID(str_id);
            if (IconButton("##reveal", *reveal ? Icon::EyeOff : Icon::Eye, px(30, 30), px(16)))
                *reveal = !*reveal;
            ImGui::PopID();
        }

        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(width, hgt));
        return result;
    }

    bool Slider(const char* label, float* v, float vmin, float vmax, const char* fmt, float width)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID id  = win->GetID(label);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + ImVec2(width, px(46)));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);

        const ImRect track(ImVec2(bb.Min.x + px(7), bb.Min.y + px(32)), ImVec2(bb.Max.x - px(7), bb.Min.y + px(36)));
        bool changed = false;
        if (held)
        {
            const float t  = ImSaturate((ImGui::GetIO().MousePos.x - track.Min.x) / track.GetWidth());
            const float nv = vmin + t * (vmax - vmin);
            if (nv != *v)
            {
                *v = nv;
                changed = true;
                ImGui::MarkItemEdited(id);
            }
        }

        const float frac = ImSaturate((*v - vmin) / (vmax - vmin));
        const float af   = Anim(Key(id, "v"), frac, 20.0f);
        const float hv   = Anim(Key(id, "h"), (hovered || held) ? 1.0f : 0.0f, 14.0f);
        const float act  = Anim(Key(id, "a"), held ? 1.0f : 0.0f, 16.0f);

        ImDrawList* dl = win->DrawList;
        const char* end = VisibleEnd(label);
        Text(dl, theme::fonts.regular,theme::size::Body, bb.Min, Gray(0.75f + 0.15f * hv), label, end);

        char vb[32];
        snprintf(vb, sizeof vb, fmt, *v);
        const ImVec2 vs = TextSize(theme::fonts.medium,theme::size::Body, vb);
        Text(dl, theme::fonts.medium,theme::size::Body, ImVec2(bb.Max.x - vs.x, bb.Min.y), Gray(0.95f), vb);

        const float tr = track.GetHeight() * 0.5f;
        dl->AddRectFilled(track.Min, track.Max, White(0.08f), tr);
        const float kx = track.Min.x + af * track.GetWidth();
        dl->AddRectFilled(track.Min, ImVec2(kx, track.Max.y), White(0.92f), tr);

        const ImVec2 kc(kx, track.GetCenter().y);
        fx::RadialGradient(dl, kc, px(14) + act * px(5), px(14) + act * px(5), White(0.10f + 0.16f * hv), White(0.0f), 24);
        dl->AddCircleFilled(kc, px(6.5f) + px(1.5f) * hv, White(1.0f), 24);
        dl->AddCircleFilled(kc, px(2.2f), Gray(0.05f), 12);
        return changed;
    }

    bool SliderInt(const char* label, int* v, int vmin, int vmax, float width)
    {
        float f = (float)*v;
        if (Slider(label, &f, (float)vmin, (float)vmax, "%.0f", width))
        {
            const int nv = (int)lroundf(f);
            if (nv != *v)
            {
                *v = nv;
                return true;
            }
        }
        return false;
    }

    bool Segmented(const char* str_id, const char* const* items, int count, int* current, float width)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return false;

        const ImGuiID id  = win->GetID(str_id);
        const ImVec2  pos = win->DC.CursorPos;
        const float   hgt = px(38);
        const ImRect  bb(pos, pos + ImVec2(width, hgt));
        ImGui::ItemSize(bb);

        ImDrawList* dl = win->DrawList;
        dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.06f, 0.06f, 0.065f, 0.85f)), px(11));
        dl->AddRect(bb.Min, bb.Max, White(0.07f), px(11), 0, ImMax(1.0f, px(1)));

        const float pad  = px(4);
        const float segW = (width - pad * 2.0f) / (float)count;
        const float at   = Anim(Key(id, "pos"), (float)*current, 14.0f);

        // seçili bölümü gösteren kaygan vurgu
        const ImVec2 h0(bb.Min.x + pad + at * segW, bb.Min.y + pad);
        const ImVec2 h1 = h0 + ImVec2(segW, hgt - pad * 2.0f);
        fx::RadialGradient(dl, (h0 + h1) * 0.5f, segW * 0.7f, hgt, White(0.06f), White(0.0f), 32);
        dl->AddRectFilled(h0, h1, Gray(0.95f), px(8));

        bool changed = false;
        for (int i = 0; i < count; ++i)
        {
            const ImGuiID sid = ImHashData(&i, sizeof(i), id);
            const ImRect sb(ImVec2(bb.Min.x + pad + segW * i, bb.Min.y), ImVec2(bb.Min.x + pad + segW * (i + 1), bb.Max.y));
            if (!ImGui::ItemAdd(sb, sid))
                continue;
            bool hovered = false, held = false;
            if (ImGui::ButtonBehavior(sb, sid, &hovered, &held) && *current != i)
            {
                *current = i;
                changed = true;
            }
            const float hv = Anim(Key(sid, "h"), hovered ? 1.0f : 0.0f, 14.0f);
            const float s  = ImSaturate(1.0f - fabsf(at - (float)i));
            const ImVec2 ts = TextSize(theme::fonts.medium,theme::size::Body, items[i]);
            const float g = ImLerp(0.55f + 0.3f * hv, 0.05f, s);
            Text(dl, theme::fonts.medium,theme::size::Body, sb.GetCenter() - ts * 0.5f, Gray(g), items[i]);
        }
        return changed;
    }

    void ProgressBar(const char* str_id, float fraction, const ImVec2& size)
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win->SkipItems)
            return;

        const ImGuiID id  = win->GetID(str_id);
        const ImVec2  pos = win->DC.CursorPos;
        const ImRect  bb(pos, pos + size);
        ImGui::ItemSize(bb);
        ImGui::ItemAdd(bb, id);

        const float f = Anim(Key(id, "f"), ImSaturate(fraction), 9.0f);
        const float r = size.y * 0.5f;

        ImDrawList* dl = win->DrawList;
        dl->AddRectFilled(bb.Min, bb.Max, White(0.07f), r);
        if (f > 0.002f)
        {
            const float x = bb.Min.x + ImMax(f * size.x, size.y);
            dl->AddRectFilled(bb.Min, ImVec2(x, bb.Max.y), White(0.94f), r);
            fx::Shine(dl, bb.Min, ImVec2(x, bb.Max.y), fmodf((float)ImGui::GetTime() * 0.7f, 1.0f), Black(0.22f));
            if (f < 0.999f)
                fx::RadialGradient(dl, ImVec2(x, bb.GetCenter().y), px(26), ImMax(px(10), size.y * 2.5f), White(0.35f), White(0.0f), 28);
        }
    }

    // ======================================================================= çizim parçaları

    void Spinner(ImDrawList* dl, const ImVec2& c, float radius, float thickness, ImU32 col)
    {
        const float t  = (float)ImGui::GetTime();
        const float a0 = t * 6.0f;
        const float len = IM_PI * (0.9f + 0.5f * sinf(t * 2.2f));
        dl->PathArcTo(c, radius, a0, a0 + len, 24);
        dl->PathStroke(col, 0, thickness);
    }

    void Ring(ImDrawList* dl, const ImVec2& c, float radius, float th, float fraction)
    {
        dl->AddCircle(c, radius, White(0.07f), 72, th);
        fraction = ImSaturate(fraction);
        if (fraction <= 0.001f)
            return;

        const float a0 = -IM_PI * 0.5f;
        const float a1 = a0 + fraction * 2.0f * IM_PI;
        dl->PathArcTo(c, radius, a0, a1, 72);
        dl->PathStroke(White(0.95f), 0, th);

        const ImVec2 s(c.x + cosf(a0) * radius, c.y + sinf(a0) * radius);
        const ImVec2 e(c.x + cosf(a1) * radius, c.y + sinf(a1) * radius);
        dl->AddCircleFilled(s, th * 0.5f, White(0.95f), 12);
        dl->AddCircleFilled(e, th * 0.5f, White(0.95f), 12);
        fx::RadialGradient(dl, e, th * 3.0f, th * 3.0f, White(0.35f), White(0.0f), 20);
    }

    void Graph(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const float* values, int n, float vmin, float vmax, float scroll)
    {
        for (int i = 0; i <= 3; ++i)
        {
            const float y = floorf(mn.y + (mx.y - mn.y) * (float)i / 3.0f) + 0.5f;
            dl->AddLine(ImVec2(mn.x, y), ImVec2(mx.x, y), White(0.045f), 1.0f);
        }
        if (n < 3)
            return;

        static std::vector<ImVec2> pts;
        pts.resize(n);
        const float step = (mx.x - mn.x) / (float)(n - 2);
        const float hgt  = mx.y - mn.y;
        for (int i = 0; i < n; ++i)
        {
            const float x = mx.x - (float)(n - 1 - i) * step + (1.0f - ImSaturate(scroll)) * step;
            const float k = ImSaturate((values[i] - vmin) / (vmax - vmin));
            pts[i] = ImVec2(x, mx.y - px(2) - k * (hgt - px(6)));
        }

        dl->PushClipRect(mn, mx, true);
        const ImU32 top = White(0.13f), bot = White(0.0f);
        for (int i = 0; i < n - 1; ++i)
            fx::GradientQuad(dl, pts[i], pts[i + 1], ImVec2(pts[i + 1].x, mx.y), ImVec2(pts[i].x, mx.y), top, top, bot, bot);
        dl->AddPolyline(pts.data(), n, White(0.92f), 0, px(1.7f));
        dl->PopClipRect();

        // çizginin sağ kenarla birleştiği yerde parlayan uç
        const ImVec2 a = pts[n - 2], b = pts[n - 1];
        const float k  = (b.x != a.x) ? ImSaturate((mx.x - a.x) / (b.x - a.x)) : 1.0f;
        const ImVec2 head = ImLerp(a, b, k);
        fx::RadialGradient(dl, head, px(14), px(14), White(0.35f), White(0.0f), 20);
        dl->AddCircleFilled(head, px(3), White(1.0f), 12);
    }

    // ======================================================================= bildirimler

    bool notificationsEnabled = true;

    struct ToastItem
    {
        Toast       type;
        std::string title, message;
        double      start;
        float       duration;
        float       y;
    };
    static std::vector<ToastItem> g_toasts;

    void Notify(Toast type, const char* title, const char* message, float duration)
    {
        if (!notificationsEnabled && type != Toast::Error)
            return;
        g_toasts.push_back({ type, title, message ? message : "", ImGui::GetTime(), duration, -1.0f });
        if (g_toasts.size() > 4)
            g_toasts.erase(g_toasts.begin());
    }

    void RenderNotifications(const ImVec2& ds)
    {
        ImDrawList* dl  = ImGui::GetForegroundDrawList();
        const double now = ImGui::GetTime();
        const float dt   = ImGui::GetIO().DeltaTime;
        const float w = px(310), h = px(64), gap = px(10), margin = px(18), fade = 0.45f;

        float targetY = ds.y - margin;
        for (int i = (int)g_toasts.size() - 1; i >= 0; --i)
        {
            ToastItem& n = g_toasts[i];
            const float age = (float)(now - n.start);
            if (age > n.duration + fade)
            {
                g_toasts.erase(g_toasts.begin() + i);
                continue;
            }

            float in = ImSaturate(age / 0.4f);
            in = 1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in);
            const float out = age > n.duration ? 1.0f - (age - n.duration) / fade : 1.0f;
            const float a   = in * out;

            targetY -= h;
            if (n.y < 0.0f)
                n.y = targetY;
            n.y = ImLerp(n.y, targetY, 1.0f - expf(-14.0f * dt));

            const float x = ds.x - margin - w + (1.0f - in) * (w + margin) + (1.0f - out) * px(30);
            const ImVec2 mn(x, n.y), mx(x + w, n.y + h);
            const float r = px(12);

            dl->AddRectFilled(mn - px(4, 4), mx + px(4, 6), Black(0.35f * a), r + px(4));
            dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImVec4(0.06f, 0.06f, 0.065f, 0.97f * a)), r);
            dl->PushClipRect(mn, mx, true);
            fx::RadialGradient(dl, ImVec2(mn.x + px(30), mn.y + h * 0.5f), px(70), px(50), White(0.06f * a), White(0.0f), 32);
            dl->PopClipRect();
            dl->AddRect(mn, mx, White(0.11f * a), r, 0, ImMax(1.0f, px(1)));

            const ImVec2 ic(mn.x + px(30), mn.y + h * 0.5f);
            dl->AddCircleFilled(ic, px(14), White(0.94f * a), 28);
            Icon icon = Icon::Check;
            if (n.type == Toast::Info)    icon = Icon::Info;
            if (n.type == Toast::Warning) icon = Icon::Warning;
            if (n.type == Toast::Error)   icon = Icon::Error;
            icons::Draw(dl, icon, ic, px(14), Gray(0.05f, a), px(1.8f));

            dl->PushClipRect(mn, mx - ImVec2(px(12), 0), true);
            Text(dl, theme::fonts.medium,theme::size::Label, ImVec2(mn.x + px(56), mn.y + px(13)), Gray(0.96f, a), n.title.c_str());
            Text(dl, theme::fonts.regular,theme::size::Body, ImVec2(mn.x + px(56), mn.y + px(33)), Gray(0.52f, a), n.message.c_str());
            dl->PopClipRect();

            const float life = ImSaturate(1.0f - age / n.duration);
            dl->AddRectFilled(ImVec2(mn.x + r, mx.y - px(3)), ImVec2(mn.x + r + (w - 2.0f * r) * life, mx.y - px(1.5f)), White(0.45f * a), px(2));

            targetY -= gap;
        }
    }
}
