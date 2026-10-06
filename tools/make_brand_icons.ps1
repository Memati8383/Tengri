# Flattens an SVG path into triangles that icons.cpp can feed to ImDrawList.
#
# ImDrawList can only fill convex polygons (AddConvexPolyFilled / PathFillConvex). The
# real brand marks are concave silhouettes, so drawing them faithfully means triangulating
# once, offline, and baking the triangle list into the source. Doing it here rather than
# at runtime keeps the renderer trivial and the cost is paid at build time.
#
# Paths come from the brand outlines in a 24x24 viewBox.
#
#   powershell -File tools\make_brand_icons.ps1

param(
    [string]$OutPath = 'src/gui/brand_icons.cpp'
)

$csharp = @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

public static class PathTri
{
    struct Pt { public double X, Y; public Pt(double x, double y) { X = x; Y = y; } }

    // A C++ floating-point literal needs either a decimal point or an exponent.
    // "0f" and "1f" are both invalid, because the digit-sequence form of the grammar
// requires the exponent part. Formatting a whole number with "0.####" drops the point
// and produces exactly those, so one is put back.
static string Fmt(double v)
{
    if (Math.Abs(v) < 0.00005) v = 0.0;
    string s = v.ToString("0.####", CultureInfo.InvariantCulture);
    if (s.IndexOf('.') < 0) s += ".0";
    if (s.StartsWith("-0.0") && double.Parse(s, CultureInfo.InvariantCulture) == 0.0) s = s.Substring(1);
    return s + "f";
}


    // ---- tokenising ---------------------------------------------------------

    static List<List<Pt>> Parse(string d)
    {
        var contours = new List<List<Pt>>();
        var cur = new List<Pt>();
        double cx = 0, cy = 0, sx = 0, sy = 0;
        double px = 0, py = 0;      // previous control point, for S/T
        char cmd = '\0';
        int i = 0;

        // A C++ floating-point literal needs either a decimal point or an exponent:
// "0f" and "1f" are both invalid, because the digit-sequence form of the grammar
// requires the exponent part. Formatting a whole number with "0.####" drops the point
// and produces exactly those, so one is put back.

// Local functions are C# 7; Add-Type compiles with the legacy CodeDom compiler,
        // so these are delegates instead.
        Action Flush = () => { if (cur.Count > 2) contours.Add(cur); cur = new List<Pt>(); };

        Func<double> Num = () =>
        {
            while (i < d.Length && (d[i] == ' ' || d[i] == ',' || d[i] == '\n' || d[i] == '\r' || d[i] == '\t')) i++;
            int start = i;
            if (i < d.Length && (d[i] == '-' || d[i] == '+')) i++;

            // A number is digits, at most one dot, then digits. Allowing dots anywhere in
            // the run swallows "11.385.6.113.82" as a single token, which is really four
            // numbers written without separators -- the way SVG path data is actually
            // written once a file has been minified.
            int digits = 0;
            while (i < d.Length && d[i] >= '0' && d[i] <= '9') { i++; digits++; }
            if (i < d.Length && d[i] == '.')
            {
                i++;
                while (i < d.Length && d[i] >= '0' && d[i] <= '9') { i++; digits++; }
            }
            if (digits == 0)
                throw new FormatException("expected a number at offset " + i + " in: ..." +
                                          d.Substring(Math.Max(0, i - 40), Math.Min(90, d.Length - Math.Max(0, i - 40))) + "...");

            if (i < d.Length && (d[i] == 'e' || d[i] == 'E'))
            {
                i++;
                if (i < d.Length && (d[i] == '-' || d[i] == '+')) i++;
                while (i < d.Length && d[i] >= '0' && d[i] <= '9') i++;
            }

            return double.Parse(d.Substring(start, i - start), CultureInfo.InvariantCulture);
        };

        Action<double, double, double, double, double, double> Cubic = (x1, y1, x2, y2, x, y) =>
        {
            // Fixed subdivision. The marks are 24 units across and end up at ~20px, so
            // anything past ~10 segments per curve is below a pixel.
            const int N = 10;
            for (int k = 1; k <= N; k++)
            {
                double t = (double)k / N, u = 1 - t;
                double bx = u*u*u*cx + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x;
                double by = u*u*u*cy + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y;
                cur.Add(new Pt(bx, by));
            }
            px = x2; py = y2;
            cx = x; cy = y;
        };

        Action<double, double, double, double> Quad = (x1, y1, x, y) =>
        {
            const int N = 8;
            for (int k = 1; k <= N; k++)
            {
                double t = (double)k / N, u = 1 - t;
                cur.Add(new Pt(u*u*cx + 2*u*t*x1 + t*t*x, u*u*cy + 2*u*t*y1 + t*t*y));
            }
            px = x1; py = y1;
            cx = x; cy = y;
        };

        while (i < d.Length)
        {
            while (i < d.Length && (d[i] == ' ' || d[i] == ',' || d[i] == '\n' || d[i] == '\r' || d[i] == '\t')) i++;
            if (i >= d.Length) break;

            if (char.IsLetter(d[i]))
            {
                cmd = d[i]; i++;
                if (cmd == 'Z' || cmd == 'z')
                {
                    if (cur.Count > 2) contours.Add(cur);
                    cur = new List<Pt>();
                    cx = sx; cy = sy; px = cx; py = cy;
                    continue;
                }
            }
            else if (cmd == 'M') cmd = 'L';
            else if (cmd == 'm') cmd = 'l';

            bool rel = char.IsLower(cmd);
            char c0 = char.ToUpper(cmd);

            if (c0 == 'M')
            {
                if (cur.Count > 2) contours.Add(cur);
                cur = new List<Pt>();
                double x = Num(), y = Num();
                if (rel) { x += cx; y += cy; }
                cx = sx = x; cy = sy = y; px = cx; py = cy;
                cur.Add(new Pt(cx, cy));
            }
            else if (c0 == 'L')
            {
                double x = Num(), y = Num();
                if (rel) { x += cx; y += cy; }
                cx = x; cy = y; px = cx; py = cy;
                cur.Add(new Pt(cx, cy));
            }
            else if (c0 == 'H')
            {
                double x = Num(); if (rel) x += cx;
                cx = x; px = cx; py = cy;
                cur.Add(new Pt(cx, cy));
            }
            else if (c0 == 'V')
            {
                double y = Num(); if (rel) y += cy;
                cy = y; px = cx; py = cy;
                cur.Add(new Pt(cx, cy));
            }
            else if (c0 == 'C')
            {
                double x1 = Num(), y1 = Num(), x2 = Num(), y2 = Num(), x = Num(), y = Num();
                if (rel) { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; }
                Cubic(x1, y1, x2, y2, x, y);
            }
            else if (c0 == 'S')
            {
                double x2 = Num(), y2 = Num(), x = Num(), y = Num();
                if (rel) { x2 += cx; y2 += cy; x += cx; y += cy; }
                Cubic(2 * cx - px, 2 * cy - py, x2, y2, x, y);
            }
            else if (c0 == 'Q')
            {
                double x1 = Num(), y1 = Num(), x = Num(), y = Num();
                if (rel) { x1 += cx; y1 += cy; x += cx; y += cy; }
                Quad(x1, y1, x, y);
            }
            else if (c0 == 'T')
            {
                double x = Num(), y = Num();
                if (rel) { x += cx; y += cy; }
                Quad(2 * cx - px, 2 * cy - py, x, y);
            }
            else if (c0 == 'A')
            {
                double rx = Num(), ry = Num(), rot = Num();
                bool large = Num() != 0, sweep = Num() != 0;
                double x = Num(), y = Num();
                if (rel) { x += cx; y += cy; }
                Arc(cx, cy, rx, ry, rot, large, sweep, x, y);
            }
            else
            {
                throw new Exception("unsupported path command: " + cmd);
            }
        }
        Flush();
        return contours;
    }

