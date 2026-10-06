#include "fx.hpp"
#include "theme.hpp"
#include "imgui_internal.h"
#include <vector>
#include <random>
#include <cmath>

namespace fx
{
    Settings settings;

    namespace
    {
        using theme::px;

        struct Particle
        {
            ImVec2 p, v;
            float  r, z, phase;
        };

        std::vector<Particle> g_particles;
        std::mt19937          g_rng{ 0xC0FFEEu };

        float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }

        // Düz beyaz, alfa kanalı elle veriliyor: bu katman hiçbir pencere dışında
        // kaldığı için stil renklerinden geçmez.
        inline ImU32 W(float a)
        {
            a = ImClamp(a, 0.0f, 1.0f);
            return IM_COL32(255, 255, 255, (int)(a * 255.0f + 0.5f));
        }

        Particle Spawn(const ImVec2& mn, const ImVec2& mx, bool at_top)
        {
            Particle q;
            q.z     = Rand(0.35f, 1.0f);                       // derinlik: uzaktakiler daha sönük ve daha yavaş düşer
            q.p     = ImVec2(Rand(mn.x, mx.x), at_top ? mn.y - Rand(px(4), px(40)) : Rand(mn.y, mx.y));
            q.v     = ImVec2(Rand(-px(5), px(5)), Rand(px(8), px(26)) * q.z);
            q.r     = px(Rand(0.7f, 1.8f) * q.z);
            q.phase = Rand(0.0f, 2.0f * IM_PI);
            return q;
        }
    }

    // ------------------------------------------------------------------ temel çizimler

    void RadialGradient(ImDrawList* dl, const ImVec2& c, float rx, float ry, ImU32 inner, ImU32 outer, int seg)
    {
        if (((inner | outer) & IM_COL32_A_MASK) == 0 || rx <= 0.0f || ry <= 0.0f)
            return;

        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        dl->PrimReserve(seg * 3, seg + 1);
        const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(c, uv, inner);
        for (int i = 0; i < seg; ++i)
        {
            const float a = (float)i / (float)seg * 2.0f * IM_PI;
            dl->PrimWriteVtx(ImVec2(c.x + cosf(a) * rx, c.y + sinf(a) * ry), uv, outer);
        }
        for (int i = 0; i < seg; ++i)
        {
            dl->PrimWriteIdx(base);
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (i + 1) % seg));
        }
    }

    void GradientQuad(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d,
                      ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd)
    {
        if (((ca | cb | cc | cd) & IM_COL32_A_MASK) == 0)
            return;

        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        dl->PrimReserve(6, 4);
        const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(a, uv, ca);
        dl->PrimWriteVtx(b, uv, cb);
        dl->PrimWriteVtx(c, uv, cc);
        dl->PrimWriteVtx(d, uv, cd);
        dl->PrimWriteIdx(base);     dl->PrimWriteIdx((ImDrawIdx)(base + 1)); dl->PrimWriteIdx((ImDrawIdx)(base + 2));
        dl->PrimWriteIdx(base);     dl->PrimWriteIdx((ImDrawIdx)(base + 2)); dl->PrimWriteIdx((ImDrawIdx)(base + 3));
    }

    void Shine(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float t, ImU32 col)
    {
        if (t <= 0.0f || t >= 1.0f || (col & IM_COL32_A_MASK) == 0)
            return;

        const ImU32 clear = col & ~IM_COL32_A_MASK;
        const float h     = mx.y - mn.y;
        const float band  = ImMax(h * 1.1f, px(36));
        const float slant = h * 0.6f;
        const float x0    = mn.x - band;
        const float x1    = mx.x + slant;
        const float x     = x0 + t * (x1 - x0);

        dl->PushClipRect(mn, mx, true);
        const ImVec2 t0(x, mn.y), t1(x + band * 0.5f, mn.y), t2(x + band, mn.y);
        const ImVec2 b0(x - slant, mx.y), b1(x + band * 0.5f - slant, mx.y), b2(x + band - slant, mx.y);
        GradientQuad(dl, t0, t1, b1, b0, clear, col, col, clear);
        GradientQuad(dl, t1, t2, b2, b1, col, clear, clear, col);
        dl->PopClipRect();
    }

    // ------------------------------------------------------------------ sahne

    static void DrawTopLight(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float alpha, float t)
    {
        const float w = mx.x - mn.x, h = mx.y - mn.y, cx = (mn.x + mx.x) * 0.5f;
        const float pulse = 0.85f + 0.15f * sinf(t * 1.1f);

        // Üst kenara oturtulmuş, aşağı doğru akan ışık kaynağı.
        RadialGradient(dl, ImVec2(cx, mn.y), w * 0.70f, h * 0.90f, W(0.14f * pulse * alpha), W(0), 72);
        RadialGradient(dl, ImVec2(cx, mn.y), w * 0.34f, h * 0.50f, W(0.18f * pulse * alpha), W(0), 64);
        RadialGradient(dl, ImVec2(cx, mn.y), w * 0.12f, h * 0.16f, W(0.26f * pulse * alpha), W(0), 48);

        // Hacimsel ışınlar: merkez parlak, kenarlar saydam yapılarak yumuşak kenar elde edilir.
        for (int i = 0; i < 7; ++i)
        {
            const float fi   = (float)i - 3.0f;
            const float sway = sinf(t * 0.35f + i * 1.9f) * 0.05f;
            const float ang  = fi * 0.13f + sway;
            const float len  = h * (0.78f + 0.18f * sinf(t * 0.5f + (float)i));
            const float topW = px(3.0f);
            const float botW = w * (0.045f + 0.02f * sinf(t * 0.7f + i * 2.3f));
            const float a    = (0.055f + 0.035f * sinf(t * 0.9f + i * 1.3f)) * alpha;

            const ImVec2 top(cx + fi * px(14), mn.y);
            const ImVec2 bot(top.x + tanf(ang) * len, mn.y + len);
            GradientQuad(dl, ImVec2(top.x - topW, top.y), top, bot, ImVec2(bot.x - botW, bot.y), W(0), W(a), W(0), W(0));
            GradientQuad(dl, top, ImVec2(top.x + topW, top.y), ImVec2(bot.x + botW, bot.y), bot, W(a), W(0), W(0), W(0));
        }

        // Üst kenarda ince parlak çizgi.
        const float lh = ImMax(1.0f, px(1.5f));
        dl->AddRectFilledMultiColor(mn, ImVec2(cx, mn.y + lh), W(0), W(0.70f * alpha), W(0.70f * alpha), W(0));
        dl->AddRectFilledMultiColor(ImVec2(cx, mn.y), ImVec2(mx.x, mn.y + lh), W(0.70f * alpha), W(0), W(0), W(0.70f * alpha));
    }

    static void DrawParticles(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float alpha, float t)
    {
        ImGuiIO& io = ImGui::GetIO();
        const float dt = ImMin(io.DeltaTime, 0.05f) * settings.speed;
        const float h  = mx.y - mn.y;

        const int target = ImClamp(settings.count, 0, 400);
        while ((int)g_particles.size() < target)
            g_particles.push_back(Spawn(mn, mx, false));
        if ((int)g_particles.size() > target)
            g_particles.resize(target);

        const ImVec2 mouse   = io.MousePos;
        const bool   mouseIn = settings.mouse && ImGui::IsMousePosValid(&mouse) &&
                               mouse.x >= mn.x && mouse.y >= mn.y && mouse.x <= mx.x && mouse.y <= mx.y;
        const float  repelR  = px(120);

        // Simülasyon: yavaş düşüş, yatay salınım, imleçten itilme, sınırdan taşınma.
        for (Particle& q : g_particles)
        {
            q.p.x += (q.v.x + sinf(t * 0.6f + q.phase) * px(6)) * dt;
            q.p.y += q.v.y * dt;

            if (mouseIn)
            {
                const ImVec2 d    = q.p - mouse;
                const float  dist = sqrtf(d.x * d.x + d.y * d.y);
                if (dist < repelR && dist > 0.01f)
                {
                    float f = 1.0f - dist / repelR;
                    f *= f;
                    q.p += d * (f * px(90) * dt / dist);
                }
            }

            if (q.p.y > mx.y + px(12))
                q = Spawn(mn, mx, true);
            if (q.p.x < mn.x - px(12))      q.p.x = mx.x + px(10);
            else if (q.p.x > mx.x + px(12)) q.p.x = mn.x - px(10);
        }

        // Parçacıklar üst ışıktan aydınlanır; kenara yaklaştıkça parlaklık artar.
        auto light = [&](float y) { return 0.40f + 0.60f * (1.0f - ImSaturate((y - mn.y) / h)); };

        if (settings.lines)
        {
            const float maxD = px(110), maxD2 = maxD * maxD, thick = ImMax(1.0f, px(1.0f));
            const int   n    = (int)g_particles.size();
            for (int i = 0; i < n; ++i)
            {
                const Particle& a = g_particles[i];
                for (int j = i + 1; j < n; ++j)
                {
                    const Particle& b = g_particles[j];
                    const float dx = a.p.x - b.p.x, dy = a.p.y - b.p.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 >= maxD2)
                        continue;
                    const float k = (1.0f - sqrtf(d2) / maxD) * 0.16f * ImMin(a.z, b.z) * light((a.p.y + b.p.y) * 0.5f);
                    dl->AddLine(a.p, b.p, W(k * alpha), thick);
                }

                if (mouseIn)
                {
                    const float mr = px(160);
                    const float dx = a.p.x - mouse.x, dy = a.p.y - mouse.y;
                    const float d  = sqrtf(dx * dx + dy * dy);
                    if (d < mr)
                        dl->AddLine(a.p, mouse, W((1.0f - d / mr) * 0.35f * a.z * alpha), thick);
                }
            }
        }

        for (const Particle& q : g_particles)
        {
            const float tw = 0.6f + 0.4f * sinf(t * 2.2f + q.phase);
            const float a  = alpha * q.z * tw * light(q.p.y);
            RadialGradient(dl, q.p, q.r * 6.0f, q.r * 6.0f, W(0.16f * a), W(0), 16);
            dl->AddCircleFilled(q.p, q.r, W(0.9f * a), 10);
        }
    }

    void DrawSweep(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float alpha)
    {
        if (!settings.sweep || alpha <= 0.001f)
            return;

        const float t      = (float)ImGui::GetTime();
        const float period = 6.5f, travel = 3.2f;
        const float ph     = fmodf(t, period) / travel;
        if (ph >= 1.0f)
            return;

        const float w = mx.x - mn.x, h = mx.y - mn.y, cx = (mn.x + mx.x) * 0.5f;
        const float e    = 1.0f - (1.0f - ph) * (1.0f - ph); // yumuşak giriş, sert çıkış
        const float band = px(140);
        const float y    = mn.y + e * (h + band);
        const float a    = sinf(ph * IM_PI) * alpha;
        const float lh   = ImMax(1.0f, px(1.2f));

        // Çizginin üzerinde kalan sürüklenen ışık.
        GradientQuad(dl, ImVec2(mn.x, y - band), ImVec2(cx, y - band), ImVec2(cx, y), ImVec2(mn.x, y), W(0), W(0), W(0.045f * a), W(0));
        GradientQuad(dl, ImVec2(cx, y - band), ImVec2(mx.x, y - band), ImVec2(mx.x, y), ImVec2(cx, y), W(0), W(0), W(0), W(0.045f * a));
        // Asıl çizgi; iki gradyan parçasına bölünüp ortası parlak bırakılır.
        GradientQuad(dl, ImVec2(mn.x, y), ImVec2(cx, y), ImVec2(cx, y + lh), ImVec2(mn.x, y + lh), W(0), W(0.35f * a), W(0.35f * a), W(0));
        GradientQuad(dl, ImVec2(cx, y), ImVec2(mx.x, y), ImVec2(mx.x, y + lh), ImVec2(cx, y + lh), W(0.35f * a), W(0), W(0), W(0.35f * a));
        RadialGradient(dl, ImVec2(cx, y), w * 0.18f, px(18), W(0.06f * a), W(0), 40);
    }

    void DrawBackground(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float alpha)
    {
        const float t = (float)ImGui::GetTime();
        const int   A = (int)(255.0f * ImSaturate(alpha));

        dl->AddRectFilled(mn, mx, IM_COL32(4, 4, 5, 255));
        dl->AddRectFilledMultiColor(mn, mx, IM_COL32(15, 15, 17, A), IM_COL32(15, 15, 17, A), IM_COL32(5, 5, 6, A), IM_COL32(5, 5, 6, A));

        if (settings.glow)
            DrawTopLight(dl, mn, mx, alpha, t);
        if (settings.particles)
            DrawParticles(dl, mn, mx, alpha, t);
        DrawSweep(dl, mn, mx, alpha);

        // Alt kenarda vinyet; sahneyi zemine bastırmak için gerekli.
        const float h = mx.y - mn.y;
        dl->AddRectFilledMultiColor(ImVec2(mn.x, mx.y - h * 0.35f), mx, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150));
    }
}
