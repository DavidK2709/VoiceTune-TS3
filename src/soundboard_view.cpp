#include "soundboard_view.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "engine.h"
#include "i18n.h"
#include "playlist.h"
#include "presets.h"
#include "ui_widgets.h"

using namespace ui;

namespace sbv {
namespace {

void (*g_bind)(int)                 = nullptr;
std::string (*g_text)(int)          = nullptr;
void (*g_all)()                     = nullptr;

constexpr uint32_t kDefaultTile = 0x2C4A82; // Farbe eines Knopfes ohne eigene Farbe
constexpr uint32_t kSwatch[12]  = {0, 0xC0392B, 0xE67E22, 0xE5B800, 0x27AE60, 0x16A085, 0x2980B9, 0x6C5CE7, 0x8E44AD, 0xD81B60, 0x7F8C8D, 0x34495E};
constexpr size_t   kMaxEditName = 28;

bool In(const RectF& r, int x, int y) { return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height; }

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring FileTitle(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name  = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot   = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return presets::Sanitize(name);
}

std::wstring Fmt1(const std::wstring& pattern, const std::wstring& arg)
{
    std::wstring out = pattern;
    const size_t pos = out.find(L"%s");
    if (pos != std::wstring::npos) out.replace(pos, 2, arg);
    return out;
}

// Farbe zu `target` (0 = schwarz, 255 = weiss) hin mischen
uint32_t Shade(uint32_t rgb, int target, float t)
{
    auto mix = [&](int v) { return static_cast<uint32_t>(std::clamp(static_cast<int>(v + (target - v) * t), 0, 255)); };
    return (mix((rgb >> 16) & 255) << 16) | (mix((rgb >> 8) & 255) << 8) | mix(rgb & 255);
}

bool IsLight(uint32_t rgb) { return 0.299f * ((rgb >> 16) & 255) + 0.587f * ((rgb >> 8) & 255) + 0.114f * (rgb & 255) > 165.0f; }

// Mehrzeiliger, mittig ausgerichteter Text mit "..." am Ende
void TextCenterWrap(Graphics& g, const std::wstring& s, const Font& f, const RectF& r, const Color& c)
{
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisWord);
    SolidBrush b(c);
    g.DrawString(s.c_str(), static_cast<INT>(s.size()), &f, r, &fmt, &b);
}

std::wstring ClipboardLine(HWND hwnd)
{
    std::wstring out;
    if (!OpenClipboard(hwnd)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            for (; *p && *p != L'\r' && *p != L'\n'; ++p) out.push_back(*p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

std::wstring AudioFilter()
{
    std::wstring f = Tr(L"Audio-Dateien");
    f.push_back(L'\0');
    f += L"*.mp3;*.wav;*.flac;*.m4a;*.aac;*.wma;*.ogg;*.oga;*.opus;*.mp4;*.mka;*.aiff;*.aif;*.webm;*.mp2;*.wv;*.ac3";
    f.push_back(L'\0');
    f += Tr(L"Alle Dateien");
    f.push_back(L'\0');
    f += L"*.*";
    f.push_back(L'\0');
    return f;
}

std::wstring FormatPercent(int v) { return std::to_wstring(v) + L" %"; }

} // namespace

void SetHotkeyHandlers(void (*bind)(int slot), std::string (*text)(int slot), void (*all)())
{
    g_bind = bind;
    g_text = text;
    g_all  = all;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------------------------------------------------------

View::Geo View::MakeGeo(const RectF& area, const Host& h) const
{
    Geo         g;
    const float s = h.s;
    g.area        = area;
    g.bar         = RectF(area.X, area.Y, area.Width, 44 * s);
    g.foot        = RectF(area.X, area.Y + area.Height - 44 * s, area.Width, 44 * s);

    // Werkzeugleiste oben: Zeilen/Spalten-Zaehler wie bei RP Soundboard, Tastenkuerzel-Schalter, rechts Abkoppeln bzw. Immer-oben
    const float y = g.bar.Y + 8 * s, bh = 28 * s;
    float       x = area.X + 4 * s;
    g.rowsLabel   = RectF(x, y, 56 * s, bh);
    g.rowsMinus   = RectF(x + 58 * s, y, 28 * s, bh);
    g.rowsVal     = RectF(x + 88 * s, y, 32 * s, bh);
    g.rowsPlus    = RectF(x + 122 * s, y, 28 * s, bh);
    x += 168 * s;
    g.colsLabel = RectF(x, y, 62 * s, bh);
    g.colsMinus = RectF(x + 64 * s, y, 28 * s, bh);
    g.colsVal   = RectF(x + 94 * s, y, 32 * s, bh);
    g.colsPlus  = RectF(x + 128 * s, y, 28 * s, bh);
    x += 176 * s;
    const float auxW = (h.popout ? 150.0f : 128.0f) * s;
    g.aux            = RectF(area.X + area.Width - 4 * s - auxW, y, auxW, bh);
    g.showKeys       = RectF(x, y + 1 * s, 44 * s, 26 * s);
    g.showKeysLabel  = RectF(x + 52 * s, y, std::max(0.0f, g.aux.X - (x + 52 * s) - 8 * s), bh);

    // Fusszeile: Gesamtlautstaerke und "Alle stoppen"
    const float fy = g.foot.Y + 8 * s;
    g.volLabel     = RectF(area.X + 4 * s, fy, 88 * s, bh);
    g.volume       = RectF(area.X + 94 * s, fy + 3 * s, std::min(300.0f * s, std::max(80.0f * s, area.Width - 94 * s - 4 * s - 150 * s - 70 * s)), 22 * s);
    g.volVal       = RectF(g.volume.X + g.volume.Width + 6 * s, fy, 56 * s, bh);
    g.stop         = RectF(area.X + area.Width - 4 * s - 140 * s, fy, 140 * s, bh);

    g.gridArea = RectF(area.X, g.bar.Y + g.bar.Height + 4 * s, area.Width, g.foot.Y - (g.bar.Y + g.bar.Height) - 8 * s);
    return g;
}

int View::CellAt(const Geo& g, int x, int y) const
{
    if (!In(g.gridArea, x, y) || g.cols <= 0 || g.rows <= 0 || g.cellW <= 0) return -1;
    const float fx = x - g.gridArea.X, fy = y - g.gridArea.Y + scroll_;
    const float stepX = g.cellW + g.gap, stepY = g.cellH + g.gap;
    const int   c = static_cast<int>(fx / stepX), r = static_cast<int>(fy / stepY);
    if (c < 0 || c >= g.cols || r < 0 || r >= g.rows) return -1;
    if (fx - c * stepX > g.cellW || fy - r * stepY > g.cellH) return -1; // Luecke zwischen den Knoepfen
    return r * g.cols + c;
}

float View::MaxScroll(const Geo& g) const { return std::max(0.0f, g.content - g.gridArea.Height); }

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------------------------------------------------------------------------

void View::OpenEditor(int slot, const std::wstring& path)
{
    const auto info = Engine::Get().GetSound(slot);
    editor_         = Editor();
    editor_.open    = true;
    editor_.slot    = slot;
    editor_.color   = info.color;
    editor_.volPct  = info.path.empty() ? 100 : info.volPct;
    if (!path.empty() && path != info.path) { // neue Datei (Ziehen/Waehlen): Name folgt dem Dateinamen
        editor_.path       = path;
        editor_.name       = FileTitle(path);
        editor_.nameEdited = false;
    } else {
        editor_.path       = info.path;
        editor_.name       = info.name;
        editor_.nameEdited = !info.path.empty() && info.name != FileTitle(info.path);
    }
    editor_.nameSel = true;
    hoverEd_        = -1;
    keyCacheAt_     = 0;
}

void View::SaveEditor()
{
    if (!editor_.open) return;
    if (editor_.path.empty()) {
        SetToast(Tr(L"Keine Datei gewählt"));
        return;
    }
    Engine::Get().SoundConfigure(editor_.slot, editor_.path, editor_.name, editor_.color, editor_.volPct);
    editor_.open = false;
}

void View::PickFile(const Host& h)
{
    std::vector<wchar_t> buf(4096, L'\0');
    const std::wstring   filter = AudioFilter();
    const std::wstring   title  = Tr(L"Sound-Datei auswählen");
    OPENFILENAMEW        ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = h.hwnd;
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle  = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    editor_.path = buf.data();
    if (!editor_.nameEdited) editor_.name = FileTitle(editor_.path);
}

// Mehrere Dateien auf freie Felder (ab `slot`, danach der Reihe nach) verteilen
void View::Assign(const std::vector<std::wstring>& files, int slot)
{
    Engine&    en    = Engine::Get();
    const auto st    = en.GetSettings();
    const int  total = std::min(kSoundSlots, st.soundRows * st.soundCols);
    std::vector<bool> taken(kSoundSlots, false); // in dieser Runde belegt (st ist ein Schnappschuss von davor)
    int        next  = std::max(0, slot);
    int        done  = 0;
    bool       full  = false;
    bool       first = true;
    for (const auto& f : files) {
        if (!playlist::IsAudioFile(f)) continue;
        int target = -1;
        if (first && slot >= 0) {
            target = slot; // die erste Datei ersetzt, was auf dem Zielknopf lag
        } else {
            for (; next < total; ++next)
                if (!taken[next] && st.sounds[next].path.empty()) break;
            if (next < total) target = next;
        }
        first = false;
        if (target < 0) {
            full = true;
            break;
        }
        en.SoundSetSlot(target, f);
        taken[target] = true;
        ++done;
    }
    if (done > 0) SetToast(std::to_wstring(done) + (done == 1 ? Tr(L" Sound zugewiesen") : Tr(L" Sounds zugewiesen")) + (full ? Tr(L" (alle Felder belegt)") : L""));
    else SetToast(full ? Tr(L"Alle Felder belegt – Raster vergrößern") : Tr(L"Keine Audio-Datei erkannt"));
}

void View::Flash(int slot)
{
    if (slot >= 0 && slot < static_cast<int>(flash_.size())) flash_[slot] = 1.0f;
}

// Trefferliste des Editors: 0 Name, 2 Datei waehlen, 3..14 Farben, 15 Lautstaerke, 16 Tastenkuerzel, 17 Entfernen, 18 Abbrechen, 19 Speichern, 20 Panel (sonst nichts), -1 ausserhalb
int View::HitEditor(const Geo& g, int x, int y) const
{
    if (!In(g.panel, x, y)) return -1;
    if (In(g.nameField, x, y)) return 0;
    if (In(g.fileBtn, x, y)) return 2;
    for (int i = 0; i < 12; ++i)
        if (In(g.swatch[i], x, y)) return 3 + i;
    if (In(g.volSlider, x, y)) return 15;
    if (In(g.keyBtn, x, y) && editor_.slot < kSoundHotkeys) return 16;
    if (In(g.delBtn, x, y) && !Engine::Get().GetSound(editor_.slot).path.empty()) return 17;
    if (In(g.cancelBtn, x, y)) return 18;
    if (In(g.okBtn, x, y)) return 19;
    return 20;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Zeichnen
// ---------------------------------------------------------------------------------------------------------------------------------------------

void View::Paint(Graphics& g, const RectF& area, const Host& h)
{
    const float s   = h.s;
    Engine&     en  = Engine::Get();
    const auto  st  = en.GetSettings();
    const auto  snd = en.GetSounds();

    const ULONGLONG now = GetTickCount64();
    const float     dt  = lastTick_ ? std::min(0.2f, static_cast<float>(now - lastTick_) / 1000.0f) : 0.0f;
    lastTick_           = now;
    for (float& f : flash_) f = std::max(0.0f, f - dt / 0.45f);
    if (toastUntil_ && now > toastUntil_) {
        toast_.clear();
        toastUntil_ = 0;
    }

    Geo geo   = MakeGeo(area, h);
    geo.rows  = st.soundRows;
    geo.cols  = st.soundCols;
    geo.gap   = (geo.cols > 10 || geo.rows > 10 ? 4.0f : 8.0f) * s;
    const float usableW = geo.gridArea.Width - 8 * s;
    geo.cellW           = std::max(8.0f, (usableW - (geo.cols - 1) * geo.gap) / geo.cols);
    const float fitH    = (geo.gridArea.Height - (geo.rows - 1) * geo.gap) / geo.rows;
    geo.cellH           = std::clamp(std::min(geo.cellW * 0.75f, fitH), 30.0f * s, 120.0f * s);
    geo.content         = geo.rows * geo.cellH + (geo.rows - 1) * geo.gap;
    scroll_             = std::clamp(scroll_, 0.0f, MaxScroll(geo));

    // Tastenkuerzel der ersten Knoepfe (Zwischenspeicher, hoechstens alle 1,5 s abfragen)
    if (g_text && (st.soundShowKeys || editor_.open) && now - keyCacheAt_ > 1500) {
        keyCacheAt_ = now;
        keyText_.assign(kSoundHotkeys, L"");
        const int n = std::min(kSoundHotkeys, geo.rows * geo.cols);
        for (int i = 0; i < n; ++i)
            if (!snd[i].path.empty() || (editor_.open && editor_.slot == i)) keyText_[i] = Utf8ToWide(g_text(i));
    }

    // ---- Werkzeugleiste oben -------------------------------------------------------------------------------------------------------------------
    auto stepper = [&](const RectF& label, const RectF& minus, const RectF& val, const RectF& plus, const wchar_t* name, int value, int lo, int hi, int hid) {
        Text(g, Tr(name), *h.small, label, C(col::sub));
        DrawButton(g, minus, L"−", *h.body, false, hoverBar_ == hid, s, col::accent, value > lo);
        DrawButton(g, plus, L"+", *h.body, false, hoverBar_ == hid + 1, s, col::accent, value < hi);
        FillRound(g, val, 6 * s, C(col::bg));
        StrokeRound(g, val, 6 * s, C(col::border, 160));
        Text(g, std::to_wstring(value), *h.body, val, C(col::text), StringAlignmentCenter);
    };
    stepper(geo.rowsLabel, geo.rowsMinus, geo.rowsVal, geo.rowsPlus, L"Zeilen", st.soundRows, 1, kSoundMaxRows, 1);
    stepper(geo.colsLabel, geo.colsMinus, geo.colsVal, geo.colsPlus, L"Spalten", st.soundCols, 1, kSoundMaxCols, 3);
    DrawToggle(g, geo.showKeys, st.soundShowKeys ? 1.0f : 0.0f);
    Text(g, Tr(L"Tastenkürzel auf Knöpfen zeigen"), *h.small, geo.showKeysLabel, hoverBar_ == 5 ? C(col::text) : C(col::sub));
    if (h.popout) {
        DrawToggle(g, RectF(geo.aux.X, geo.aux.Y + 1 * s, 44 * s, 26 * s), st.soundPinned ? 1.0f : 0.0f);
        Text(g, Tr(L"Immer oben"), *h.small, RectF(geo.aux.X + 52 * s, geo.aux.Y, geo.aux.Width - 52 * s, geo.aux.Height), hoverBar_ == 6 ? C(col::text) : C(col::sub));
    } else {
        DrawButton(g, geo.aux, L"", *h.small, false, hoverBar_ == 6, s);
        DrawIcon(g, Icon::Pop, RectF(geo.aux.X + 12 * s, geo.aux.Y + 5 * s, 18 * s, 18 * s), C(col::text), s);
        Text(g, Tr(L"Abkoppeln"), *h.small, RectF(geo.aux.X + 34 * s, geo.aux.Y, geo.aux.Width - 38 * s, geo.aux.Height), C(col::text));
    }

    // ---- Raster --------------------------------------------------------------------------------------------------------------------------------
    {
        const GraphicsState gs = g.Save();
        g.SetClip(geo.gridArea);
        const Font& nameFont = geo.cellW >= 120 * s ? *h.body : geo.cellW >= 70 * s ? *h.small : *h.tiny;
        for (int r = 0; r < geo.rows; ++r) {
            const float top = geo.gridArea.Y + r * (geo.cellH + geo.gap) - scroll_;
            if (top + geo.cellH < geo.gridArea.Y || top > geo.gridArea.Y + geo.gridArea.Height) continue;
            for (int c = 0; c < geo.cols; ++c) {
                const int  slot = r * geo.cols + c;
                const RectF cell(geo.gridArea.X + c * (geo.cellW + geo.gap), top, geo.cellW, geo.cellH);
                const auto& info = snd[slot];
                const bool  hover = hoverSlot_ == slot && !editor_.open;
                const float rad   = std::min(10 * s, geo.cellH * 0.22f);
                if (info.path.empty()) { // leer: dezentes "+"
                    FillRound(g, cell, rad, hover ? C(col::cardHover) : C(col::card));
                    StrokeRound(g, cell, rad, C(hover ? col::accent : col::border, hover ? 200 : 110));
                    const float ps = std::min(geo.cellH, geo.cellW) * 0.34f;
                    DrawIcon(g, Icon::Plus, RectF(cell.X + (cell.Width - ps) / 2, cell.Y + (cell.Height - ps) / 2, ps, ps), C(hover ? col::accent : col::sub, hover ? 255 : 120), s);
                    if (drag_.active && drag_.moved && CellAt(geo_, drag_.curX, drag_.curY) == slot) StrokeRound(g, cell, rad, C(col::accent), 2.0f * s);
                    continue;
                }
                const uint32_t base  = info.color ? info.color : kDefaultTile;
                const bool     light = IsLight(base);
                const bool     lifted = drag_.active && drag_.moved && drag_.from == slot;
                const BYTE     a     = lifted ? 90 : 255;
                FillRoundGrad(g, cell, rad, C(Shade(base, 255, 0.12f), a), C(Shade(base, 0, 0.22f), a));
                if (hover) FillRound(g, cell, rad, C(0xFFFFFF, 26));
                if (flash_[slot] > 0.01f) FillRound(g, cell, rad, C(0xFFFFFF, static_cast<BYTE>(150 * flash_[slot])));
                const Color textCol = light ? C(0x101318) : C(0xFFFFFF);
                const bool  failed  = info.state == Soundboard::State::Failed;
                if (failed) StrokeRound(g, cell, rad, C(col::bad), 2.0f * s);
                else StrokeRound(g, cell, rad, C(Shade(base, 255, 0.35f), 90));
                const float padX = 5 * s;
                const bool  hasKey = st.soundShowKeys && slot < kSoundHotkeys && slot < static_cast<int>(keyText_.size()) && !keyText_[slot].empty();
                const bool  roomy  = geo.cellH >= 50 * s;
                const float bottom = (hasKey || (roomy && info.seconds > 0)) ? 15 * s : 0.0f;
                const RectF nameRect(cell.X + padX, cell.Y + 3 * s, cell.Width - 2 * padX, cell.Height - 6 * s - bottom);
                TextCenterWrap(g, failed ? Tr(L"Nicht lesbar") : info.name, nameFont, nameRect, failed ? C(col::bad) : textCol);
                if (hasKey) { // Tastenkuerzel unten links
                    const RectF kr(cell.X + padX, cell.Y + cell.Height - 15 * s, cell.Width - 2 * padX, 13 * s);
                    Text(g, keyText_[slot], *h.tiny, kr, light ? C(0x101318, 190) : C(0xFFFFFF, 200), StringAlignmentNear);
                }
                if (roomy && info.seconds > 0) { // Dauer unten rechts
                    wchar_t buf[24];
                    swprintf(buf, 24, L"%.1f s", info.seconds);
                    Text(g, buf, *h.tiny, RectF(cell.X + padX, cell.Y + cell.Height - 15 * s, cell.Width - 2 * padX, 13 * s), light ? C(0x101318, 150) : C(0xFFFFFF, 150),
                         StringAlignmentFar);
                }
                if (info.state == Soundboard::State::Loading) { // Ladeanzeige: kreisender Bogen oben rechts
                    Pen pen(C(textCol.GetValue() & 0xFFFFFF, 220), std::max(1.5f, 1.8f * s));
                    pen.SetStartCap(LineCapRound);
                    pen.SetEndCap(LineCapRound);
                    const float d = 10 * s;
                    g.DrawArc(&pen, cell.X + cell.Width - d - 5 * s, cell.Y + 5 * s, d, d, static_cast<REAL>((h.tick * 18) % 360), 250.0f);
                }
            }
        }
        // gezogener Knopf folgt der Maus
        if (drag_.active && drag_.moved && drag_.from >= 0) {
            const auto&    info = snd[drag_.from];
            const uint32_t base = info.color ? info.color : kDefaultTile;
            const RectF    ghost(drag_.curX - geo.cellW / 2, drag_.curY - geo.cellH / 2, geo.cellW, geo.cellH);
            FillRoundGrad(g, ghost, std::min(10 * s, geo.cellH * 0.22f), C(Shade(base, 255, 0.12f), 215), C(Shade(base, 0, 0.22f), 215));
            StrokeRound(g, ghost, std::min(10 * s, geo.cellH * 0.22f), C(col::accent), 2.0f * s);
            TextCenterWrap(g, info.name, nameFont, ghost, IsLight(base) ? C(0x101318) : C(0xFFFFFF));
        }
        g.Restore(gs);
    }
    // Bildlaufleiste
    if (MaxScroll(geo) > 0) {
        const float trackH = geo.gridArea.Height, thumbH = std::max(24 * s, trackH * trackH / geo.content);
        const float thumbY = geo.gridArea.Y + (trackH - thumbH) * (scroll_ / MaxScroll(geo));
        FillRound(g, RectF(geo.gridArea.X + geo.gridArea.Width - 5 * s, thumbY, 4 * s, thumbH), 2 * s, C(col::sub, 120));
    }

    // ---- Fusszeile -----------------------------------------------------------------------------------------------------------------------------
    Text(g, Tr(L"Lautstärke"), *h.small, geo.volLabel, C(col::sub));
    DrawSlider(g, geo.volume, st.soundVolPct / 200.0f, sliderDrag_ == 1 || hoverBar_ == 7, true);
    Text(g, FormatPercent(st.soundVolPct), *h.small, geo.volVal, C(col::text));
    DrawButton(g, geo.stop, Tr(L"Alle stoppen"), *h.small, false, hoverBar_ == 8, s);

    if (!toast_.empty()) {
        const float w = std::min(geo.area.Width - 20 * s, 360.0f * s);
        const RectF tr(geo.area.X + (geo.area.Width - w) / 2, geo.foot.Y - 34 * s, w, 28 * s);
        FillRound(g, tr, 14 * s, C(0x000000, 200));
        Text(g, toast_, *h.small, tr, C(0xFFFFFF), StringAlignmentCenter);
    }

    // ---- Editor ----------------------------------------------------------------------------------------------------------------------------------
    if (editor_.open) {
        const float k  = std::min(1.0f, (area.Height - 12 * s) / (410 * s));
        const float pw = std::min(500.0f * s, area.Width - 24 * s), ph = 410 * s * k;
        geo.panel      = RectF(area.X + (area.Width - pw) / 2, area.Y + (area.Height - ph) / 2, pw, ph);
        const float px = geo.panel.X + 20 * s, inW = pw - 40 * s;
        auto        Y  = [&](float v) { return geo.panel.Y + v * s * k; };
        geo.nameField  = RectF(px, Y(72), inW, 32 * s);
        geo.fileField  = RectF(px, Y(134), inW - 110 * s, 32 * s);
        geo.fileBtn    = RectF(px + inW - 100 * s, Y(134), 100 * s, 32 * s);
        const float sw = std::min(30.0f * s, inW / 12.0f);
        for (int i = 0; i < 12; ++i) geo.swatch[i] = RectF(px + i * sw, Y(196), 26 * s, 26 * s);
        geo.volSlider = RectF(px, Y(254), inW - 64 * s, 22 * s);
        geo.keyBtn    = RectF(px, Y(310), inW, 30 * s);
        geo.delBtn    = RectF(px, Y(358), 110 * s, 30 * s);
        geo.okBtn     = RectF(px + inW - 120 * s, Y(358), 120 * s, 30 * s);
        geo.cancelBtn = RectF(px + inW - 120 * s - 8 * s - 110 * s, Y(358), 110 * s, 30 * s);

        FillRound(g, RectF(area.X, area.Y, area.Width, area.Height), 0, C(0x000000, 150));
        FillRoundGrad(g, geo.panel, 14 * s, C(0x252932), C(col::card));
        StrokeRound(g, geo.panel, 14 * s, C(col::border));
        Text(g, Fmt1(Tr(L"Knopf %s"), std::to_wstring(editor_.slot + 1)), *h.head, RectF(px, Y(14), inW, 30 * s), C(col::text));
        auto label = [&](const wchar_t* t, float yy) { Text(g, Tr(t), *h.small, RectF(px, Y(yy), inW, 16 * s), C(col::sub)); };
        label(L"Name", 54);
        label(L"Datei", 116);
        label(L"Farbe", 178);
        label(L"Lautstärke", 236);
        label(L"Tastenkürzel", 292);

        // Name
        FillRound(g, geo.nameField, 8 * s, C(col::bg));
        StrokeRound(g, geo.nameField, 8 * s, C(col::accent, 230), 1.5f);
        {
            StringFormat fmt(StringFormat::GenericTypographic());
            fmt.SetFormatFlags(fmt.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
            const float tx = geo.nameField.X + 12 * s, ty = geo.nameField.Y + (geo.nameField.Height - 16 * s) / 2;
            RectF       box(0, 0, 0, 0);
            g.MeasureString(editor_.name.c_str(), static_cast<INT>(editor_.name.size()), h.body, PointF(tx, ty), &fmt, &box);
            if (editor_.nameSel && !editor_.name.empty()) FillRound(g, RectF(tx - 2 * s, ty - 1 * s, box.Width + 4 * s, 18 * s), 3 * s, C(col::accent, 120));
            SolidBrush b(editor_.name.empty() ? C(col::sub) : C(col::text));
            const std::wstring shown = editor_.name.empty() ? std::wstring(Tr(L"Name eingeben …")) : editor_.name;
            g.DrawString(shown.c_str(), static_cast<INT>(shown.size()), h.body, PointF(tx, ty), &fmt, &b);
            if ((h.tick / 14) % 2 == 0) {
                Pen caret(C(col::text), std::max(1.2f, s));
                g.DrawLine(&caret, tx + box.Width + 1 * s, ty, tx + box.Width + 1 * s, ty + 16 * s);
            }
        }
        // Datei
        FillRound(g, geo.fileField, 8 * s, C(col::bg));
        StrokeRound(g, geo.fileField, 8 * s, C(col::border));
        {
            const size_t slash = editor_.path.find_last_of(L"\\/");
            const std::wstring shown = editor_.path.empty() ? std::wstring(Tr(L"Keine Datei gewählt")) : (slash == std::wstring::npos ? editor_.path : editor_.path.substr(slash + 1));
            Text(g, shown, *h.small, RectF(geo.fileField.X + 10 * s, geo.fileField.Y, geo.fileField.Width - 20 * s, geo.fileField.Height), editor_.path.empty() ? C(col::sub) : C(col::text));
        }
        DrawButton(g, geo.fileBtn, Tr(L"Wählen …"), *h.small, false, hoverEd_ == 2, s, col::accent);
        // Farbe
        for (int i = 0; i < 12; ++i) {
            const RectF&   r = geo.swatch[i];
            const uint32_t cc = kSwatch[i] ? kSwatch[i] : kDefaultTile;
            FillCircle(g, r.X + r.Width / 2, r.Y + r.Height / 2, r.Width / 2, C(cc));
            if (editor_.color == kSwatch[i]) {
                Pen ring(C(0xFFFFFF), 2.0f * s);
                g.DrawEllipse(&ring, r.X - 3 * s, r.Y - 3 * s, r.Width + 6 * s, r.Height + 6 * s);
            } else if (hoverEd_ == 3 + i) {
                Pen ring(C(0xFFFFFF, 110), 1.5f * s);
                g.DrawEllipse(&ring, r.X - 3 * s, r.Y - 3 * s, r.Width + 6 * s, r.Height + 6 * s);
            }
            if (i == 0) { // "Standard": kleiner Punkt in der Mitte
                FillCircle(g, r.X + r.Width / 2, r.Y + r.Height / 2, 3 * s, C(0xFFFFFF, 200));
            }
        }
        // Lautstaerke
        DrawSlider(g, geo.volSlider, editor_.volPct / 200.0f, sliderDrag_ == 2 || hoverEd_ == 15, true);
        Text(g, FormatPercent(editor_.volPct), *h.small, RectF(geo.volSlider.X + geo.volSlider.Width + 8 * s, geo.volSlider.Y - 3 * s, 56 * s, 28 * s), C(col::text));
        // Tastenkuerzel
        if (editor_.slot < kSoundHotkeys) {
            const std::wstring key = editor_.slot < static_cast<int>(keyText_.size()) ? keyText_[editor_.slot] : L"";
            DrawButton(g, geo.keyBtn, key.empty() ? Tr(L"Kein Kürzel – festlegen …") : key + L"  ·  " + Tr(L"ändern …"), *h.small, false, hoverEd_ == 16, s, col::accent, g_bind != nullptr);
        } else {
            Text(g, Tr(L"Tastenkürzel gibt es für die ersten 64 Knöpfe"), *h.small, geo.keyBtn, C(col::sub));
        }
        // Knoepfe unten
        const bool exists = !Engine::Get().GetSound(editor_.slot).path.empty();
        if (exists) {
            FillRound(g, geo.delBtn, geo.delBtn.Height / 2, hoverEd_ == 17 ? C(col::bad, 230) : C(col::bad, 70));
            Text(g, Tr(L"Entfernen"), *h.small, geo.delBtn, C(0xFFFFFF), StringAlignmentCenter);
        }
        DrawButton(g, geo.cancelBtn, Tr(L"Abbrechen"), *h.small, false, hoverEd_ == 18, s);
        DrawButton(g, geo.okBtn, Tr(L"Speichern"), *h.small, true, hoverEd_ == 19, s, col::accent, !editor_.path.empty());
    }
    geo_ = geo;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Eingaben
// ---------------------------------------------------------------------------------------------------------------------------------------------

bool View::MouseDown(int x, int y, const Host& h)
{
    Engine& en = Engine::Get();
    const Geo& g = geo_;
    if (g.area.Width <= 0 || !In(g.area, x, y)) return false;

    if (editor_.open) {
        switch (HitEditor(g, x, y)) {
        case 0: editor_.nameSel = false; break;
        case 2: PickFile(h); break;
        case 15:
            sliderDrag_ = 2;
            editor_.volPct = SliderValue(g.volSlider, x, 200);
            break;
        case 16:
            if (g_bind) g_bind(editor_.slot);
            keyCacheAt_ = 0;
            break;
        case 17:
            en.SoundSetSlot(editor_.slot, L"");
            editor_.open = false;
            break;
        case 18: editor_.open = false; break;
        case 19: SaveEditor(); break;
        case -1: break; // Klick neben das Fenster: nichts
        default:
            for (int i = 0; i < 12; ++i)
                if (In(g.swatch[i], x, y)) editor_.color = kSwatch[i];
            break;
        }
        return true;
    }

    const Settings st = en.GetSettings();
    if (In(g.rowsMinus, x, y)) { en.SetSoundGrid(st.soundRows - 1, st.soundCols); return true; }
    if (In(g.rowsPlus, x, y)) { en.SetSoundGrid(st.soundRows + 1, st.soundCols); return true; }
    if (In(g.colsMinus, x, y)) { en.SetSoundGrid(st.soundRows, st.soundCols - 1); return true; }
    if (In(g.colsPlus, x, y)) { en.SetSoundGrid(st.soundRows, st.soundCols + 1); return true; }
    if (In(g.showKeys, x, y) || In(g.showKeysLabel, x, y)) { en.SetSoundShowKeys(!st.soundShowKeys); return true; }
    if (In(g.aux, x, y)) {
        if (h.popout) en.SetSoundPinned(!st.soundPinned);
        else popRequest_ = true;
        return true;
    }
    if (In(g.volume, x, y)) {
        sliderDrag_ = 1;
        en.SetSoundVolume(SliderValue(g.volume, x, 200));
        return true;
    }
    if (In(g.stop, x, y)) { en.SoundStop(); return true; }

    const int slot = CellAt(g, x, y);
    if (slot >= 0) {
        if (en.GetSound(slot).path.empty()) {
            OpenEditor(slot);
        } else {
            drag_ = DragState();
            drag_.active = true;
            drag_.from   = slot;
            drag_.startX = drag_.curX = x;
            drag_.startY = drag_.curY = y;
        }
        return true;
    }
    return true; // Klick auf freie Flaeche der Ansicht
}

void View::MouseMove(int x, int y, const Host& h)
{
    const Geo& g = geo_;
    hoverSlot_ = hoverBar_ = hoverEd_ = -1;
    if (g.area.Width <= 0) return;
    if (sliderDrag_ == 1) { Engine::Get().SetSoundVolume(SliderValue(g.volume, x, 200)); return; }
    if (sliderDrag_ == 2) { editor_.volPct = SliderValue(g.volSlider, x, 200); return; }
    if (editor_.open) {
        hoverEd_ = HitEditor(g, x, y);
        return;
    }
    if (drag_.active) {
        drag_.curX = x;
        drag_.curY = y;
        if (!drag_.moved && std::hypot(static_cast<float>(x - drag_.startX), static_cast<float>(y - drag_.startY)) > 6 * h.s) drag_.moved = true;
        return;
    }
    if (In(g.rowsMinus, x, y)) hoverBar_ = 1;
    else if (In(g.rowsPlus, x, y)) hoverBar_ = 2;
    else if (In(g.colsMinus, x, y)) hoverBar_ = 3;
    else if (In(g.colsPlus, x, y)) hoverBar_ = 4;
    else if (In(g.showKeys, x, y) || In(g.showKeysLabel, x, y)) hoverBar_ = 5;
    else if (In(g.aux, x, y)) hoverBar_ = 6;
    else if (In(g.volume, x, y)) hoverBar_ = 7;
    else if (In(g.stop, x, y)) hoverBar_ = 8;
    else hoverSlot_ = CellAt(g, x, y);
}

void View::MouseUp(int x, int y, const Host& h)
{
    sliderDrag_ = 0;
    if (!drag_.active) return;
    const DragState d = drag_;
    drag_             = DragState();
    Engine& en        = Engine::Get();
    if (!d.moved) { // Klick = abspielen
        const auto info = en.GetSound(d.from);
        if (info.state == Soundboard::State::Failed) {
            en.SoundReload(d.from);
            SetToast(Tr(L"Die Datei wird erneut geladen …"));
        } else {
            en.SoundPlay(d.from);
            Flash(d.from);
        }
        return;
    }
    const int target = CellAt(geo_, x, y);
    if (target >= 0 && target != d.from) en.SoundSwap(d.from, target);
}

bool View::RightDown(int x, int y, const Host& h)
{
    if (!In(geo_.area, x, y) || editor_.open) return editor_.open;
    const int slot = CellAt(geo_, x, y);
    if (slot < 0) return false;
    OpenEditor(slot);
    return true;
}

bool View::Wheel(int x, int y, int delta, const Host& h)
{
    if (!In(geo_.area, x, y)) return false;
    if (editor_.open) return true;
    scroll_ = std::clamp(scroll_ - delta / 120.0f * (geo_.cellH + geo_.gap), 0.0f, MaxScroll(geo_));
    return true;
}

bool View::Char(wchar_t ch, const Host& h)
{
    if (!editor_.open) return false;
    if (ch == L'\r') { SaveEditor(); return true; }
    if (ch == 27) { editor_.open = false; return true; }
    if (ch == L'\b') {
        if (editor_.nameSel) editor_.name.clear();
        else if (!editor_.name.empty()) editor_.name.pop_back();
        editor_.nameSel = false;
        editor_.nameEdited = true;
    } else if (ch == 22) { // Strg+V
        const std::wstring paste = presets::Sanitize(ClipboardLine(h.hwnd));
        if (editor_.nameSel) editor_.name.clear();
        editor_.nameSel    = false;
        editor_.name       = presets::Sanitize(editor_.name + paste);
        editor_.nameEdited = true;
    } else if (ch >= 32 && ch != 127 && ch != L'"') {
        if (editor_.nameSel) editor_.name.clear();
        editor_.nameSel = false;
        if (editor_.name.size() < kMaxEditName) editor_.name += ch;
        editor_.nameEdited = true;
    }
    return true;
}

bool View::Key(WPARAM vk, const Host&)
{
    if (!editor_.open) return false;
    if (vk == VK_ESCAPE) { editor_.open = false; return true; }
    if (vk == VK_DELETE && editor_.nameSel) {
        editor_.name.clear();
        editor_.nameSel    = false;
        editor_.nameEdited = true;
    }
    return true;
}

bool View::Drop(int x, int y, const std::vector<std::wstring>& files, const Host& h)
{
    std::vector<std::wstring> audio;
    for (const auto& f : files)
        if (playlist::IsAudioFile(f)) audio.push_back(f);
    const int slot = (editor_.open || !In(geo_.area, x, y)) ? -1 : CellAt(geo_, x, y); // neben die Knoepfe gezogen: freie Felder der Reihe nach
    if (editor_.open) { // auf den offenen Editor gezogen: ersetzt die Datei
        if (!audio.empty()) {
            editor_.path = audio[0];
            if (!editor_.nameEdited) editor_.name = FileTitle(editor_.path);
        }
        return true;
    }
    if (audio.size() == 1 && slot >= 0) OpenEditor(slot, audio[0]);
    else Assign(audio.empty() ? files : audio, slot);
    return true;
}

} // namespace sbv