    // ---- elliptical arc -> cubic segments (SVG implementation notes F.6.5) ----

    static void Arc(double x1, double y1, double rx, double ry, double phiDeg,
                    bool large, bool sweep, double x2, double y2)
    {
        if (rx == 0 || ry == 0) { x1 = x2; y1 = y2; return; }
        rx = Math.Abs(rx); ry = Math.Abs(ry);
        double phi = phiDeg * Math.PI / 180.0;
        double dx2 = (x1 - x2) / 2.0, dy2 = (y1 - y2) / 2.0;
        double x1p =  Math.Cos(phi) * dx2 + Math.Sin(phi) * dy2;
        double y1p = -Math.Sin(phi) * dx2 + Math.Cos(phi) * dy2;

        double lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lam > 1) { double s = Math.Sqrt(lam); rx *= s; ry *= s; }

        double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
        double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
        double co = Math.Sqrt(Math.Max(0, num / den));
        if (large == sweep) co = -co;
        double cxp =  co * rx * y1p / ry;
        double cyp = -co * ry * x1p / rx;

        double ccx = Math.Cos(phi) * cxp - Math.Sin(phi) * cyp + (x1 + x2) / 2.0;
        double ccy = Math.Sin(phi) * cxp + Math.Cos(phi) * cyp + (y1 + y2) / 2.0;

        double th1 = Angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
        double dth = Angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        if (!sweep && dth > 0) dth -= 2 * Math.PI;
        else if (sweep && dth < 0) dth += 2 * Math.PI;

        int segs = (int)Math.Ceiling(Math.Abs(dth) / (Math.PI / 2.0));
        double delta = dth / segs;
        double t = 4.0 / 3.0 * Math.Tan(delta / 4.0);

