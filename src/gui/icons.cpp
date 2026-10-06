#include "icons.hpp"
#include "brand_icons.hpp"
#include "imgui_internal.h"
#include <cmath>

namespace icons
{
    void Draw(ImDrawList* dl, Icon icon, const ImVec2& c, float s, ImU32 col, float th)
    {
        if (icon == Icon::None)
            return;
        if (th <= 0.0f)
            th = ImMax(1.0f, s * 0.085f);

        const float h = s * 0.5f;
        auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
        auto line = [&](float x0, float y0, float x1, float y1) { dl->AddLine(P(x0, y0), P(x1, y1), col, th); };

        switch (icon)
        {
        case Icon::Dashboard:
            dl->AddRectFilled(P(-0.85f, -0.85f), P(-0.12f, -0.12f), col, h * 0.18f);
            dl->AddRect(P(0.12f, -0.85f), P(0.85f, -0.12f), col, h * 0.18f, 0, th);
            dl->AddRect(P(-0.85f, 0.12f), P(-0.12f, 0.85f), col, h * 0.18f, 0, th);
            dl->AddRect(P(0.12f, 0.12f), P(0.85f, 0.85f), col, h * 0.18f, 0, th);
            break;

        case Icon::Cleaner:
        {
            line(-0.9f, -0.55f, 0.9f, -0.55f);
            const ImVec2 handle[] = { P(-0.3f, -0.55f), P(-0.3f, -0.85f), P(0.3f, -0.85f), P(0.3f, -0.55f) };
            dl->AddPolyline(handle, 4, col, 0, th);
            const ImVec2 body[] = { P(-0.65f, -0.55f), P(-0.52f, 0.9f), P(0.52f, 0.9f), P(0.65f, -0.55f) };
            dl->AddPolyline(body, 4, col, 0, th);
            line(-0.2f, -0.2f, -0.17f, 0.55f);
            line(0.2f, -0.2f, 0.17f, 0.55f);
            break;
        }

        case Icon::Tweaks:
            line(-0.9f, -0.6f, 0.9f, -0.6f);
            line(-0.9f, 0.0f, 0.9f, 0.0f);
            line(-0.9f, 0.6f, 0.9f, 0.6f);
            dl->AddCircleFilled(P(-0.4f, -0.6f), h * 0.24f, col, 16);
            dl->AddCircleFilled(P(0.38f, 0.0f), h * 0.24f, col, 16);
            dl->AddCircleFilled(P(-0.1f, 0.6f), h * 0.24f, col, 16);
            break;

        case Icon::Network:
        {
            const ImVec2 o = P(0.0f, 0.7f);
            for (int i = 1; i <= 3; ++i)
            {
                dl->PathArcTo(o, h * 0.48f * (float)i, -IM_PI * 0.78f, -IM_PI * 0.22f, 20);
                dl->PathStroke(col, 0, th);
            }
            dl->AddCircleFilled(o, th * 1.1f, col, 12);
            break;
        }

        case Icon::Settings:
            dl->AddCircle(c, h * 0.56f, col, 24, th);
            dl->AddCircle(c, h * 0.2f, col, 16, th);
            for (int i = 0; i < 8; ++i)
            {
                const float a = (float)i * IM_PI * 0.25f;
                dl->AddLine(ImVec2(c.x + cosf(a) * h * 0.6f, c.y + sinf(a) * h * 0.6f),
                            ImVec2(c.x + cosf(a) * h * 0.92f, c.y + sinf(a) * h * 0.92f), col, th * 1.8f);
            }
            break;

        case Icon::Key:
            dl->AddCircle(P(-0.48f, 0.0f), h * 0.38f, col, 20, th);
            line(-0.1f, 0.0f, 0.92f, 0.0f);
            line(0.55f, 0.0f, 0.55f, 0.32f);
            line(0.85f, 0.0f, 0.85f, 0.38f);
            break;

        case Icon::User:
            dl->AddCircle(P(0.0f, -0.38f), h * 0.36f, col, 20, th);
            dl->PathArcTo(P(0.0f, 0.95f), h * 0.78f, IM_PI * 1.08f, IM_PI * 1.92f, 20);
            dl->PathStroke(col, 0, th);
            break;

        case Icon::Logout:
        {
            const ImVec2 b[] = { P(-0.1f, -0.85f), P(-0.85f, -0.85f), P(-0.85f, 0.85f), P(-0.1f, 0.85f) };
            dl->AddPolyline(b, 4, col, 0, th);
            line(-0.35f, 0.0f, 0.9f, 0.0f);
            const ImVec2 a[] = { P(0.52f, -0.38f), P(0.9f, 0.0f), P(0.52f, 0.38f) };
            dl->AddPolyline(a, 3, col, 0, th);
            break;
        }

        case Icon::Close:
            line(-0.6f, -0.6f, 0.6f, 0.6f);
            line(0.6f, -0.6f, -0.6f, 0.6f);
            break;

        case Icon::Minimize:
            line(-0.65f, 0.0f, 0.65f, 0.0f);
            break;

        case Icon::Check:
        {
            const ImVec2 p[] = { P(-0.65f, 0.02f), P(-0.18f, 0.5f), P(0.7f, -0.5f) };
            dl->AddPolyline(p, 3, col, 0, th);
            break;
        }

        case Icon::Eye:
        case Icon::EyeOff:
            dl->AddBezierCubic(P(-1.0f, 0.0f), P(-0.5f, -0.78f), P(0.5f, -0.78f), P(1.0f, 0.0f), col, th, 20);
            dl->AddBezierCubic(P(-1.0f, 0.0f), P(-0.5f, 0.78f), P(0.5f, 0.78f), P(1.0f, 0.0f), col, th, 20);
            dl->AddCircle(c, h * 0.27f, col, 16, th);
            if (icon == Icon::EyeOff)
                line(-0.8f, -0.8f, 0.8f, 0.8f);
            break;

        case Icon::Cpu:
            dl->AddRect(P(-0.6f, -0.6f), P(0.6f, 0.6f), col, h * 0.15f, 0, th);
            dl->AddRectFilled(P(-0.24f, -0.24f), P(0.24f, 0.24f), col, h * 0.06f);
            for (int i = -1; i <= 1; ++i)
            {
                const float o = 0.3f * (float)i;
                line(o, -0.6f, o, -0.92f);
                line(o, 0.6f, o, 0.92f);
                line(-0.6f, o, -0.92f, o);
                line(0.6f, o, 0.92f, o);
            }
            break;

        case Icon::Memory:
            dl->AddRect(P(-0.95f, -0.5f), P(0.95f, 0.42f), col, h * 0.12f, 0, th);
            for (int i = 0; i < 3; ++i)
            {
                const float x = -0.6f + 0.6f * (float)i;
                dl->AddRectFilled(P(x - 0.16f, -0.22f), P(x + 0.16f, 0.14f), col, h * 0.04f);
            }
            for (int i = 0; i < 5; ++i)
            {
                const float x = -0.7f + 0.35f * (float)i;
                line(x, 0.42f, x, 0.78f);
            }
            break;

        case Icon::Disk:
            dl->AddRect(P(-0.88f, -0.62f), P(0.88f, 0.62f), col, h * 0.2f, 0, th);
            line(-0.88f, 0.12f, 0.88f, 0.12f);
            dl->AddCircleFilled(P(0.52f, 0.38f), th * 1.1f, col, 10);
            dl->AddCircleFilled(P(0.22f, 0.38f), th * 1.1f, col, 10);
            break;

        case Icon::Shield:
        {
            const ImVec2 p[] = { P(0.0f, -0.95f), P(0.8f, -0.62f), P(0.72f, 0.18f), P(0.0f, 0.95f), P(-0.72f, 0.18f), P(-0.8f, -0.62f) };
            dl->AddPolyline(p, 6, col, ImDrawFlags_Closed, th);
            const ImVec2 k[] = { P(-0.32f, 0.0f), P(-0.06f, 0.26f), P(0.36f, -0.24f) };
            dl->AddPolyline(k, 3, col, 0, th);
            break;
        }

        case Icon::Bolt:
        {
            const ImVec2 p[] = { P(0.18f, -1.0f), P(-0.62f, 0.12f), P(-0.04f, 0.12f), P(-0.18f, 1.0f), P(0.62f, -0.14f), P(0.04f, -0.14f) };
            dl->AddPolyline(p, 6, col, ImDrawFlags_Closed, th);
            break;
        }

        case Icon::Info:
            dl->AddCircleFilled(P(0.0f, -0.48f), th * 1.15f, col, 10);
            line(0.0f, -0.12f, 0.0f, 0.6f);
            break;

        case Icon::Warning:
            line(0.0f, -0.6f, 0.0f, 0.18f);
            dl->AddCircleFilled(P(0.0f, 0.55f), th * 1.15f, col, 10);
            break;

        case Icon::Error:
            line(-0.45f, -0.45f, 0.45f, 0.45f);
            line(0.45f, -0.45f, -0.45f, 0.45f);
            break;

        case Icon::Search:
            dl->AddCircle(P(-0.15f, -0.15f), h * 0.58f, col, 24, th);
            line(0.3f, 0.3f, 0.88f, 0.88f);
            break;

        case Icon::Refresh:
        {
            dl->PathArcTo(c, h * 0.72f, -IM_PI * 0.35f, IM_PI * 1.45f, 28);
            dl->PathStroke(col, 0, th);
            const ImVec2 tip(c.x + cosf(-IM_PI * 0.35f) * h * 0.72f, c.y + sinf(-IM_PI * 0.35f) * h * 0.72f);
            dl->AddLine(tip, ImVec2(tip.x - h * 0.42f, tip.y - h * 0.08f), col, th);
            dl->AddLine(tip, ImVec2(tip.x + h * 0.02f, tip.y + h * 0.42f), col, th);
            break;
        }

        case Icon::Monitor:
            dl->AddRect(P(-0.9f, -0.7f), P(0.9f, 0.35f), col, h * 0.12f, 0, th);
            line(-0.3f, 0.35f, -0.4f, 0.85f);
            line(0.3f, 0.35f, 0.4f, 0.85f);
            line(-0.55f, 0.85f, 0.55f, 0.85f);
            break;

        case Icon::Globe:
            dl->AddCircle(c, h * 0.82f, col, 28, th);
            dl->PathArcTo(ImVec2(c.x - h * 0.48f, c.y), h * 0.68f, -IM_PI * 0.5f, IM_PI * 0.5f, 16);
            dl->PathStroke(col, 0, th);
            dl->PathArcTo(ImVec2(c.x + h * 0.48f, c.y), h * 0.68f, IM_PI * 0.5f, IM_PI * 1.5f, 16);
            dl->PathStroke(col, 0, th);
            line(-0.82f, 0.0f, 0.82f, 0.0f);
            break;

        case Icon::Lock:
        {
            dl->AddRect(P(-0.65f, -0.05f), P(0.65f, 0.9f), col, h * 0.14f, 0, th);
            const ImVec2 arc[] = { P(-0.4f, -0.05f), P(-0.4f, -0.5f) };
            dl->PathArcTo(c, h * 0.4f, -IM_PI, 0, 16);
            dl->PathLineTo(P(0.4f, -0.05f));
            dl->PathStroke(col, 0, th);
            dl->AddCircleFilled(P(0.0f, 0.38f), th * 1.5f, col, 10);
            break;
        }

        case Icon::Chip:
            dl->AddRect(P(-0.5f, -0.5f), P(0.5f, 0.5f), col, h * 0.1f, 0, th);
            for (int i = -1; i <= 1; i += 2)
            {
                line((float)i * 0.3f, -0.5f, (float)i * 0.3f, -0.85f);
                line((float)i * 0.3f, 0.5f, (float)i * 0.3f, 0.85f);
                line(-0.5f, (float)i * 0.3f, -0.85f, (float)i * 0.3f);
                line(0.5f, (float)i * 0.3f, 0.85f, (float)i * 0.3f);
            }
            line(0.0f, -0.5f, 0.0f, -0.85f);
            line(0.0f, 0.5f, 0.0f, 0.85f);
            line(-0.5f, 0.0f, -0.85f, 0.0f);
            line(0.5f, 0.0f, 0.85f, 0.0f);
            break;

        // Marka işaretleri genel şekillerle yaklaşık olarak değil, gerçek konturlarından
        // çizilir; aksi hâlde Hakkında sayfası "Instagram" yazısının yanında bir kamera
        // ikonu dururdu.

        case Icon::Instagram:
        {
            // Bayrak argümanı açıkça yazılıyor: AddRect'in yalnızca yuvarlatma/kalınlık
// çiftinin tipiyle farklılaşan aşırı yüklemeleri var, oraya float yazmak belirsiz.
            dl->AddRect(P(-0.9f, -0.9f), P(0.9f, 0.9f), col, h * 0.36f, ImDrawFlags_None, th);
            dl->AddCircle(c, h * 0.44f, col, 28, th);
            dl->AddCircleFilled(P(0.5f, -0.5f), th * 1.25f, col, 10);
            break;
        }

        case Icon::GitHub:
        {
            // Gerçek siluet, tools/make_brand_icons.ps1 tarafından derleme zamanında
            // üçgenlenmiş hâlde. İlkel çizimlerle denendi önce, hepsi hayvan gibi okundu:
            // işaret 20px'te daire ve dikdörtgenlerle taklit edilemeyecek kadar organik.
            const float*   pts = brandicons::kGitHubPoints;
            const unsigned short* tris = brandicons::kGitHubTris;
            const int nPts = brandicons::kGitHubPointCount;
            const int nTris = brandicons::kGitHubTriCount;
            for (int t = 0; t < nTris; ++t)
            {
                const unsigned short a = tris[t * 3 + 0];
                const unsigned short b = tris[t * 3 + 1];
                const unsigned short d = tris[t * 3 + 2];
                if ((int)a >= nPts || (int)b >= nPts || (int)d >= nPts) continue;
                const ImVec2 pa = P(pts[a * 2 + 0], pts[a * 2 + 1]);
                const ImVec2 pb = P(pts[b * 2 + 0], pts[b * 2 + 1]);
                const ImVec2 pc = P(pts[d * 2 + 0], pts[d * 2 + 1]);
                dl->AddTriangleFilled(pa, pb, pc, col);
            }
            break;
        }

        case Icon::Code:
        {
            // Geniş, sivri açılımlar ve belirgin eğimli eğik çizgi. Dar açılımlar dik bir
            // eğik çizgiyle ikon boyutunda okunamaz tek bir şekle dönüşüyor.
            const ImVec2 left[]  = { P(-0.46f, -0.92f), P(-0.98f, 0.0f), P(-0.46f, 0.92f) };
            dl->AddPolyline(left, 3, col, 0, th);
            const ImVec2 right[] = { P(0.46f, -0.92f), P(0.98f, 0.0f), P(0.46f, 0.92f) };
            dl->AddPolyline(right, 3, col, 0, th);
            dl->AddLine(P(-0.24f, 0.78f), P(0.24f, -0.78f), col, th);
            break;
        }

        default:
            break;
        }
    }
}
