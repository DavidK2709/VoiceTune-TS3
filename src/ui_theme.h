#pragma once
// Farben und GDI+-Zeichenhelfer fuer das Einstellungsfenster.
#include <windows.h>

#include <objidl.h>

#include <algorithm>
#include <cstdint>
using std::max; // gdiplus.h braucht min/max, NOMINMAX blendet die Makros aus
using std::min;
#include <gdiplus.h>

namespace ui {
using namespace Gdiplus;

namespace col {
constexpr uint32_t bg        = 0x14161B;
constexpr uint32_t card      = 0x1D2027;
constexpr uint32_t cardHover = 0x262A33;
constexpr uint32_t border    = 0x2B303A;
constexpr uint32_t text      = 0xE8EAF0;
constexpr uint32_t sub       = 0x8A90A0;
constexpr uint32_t accent    = 0x3D8BFF;
constexpr uint32_t ok        = 0x34C77B;
constexpr uint32_t warn      = 0xF5A524;
constexpr uint32_t bad       = 0xF0555A;
constexpr uint32_t track     = 0x343944;
} // namespace col

inline Color C(uint32_t rgb, BYTE alpha = 255) { return Color(alpha, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

inline void RoundPath(GraphicsPath& p, const RectF& r, float rad)
{
    const float d = std::min(rad * 2, std::min(r.Width, r.Height));
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

inline void FillRound(Graphics& g, const RectF& r, float rad, const Color& c)
{
    GraphicsPath p;
    RoundPath(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

inline void StrokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float width = 1.0f)
{
    GraphicsPath p;
    RoundPath(p, r, rad);
    Pen pen(c, width);
    g.DrawPath(&pen, &p);
}

inline void FillCircle(Graphics& g, float cx, float cy, float rad, const Color& c)
{
    SolidBrush b(c);
    g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
}

// Einzeiliger Text mit "..." am Ende, wenn er nicht passt.
inline void Text(Graphics& g, const std::wstring& s, const Font& f, const RectF& r, const Color& c, StringAlignment h = StringAlignmentNear,
                 StringAlignment v = StringAlignmentCenter)
{
    StringFormat fmt(StringFormatFlagsNoWrap);
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g.DrawString(s.c_str(), static_cast<INT>(s.size()), &f, r, &fmt, &b);
}

// Mehrzeiliger Text (bricht um, oben links).
inline void TextWrap(Graphics& g, const std::wstring& s, const Font& f, const RectF& r, const Color& c)
{
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisWord);
    SolidBrush b(c);
    g.DrawString(s.c_str(), static_cast<INT>(s.size()), &f, r, &fmt, &b);
}

} // namespace ui