        double th = th1;
        double cxp2 = ccx, cyp2 = ccy;
        for (int s = 0; s < segs; s++)
        {
            double th2 = th + delta;
            double ex = ccx + rx * Math.Cos(phi) * Math.Cos(th2) - ry * Math.Sin(phi) * Math.Sin(th2);
            double ey = ccy + rx * Math.Sin(phi) * Math.Cos(th2) + ry * Math.Cos(phi) * Math.Sin(th2);
            double d1x = -rx * Math.Cos(phi) * Math.Sin(th) - ry * Math.Sin(phi) * Math.Cos(th);
            double d1y = -rx * Math.Sin(phi) * Math.Sin(th) + ry * Math.Cos(phi) * Math.Cos(th);
            double d2x = -rx * Math.Cos(phi) * Math.Sin(th2) - ry * Math.Sin(phi) * Math.Cos(th2);
            double d2y = -rx * Math.Sin(phi) * Math.Sin(th2) + ry * Math.Cos(phi) * Math.Cos(th2);

            double c1x = cxp2 + t * d1x, c1y = cyp2 + t * d1y;
            double c2x = ex - t * d2x,      c2y = ey - t * d2y;

            const int N = 8;
            for (int k = 1; k <= N; k++)
            {
                double u = (double)k / N, v = 1 - u;
                pending.Add(new Pt(
                    v*v*v*cxp2 + 3*v*v*u*c1x + 3*v*u*u*c2x + u*u*u*ex,
                    v*v*v*cyp2 + 3*v*v*u*c1y + 3*v*u*u*c2y + u*u*u*ey));
            }
            th = th2; cxp2 = ex; cyp2 = ey;
        }
        x1 = x2; y1 = y2;
    }

    static List<Pt> pending = new List<Pt>();
    static List<Pt> sink = new List<Pt>();

    static double Angle(double ux, double uy, double vx, double vy)
    {
        double d = (ux * vx + uy * vy) / (Math.Sqrt(ux*ux + uy*uy) * Math.Sqrt(vx*vx + vy*vy));
        d = Math.Max(-1, Math.Min(1, d));
        double a = Math.Acos(d);
        if (ux * vy - uy * vx < 0) a = -a;
        return a;
    }

    // ---- ear clipping -------------------------------------------------------

    static List<int[]> Triangulate(List<Pt> poly)
    {
        var tris = new List<int[]>();
        int n = poly.Count;
        if (n < 3) return tris;
        var idx = new List<int>();
        for (int k = 0; k < n; k++) idx.Add(k);

        bool ccw = SignedArea(poly) > 0;
        if (!ccw) idx.Reverse();

        int guard = 0;
        while (idx.Count > 3 && guard++ < n * n + 64)
        {
            bool clipped = false;
            for (int i = 0; i < idx.Count; i++)
            {
                int ia = idx[(i + idx.Count - 1) % idx.Count], ib = idx[i], ic = idx[(i + 1) % idx.Count];
                if (!IsEar(poly, idx, ia, ib, ic)) continue;
                tris.Add(new[] { ia, ib, ic });
                idx.RemoveAt(i);
                clipped = true;
                break;
            }
            if (!clipped) break;   // degenerate input: bail with what we have
        }
        if (idx.Count == 3) tris.Add(new[] { idx[0], idx[1], idx[2] });
        return tris;
    }

    static double SignedArea(List<Pt> p)
    {
        double a = 0;
        for (int k = 0; k < p.Count; k++)
        {
            Pt q = p[k], r = p[(k + 1) % p.Count];
            a += q.X * r.Y - r.X * q.Y;
        }
        return a / 2.0;
    }

    static bool InTri(Pt a, Pt b, Pt c, Pt p)
    {
        double d1 = Cross(a, b, p), d2 = Cross(b, c, p), d3 = Cross(c, a, p);
        bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(neg && pos);
    }

    static double Cross(Pt o, Pt a, Pt b) { return (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X); }

    static bool IsEar(List<Pt> poly, List<int> idx, int ia, int ib, int ic)
    {
        Pt a = poly[ia], b = poly[ib], c = poly[ic];
        if (Cross(a, b, c) <= 0) return false;          // reflex or degenerate
        for (int k = 0; k < idx.Count; k++)
        {
            int ip = idx[k];
            if (ip == ia || ip == ib || ip == ic) continue;
            if (InTri(a, b, c, poly[ip])) return false;
        }
        return true;
    }

    // ---- entry point --------------------------------------------------------

    public static string Run(string name, string path, string indent)
    {
        pending.Clear();
        var contours = Parse(path);

        // Arc() appends through the shared buffer; splice those points into the contour
        // list in order, which Parse has not seen.
        var points = new List<Pt>();
        var outTris = new List<int[]>();

        var all = new List<List<Pt>>();
        foreach (var c in contours) all.Add(c);
        if (pending.Count > 0) all.Add(pending);

        foreach (var c in all)
        {
            // Drop near-duplicates: the flattener emits a point per curve segment, and
            // collinear runs would otherwise triple the triangle count for nothing.
            var simp = new List<Pt>();
            foreach (var p in c)
            {
                if (simp.Count > 0)
                {
                    Pt q = simp[simp.Count - 1];
                    if (Math.Abs(q.X - p.X) < 0.004 && Math.Abs(q.Y - p.Y) < 0.004) continue;
                }
                simp.Add(p);
            }
            if (simp.Count < 3) continue;

            int baseIdx = points.Count;
            points.AddRange(simp);
            foreach (var t in Triangulate(simp))
                outTris.Add(new[] { baseIdx + t[0], baseIdx + t[1], baseIdx + t[2] });
        }

        // Normalise to -1..1 around the viewBox centre so icons::Draw's P() applies.
        var sb = new StringBuilder();

        // Counts are exported because the arrays are extern in the header, and sizeof on
        // an incomplete array type does not compile.
        sb.Append(indent).Append("const int ").Append(name).Append("PointCount = ").Append(points.Count).Append(";\n");
        sb.Append(indent).Append("const int ").Append(name).Append("TriCount = ").Append(outTris.Count).Append(";\n\n");

        sb.Append(indent).Append("const float ").Append(name).Append("Points[] = {\n");
        for (int k = 0; k < points.Count; k++)
        {
            double nx = (points[k].X - 12.0) / 12.0;
            double ny = (points[k].Y - 12.0) / 12.0;
            if (k % 8 == 0) sb.Append(indent).Append("    ");
            sb.Append(Fmt(nx)).Append(", ").Append(Fmt(ny)).Append(",");
            if (k % 8 == 7) sb.Append('\n'); else sb.Append(' ');
        }
        sb.Append("\n").Append(indent).Append("};\n\n");

        sb.Append(indent).Append("const unsigned short ").Append(name).Append("Tris[] = {\n");
        for (int k = 0; k < outTris.Count; k++)
        {
            if (k % 10 == 0) sb.Append(indent).Append("    ");
            sb.AppendFormat(CultureInfo.InvariantCulture, "{0},{1},{2},", outTris[k][0], outTris[k][1], outTris[k][2]);
            if (k % 10 == 9) sb.Append('\n'); else sb.Append(' ');
        }
        sb.Append("\n").Append(indent).Append("};\n");

        return sb.ToString();
    }
}
'@

