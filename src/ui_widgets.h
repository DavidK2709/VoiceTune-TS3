#pragma once
// Gemeinsame, selbst gezeichnete Bedienelemente (Schalter, Regler, Knoepfe, Symbole) fuer das Hauptfenster und das Soundboard-Fenster.
#include <cmath>
#include <string>

#include "ui_theme.h"

namespace ui {

// Wert 0..maxV aus der Mausposition; Lautstaerke (max 200) rastet bei 100 % ein, sonst auf `step` gerundet.
inline int SliderValue(const RectF& r, int x, int maxV, int step = 10)
{
    const float pad = 8 * (r.Height / 22.0f);
    float       t   = (x - (r.X + pad)) / (r.Width - 2 * pad);
    int         v   = static_cast<int>(std::lround(std::clamp(t, 0.0f, 1.0f) * maxV));
    if (maxV == 200) return std::abs(v - 100) <= 3 ? 100 : v;
    return (v + step / 2) / step * step;
}

inline void DrawToggle(Graphics& g, const RectF& r, float anim)
{
    const float rad = r.Height / 2;
    Color       off = C(col::track), on = C(col::accent);
    Color       mix(255, static_cast<BYTE>(off.GetR() + (on.GetR() - off.GetR()) * anim), static_cast<BYTE>(off.GetG() + (on.GetG() - off.GetG()) * anim),
                    static_cast<BYTE>(off.GetB() + (on.GetB() - off.GetB()) * anim));
    FillRound(g, r, rad, mix);
    const float knobR = rad - 3 * (r.Height / 26.0f);
    const float cx    = r.X + rad + (r.Width - 2 * rad) * anim;
    FillCircle(g, cx, r.Y + rad + 1, knobR, C(0x000000, 60)); // weicher Schatten
    FillCircle(g, cx, r.Y + rad, knobR, C(0xFFFFFF));
}

inline void DrawSlider(Graphics& g, const RectF& r, float frac, bool active, bool markMiddle, bool dim = false)
{
    const float u   = r.Height / 22.0f;
    const float pad = 8 * u, h = 7 * u, cy = r.Y + r.Height / 2;
    RectF       track(r.X + pad, cy - h / 2, r.Width - 2 * pad, h);
    FillRound(g, track, h / 2, C(col::track));
    const float x = track.X + track.Width * frac;
    RectF       fill(track.X, track.Y, x - track.X, h);
    if (fill.Width > 0) {
        if (dim) {
            FillRound(g, fill, h / 2, C(col::sub));
        } else {
            GraphicsPath gp;
            RoundPath(gp, fill, h / 2);
            LinearGradientBrush gb(PointF(fill.X - 0.5f, 0), PointF(fill.X + std::max(fill.Width, 1.0f), 0), C(col::accent), C(0x7DB2FF));
            g.FillPath(&gb, &gp);
        }
    }
    if (markMiddle) { // 100 %
        const float mark = track.X + track.Width * 0.5f;
        Pen         tick(C(col::sub, 120), std::max(1.0f, u));
        g.DrawLine(&tick, mark, cy - 8 * u, mark, cy - 5 * u);
    }
    const float kr = (active ? 9.0f : 8.0f) * u;
    FillCircle(g, x, cy + 1, kr, C(0x000000, 70));
    FillCircle(g, x, cy, kr, C(0xFFFFFF));
    Pen ring(dim ? C(col::sub, 190) : C(col::accent, active ? 255 : 200), std::max(1.6f, 2.2f * u));
    g.DrawEllipse(&ring, x - kr + 1.2f * u, cy - kr + 1.2f * u, 2 * (kr - 1.2f * u), 2 * (kr - 1.2f * u));
}

// Knopf: Segment/Pille. selected = Akzentfarbe, hover = heller.
inline void DrawButton(Graphics& g, const RectF& r, const std::wstring& label, const Font& f, bool selected, bool hover, float s, uint32_t selColor = col::accent, bool enabled = true)
{
    if (!enabled) {
        FillRound(g, r, r.Height / 2, C(col::track, 60));
        Text(g, label, f, r, C(col::sub, 120), StringAlignmentCenter);
        return;
    }
    if (selected) FillRound(g, r, r.Height / 2, C(selColor));
    else FillRound(g, r, r.Height / 2, hover ? C(col::cardHover) : C(col::track, 140));
    Text(g, label, f, r, selected ? C(0xFFFFFF) : C(col::text), StringAlignmentCenter);
}

// Kleiner Pfeil-Knopf (links/rechts), z.B. zum Blaettern durch Profile
inline void DrawArrowBtn(Graphics& g, const RectF& r, bool left, bool hover, bool enabled, float s)
{
    FillRound(g, r, r.Height / 2, enabled ? (hover ? C(col::cardHover) : C(col::track, 140)) : C(col::track, 60));
    const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, d = left ? -1.0f : 1.0f;
    PointF      tri[3] = {PointF(cx + d * 4 * s, cy), PointF(cx - d * 3 * s, cy - 6 * s), PointF(cx - d * 3 * s, cy + 6 * s)};
    SolidBrush  b(enabled ? C(col::text) : C(col::sub, 120));
    g.FillPolygon(&b, tri, 3);
}

// Rundes Rechteck mit senkrechtem Farbverlauf (Karten: oben eine Spur heller)
inline void FillRoundGrad(Graphics& g, const RectF& r, float rad, const Color& top, const Color& bottom)
{
    if (r.Height < 2) return;
    GraphicsPath p;
    RoundPath(p, r, rad);
    LinearGradientBrush br(PointF(r.X, r.Y), PointF(r.X, r.Y + r.Height), top, bottom);
    g.FillPath(&br, &p);
}

// Kleine, selbst gezeichnete Symbole (24 x 24 Raster, Linienstil) fuer Seitenleiste und Kartentitel
enum class Icon { Source, Speaker, Headphones, Person, Eq, Wave, Note, Pads, Chat, Info, Sliders, Send, Gear, Voice, Pop, Pin, Plus, Key };

inline void DrawIcon(Graphics& g, Icon ic, const RectF& r, const Color& col, float s)
{
    (void)s;
    const float u = r.Width / 24.0f, X = r.X, Y = r.Y;
    auto        P = [&](float x, float y) { return PointF(X + x * u, Y + y * u); };
    Pen         pen(col, std::max(1.4f, 1.8f * u));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    SolidBrush br(col);
    switch (ic) {
    case Icon::Source: { // Aktivitaets-Balken
        const float hs[4] = {8, 16, 11, 14};
        for (int i = 0; i < 4; ++i) FillRound(g, RectF(X + (3 + i * 5) * u, Y + (20 - hs[i]) * u, 3.2f * u, hs[i] * u), 1.6f * u, col);
        break;
    }
    case Icon::Speaker: {
        PointF poly[6] = {P(3, 9), P(7, 9), P(12, 5), P(12, 19), P(7, 15), P(3, 15)};
        g.FillPolygon(&br, poly, 6);
        g.DrawArc(&pen, X + 10 * u, Y + 8 * u, 7 * u, 8 * u, -55, 110);
        g.DrawArc(&pen, X + 8 * u, Y + 4 * u, 13 * u, 16 * u, -55, 110);
        break;
    }
    case Icon::Headphones:
        g.DrawArc(&pen, X + 4 * u, Y + 3 * u, 16 * u, 16 * u, 180, 180);
        FillRound(g, RectF(X + 3 * u, Y + 12 * u, 5 * u, 8 * u), 2 * u, col);
        FillRound(g, RectF(X + 16 * u, Y + 12 * u, 5 * u, 8 * u), 2 * u, col);
        break;
    case Icon::Person:
        g.DrawEllipse(&pen, X + 8 * u, Y + 3 * u, 8 * u, 8 * u);
        g.DrawArc(&pen, X + 4 * u, Y + 13 * u, 16 * u, 14 * u, 180, 180);
        break;
    case Icon::Eq: {
        const float xs[3] = {6, 12, 18}, ks[3] = {14, 7, 16};
        for (int i = 0; i < 3; ++i) {
            g.DrawLine(&pen, P(xs[i], 4), P(xs[i], 20));
            FillCircle(g, X + xs[i] * u, Y + ks[i] * u, 2.8f * u, col);
        }
        break;
    }
    case Icon::Wave: {
        PointF pts[9];
        const float hs[9] = {0, -4, 5, -7, 3, -6, 4, -3, 0};
        for (int i = 0; i < 9; ++i) pts[i] = P(2 + i * 2.5f, 12 + hs[i]);
        g.DrawCurve(&pen, pts, 9);
        break;
    }
    case Icon::Note:
        g.FillEllipse(&br, X + 4 * u, Y + 14 * u, 7 * u, 5.5f * u);
        g.DrawLine(&pen, P(10.5f, 16), P(10.5f, 4));
        g.DrawBezier(&pen, P(10.5f, 4), P(15, 5), P(18, 7), P(18, 12));
        break;
    case Icon::Pads:
        for (int i = 0; i < 4; ++i) StrokeRound(g, RectF(X + (3 + (i % 2) * 10) * u, Y + (3 + (i / 2) * 10) * u, 8 * u, 8 * u), 2.2f * u, col, std::max(1.4f, 1.8f * u));
        break;
    case Icon::Chat: {
        StrokeRound(g, RectF(X + 3 * u, Y + 4 * u, 18 * u, 12 * u), 3 * u, col, std::max(1.4f, 1.8f * u));
        PointF tail[3] = {P(8, 16), P(8, 21), P(13, 16)};
        g.DrawLines(&pen, tail, 3);
        break;
    }
    case Icon::Info:
        g.DrawEllipse(&pen, X + 3 * u, Y + 3 * u, 18 * u, 18 * u);
        g.DrawLine(&pen, P(12, 11), P(12, 17));
        FillCircle(g, X + 12 * u, Y + 7.5f * u, 1.4f * u, col);
        break;
    case Icon::Sliders: {
        const float ys[3] = {6, 12, 18}, ks[3] = {9, 15, 8};
        for (int i = 0; i < 3; ++i) {
            g.DrawLine(&pen, P(4, ys[i]), P(20, ys[i]));
            FillCircle(g, X + ks[i] * u, Y + ys[i] * u, 2.6f * u, col);
        }
        break;
    }
    case Icon::Send: {
        g.DrawLine(&pen, P(5, 19), P(19, 5));
        PointF head[3] = {P(10, 5), P(19, 5), P(19, 14)};
        g.DrawLines(&pen, head, 3);
        break;
    }
    case Icon::Voice: { // Mikrofon mit Tonhoehen-Wellen
        StrokeRound(g, RectF(X + 8.5f * u, Y + 3 * u, 7 * u, 11 * u), 3.5f * u, col, std::max(1.4f, 1.8f * u));
        g.DrawArc(&pen, X + 5.5f * u, Y + 7 * u, 13 * u, 11 * u, 10, 160);
        g.DrawLine(&pen, P(12, 18), P(12, 21));
        g.DrawLine(&pen, P(8.5f, 21), P(15.5f, 21));
        break;
    }
    case Icon::Pop: { // Fenster mit Pfeil nach rechts oben
        StrokeRound(g, RectF(X + 3 * u, Y + 7 * u, 13 * u, 13 * u), 2.5f * u, col, std::max(1.4f, 1.8f * u));
        g.DrawLine(&pen, P(11, 13), P(20, 4));
        PointF head[3] = {P(14, 4), P(20, 4), P(20, 10)};
        g.DrawLines(&pen, head, 3);
        break;
    }
    case Icon::Pin: { // Pinnnadel
        PointF head[4] = {P(8, 4), P(16, 4), P(15, 11), P(9, 11)};
        g.DrawPolygon(&pen, head, 4);
        g.DrawLine(&pen, P(6, 12), P(18, 12));
        g.DrawLine(&pen, P(12, 12), P(12, 20));
        break;
    }
    case Icon::Plus:
        g.DrawLine(&pen, P(12, 5), P(12, 19));
        g.DrawLine(&pen, P(5, 12), P(19, 12));
        break;
    case Icon::Key: { // Taste
        StrokeRound(g, RectF(X + 3 * u, Y + 6 * u, 18 * u, 12 * u), 3 * u, col, std::max(1.4f, 1.8f * u));
        g.DrawLine(&pen, P(8, 14), P(16, 14));
        break;
    }
    case Icon::Gear: { // gefuelltes Zahnrad mit 8 Zaehnen und Loch (Rundungen ueber einen Rand in derselben Farbe)
        GraphicsPath gp(FillModeAlternate);
        PointF       pts[32];
        const float  rTip = 9.6f, rRoot = 7.4f;
        for (int k = 0; k < 8; ++k) {
            const float c  = static_cast<float>(k) * 0.7853982f;
            const float da[4] = {-0.40f, -0.20f, 0.20f, 0.40f}; // Fuss, Spitze, Spitze, Fuss
            const float rr[4] = {rRoot, rTip, rTip, rRoot};
            for (int j = 0; j < 4; ++j) pts[k * 4 + j] = P(12 + std::cos(c + da[j]) * rr[j], 12 + std::sin(c + da[j]) * rr[j]);
        }
        gp.AddPolygon(pts, 32);
        gp.AddEllipse(X + 8.6f * u, Y + 8.6f * u, 6.8f * u, 6.8f * u);
        g.FillPath(&br, &gp);
        Pen edge(col, 1.2f * u);
        edge.SetLineJoin(LineJoinRound);
        g.DrawPolygon(&edge, pts, 32);
        break;
    }
    }
}


} // namespace ui