Add-Type -TypeDefinition $csharp -ReferencedAssemblies System.Drawing

$github = 'M12 .297c-6.63 0-12 5.373-12 12 0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.042-1.61-4.042-1.61C4.422 18.07 3.633 17.7 3.633 17.7c-1.087-.744.084-.729.084-.729 1.205.084 1.838 1.236 1.838 1.236 1.07 1.835 2.809 1.305 3.495.998.108-.776.417-1.305.76-1.605-2.665-.3-5.466-1.332-5.466-5.93 0-1.31.465-2.38 1.235-3.22-.135-.303-.54-1.523.105-3.176 0 0 1.005-.322 3.3 1.23.96-.267 1.98-.399 3-.405 1.02.006 2.04.138 3 .405 2.28-1.552 3.285-1.23 3.285-1.23.645 1.653.24 2.873.12 3.176.765.84 1.23 1.91 1.23 3.22 0 4.61-2.805 5.625-5.475 5.92.42.36.81 1.096.81 2.22 0 1.606-.015 2.896-.015 3.286 0 .315.21.69.825.57C20.565 22.092 24 17.592 24 12.297c0-6.627-5.373-12-12-12'

$body  = [PathTri]::Run('kGitHub', $github, '    ')

$out = @"
// tools/make_brand_icons.ps1 tarafından üretildi; elle düzenlenmemelidir.
//
// Hakkında sayfasındaki marka işaretlerinin üçgen listeleri.
//
// Önceden üçgenlenmelerinin nedeni: ImDrawList yalnızca konveks poligon
// (AddConvexPolyFilled) doldurabiliyor, işaretler ise konkav. Düzleştirmeyi derleme
// zamanına bırakmak, çalışma anı maliyetini bir statik dizi üzerinde döngüye indiriyor.
//
// Koordinatlar viewBox merkezine göre -1..1 aralığına normalleştirilmiştir; bu tam olarak
// icons::Draw'ın P() yardımcısının beklediği biçimdir.

#include "brand_icons.hpp"

namespace brandicons
{
$body}
"@

[System.IO.File]::WriteAllText((Join-Path (Get-Location) $OutPath), $out, (New-Object System.Text.UTF8Encoding $false))
Write-Output ("wrote {0}" -f $OutPath)
