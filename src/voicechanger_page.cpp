
/*
 * VoiceTune-TS3 - Voicechanger-Plugin für TeamSpeak 3
 * Copyright (C) 2026  David K., Tony S.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "voicechanger_page.h"

#include <cstdio>

#include "engine.h"
#include "i18n.h"
#include "ui_widgets.h"

using namespace ui;

namespace vcpage {

namespace {
enum Btn { kBtnSave, kBtnRename, kBtnDelete, kBtnToggle, kBtnMirror, kBtnInvert };

bool In(const RectF& r, int x, int y) { return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height; }

// "+3,5" (deutsches Komma), ganze Werte ohne Nachkommastelle
std::wstring Signed(float v, int decimals)
{
    wchar_t buf[32];
    if (std::fabs(v) < 0.05f) return L"0";
    swprintf(buf, 32, L"%+.*f", decimals, v);
    std::wstring out = buf;
    if (i18n::Current() == i18n::Lang::German)
        for (auto& ch : out)
            if (ch == L'.') ch = L',';
    return out;
}

std::wstring ClipboardLine()
{
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            for (; *p && *p != L'\r' && *p != L'\n'; ++p) out.push_back(*p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

// Gewaehlte Stimme mit ihren gespeicherten Werten; nullptr = keine (mehr)
const voicepresets::Voice* Selected(const Settings& st)
{
    if (const auto* v = voicepresets::FindBuiltin(st.voicePresetName)) return v;
    return st.voicePresets.Find(st.voicePresetName);
}
} // namespace

// Die Karte ist 788 x 516 gross: links die Stimmen, rechts Ein/Aus, Regler und Abhoeren
Page::Geo Page::MakeGeo(const RectF& a, float s) const
{
    auto R = [&](float x, float y, float w, float h) { return RectF(a.X + x * s, a.Y + y * s, w * s, h * s); };
    Geo  g;
    g.rowH      = 30 * s;
    g.list      = R(16, 44, 224, 352);
    g.saveBtn   = R(16, 404, 224, 30);
    g.renameBtn = R(16, 442, 110, 30);
    g.deleteBtn = R(130, 442, 110, 30);

    const float x = 272, w = 500;
    g.toggleRow = R(x, 8, w, 36);
    g.toggle    = R(x + w - 48, 13, 48, 26);
    for (int i = 0; i < kMonitor; ++i) g.slider[i] = R(x, 72 + i * 50.0f, w, 22);
    const float colW = (w - 20) / 2;
    g.mirrorRow  = R(x, 244, colW, 44);
    g.mirror     = R(x + colW - 48, 244, 48, 26);
    g.invertRow  = R(x + colW + 20, 244, colW, 44);
    g.invert     = R(x + w - 48, 244, 48, 26);
    g.longWinRow = R(x, 294, w, 44);
    g.longWin    = R(x + w - 48, 294, 48, 26);
    g.slider[kMonitor] = R(x + 14, 394, w - 28, 22);
    return g;
}

std::wstring Page::RowName(int row) const
{
    const auto& builtins = voicepresets::Builtins();
    if (row < static_cast<int>(builtins.size())) return builtins[row].name;
    const Settings st = Engine::Get().GetSettings();
    const size_t   i  = static_cast<size_t>(row) - builtins.size();
    return i < st.voicePresets.items.size() ? st.voicePresets.items[i].name : L"";
}

float Page::MaxScroll(const Geo& g) const
{
    const size_t rows = voicepresets::Builtins().size() + Engine::Get().GetSettings().voicePresets.items.size();
    return std::max(0.0f, static_cast<float>(rows) * g.rowH - g.list.Height);
}

void Page::SetToast(const std::wstring& t, bool bad)
{
    toast_      = t;
    toastBad_   = bad;
    toastUntil_ = GetTickCount64() + 3500;
}

void Page::Paint(Graphics& g, const RectF& area, float s, const Fonts& f)
{
    area_    = area;
    painted_ = true;
    const Geo      geo = MakeGeo(area, s);
    Engine&        en  = Engine::Get();
    const Settings st  = en.GetSettings();
    const bool     on  = en.VoiceEnabled();
    auto R = [&](float x, float y, float w, float h) { return RectF(area.X + x * s, area.Y + y * s, w * s, h * s); };

    // ---- Links: Stimmen
    DrawIcon(g, Icon::Person, R(16, 14, 18, 18), C(col::accent), s);
    Text(g, Tr(L"Stimmen"), *f.head, R(40, 10, 200, 26), C(col::text));

    const auto&                builtins = voicepresets::Builtins();
    const voicepresets::Voice* selected = Selected(st);
    const bool                 modified = selected && !voicepresets::Same(selected->params, st.voice);
    const bool                 isOwn    = selected && !voicepresets::FindBuiltin(selected->name);
    scroll_ = std::clamp(scroll_, 0.0f, MaxScroll(geo));
    {
        GraphicsState state = g.Save();
        g.SetClip(geo.list);
        const int total = static_cast<int>(builtins.size() + st.voicePresets.items.size());
        for (int i = 0; i < total; ++i) {
            const bool         builtin = i < static_cast<int>(builtins.size());
            const std::wstring name    = builtin ? builtins[i].name : st.voicePresets.items[i - builtins.size()].name;
            const RectF        row(geo.list.X, geo.list.Y + static_cast<float>(i) * geo.rowH - scroll_, geo.list.Width - 6 * s, geo.rowH - 3 * s);
            const bool         sel = selected && presets::EqualNoCase(selected->name, name);
            if (sel) FillRound(g, row, 8 * s, C(col::accent, 210));
            else if (hoverRow_ == i) FillRound(g, row, 8 * s, C(col::cardHover));
            RectF text(row.X + 12 * s, row.Y, row.Width - 24 * s, row.Height);
            Text(g, builtin ? Tr(name) : name, *f.small, text, sel ? C(0xFFFFFF) : C(col::text));
            if (sel && modified) FillCircle(g, row.X + row.Width - 12 * s, row.Y + row.Height / 2, 3.5f * s, C(col::warn));
        }
        g.Restore(state);
    }

    if (edit_) {
        FillRound(g, geo.saveBtn, 8 * s, C(col::bg));
        StrokeRound(g, geo.saveBtn, 8 * s, C(col::accent, 230), 1.5f);
        const bool blink = (GetTickCount64() / 500) % 2 == 0;
        RectF      tr(geo.saveBtn.X + 10 * s, geo.saveBtn.Y, geo.saveBtn.Width - 20 * s, geo.saveBtn.Height);
        if (editSelectAll_ && !editText_.empty()) {
            RectF box;
            g.MeasureString(editText_.c_str(), static_cast<INT>(editText_.size()), f.small, PointF(tr.X, tr.Y), &box);
            FillRound(g, RectF(tr.X, tr.Y + 5 * s, std::min(box.Width, tr.Width), tr.Height - 10 * s), 3 * s, C(col::accent, 150));
        }
        Text(g, editText_ + (blink && !editSelectAll_ ? L"|" : L""), *f.small, tr, C(col::text));
        TextWrap(g, Tr(L"Name eingeben, Enter speichert, Esc bricht ab"), *f.tiny, RectF(geo.renameBtn.X, geo.renameBtn.Y, geo.list.Width, geo.renameBtn.Height), C(col::sub));
    } else {
        DrawButton(g, geo.saveBtn, Tr(L"Speichern als …"), *f.small, false, hoverBtn_ == kBtnSave, s);
        DrawButton(g, geo.renameBtn, Tr(L"Umbenennen"), *f.small, false, hoverBtn_ == kBtnRename, s, col::accent, isOwn);
        const bool armed = GetTickCount64() < deleteArmUntil_;
        DrawButton(g, geo.deleteBtn, armed ? Tr(L"Wirklich löschen?") : Tr(L"Löschen"), *f.small, armed, hoverBtn_ == kBtnDelete, s, col::bad, isOwn);
    }
    if (GetTickCount64() < toastUntil_) Text(g, toast_, *f.tiny, R(16, 478, 224, 28), C(toastBad_ ? col::warn : col::ok));
    else if (modified) Text(g, Tr(L"(geändert)"), *f.tiny, R(16, 478, 224, 28), C(col::warn));

    // Trennlinie zwischen den Spalten
    {
        Pen line(C(col::border), std::max(1.0f, s));
        g.DrawLine(&line, area.X + 256 * s, area.Y + 14 * s, area.X + 256 * s, area.Y + 502 * s);
    }

    // ---- Rechts: Ein/Aus
    toggleAnim_ += ((on ? 1.0f : 0.0f) - toggleAnim_) * 0.3f;
    DrawIcon(g, Icon::Voice, R(272, 14, 22, 22), C(on ? col::ok : col::sub), s);
    Text(g, on ? Tr(L"Voicechanger aktiv") : Tr(L"Voicechanger aus – du klingst wie immer"), *f.head, R(300, 8, 400, 36), C(on ? col::ok : col::text));
    DrawToggle(g, geo.toggle, toggleAnim_);

    // ---- Regler
    const VoiceParams& v = st.voice;
    struct Row {
        const wchar_t* label;
        std::wstring   value;
        float          frac;
        bool           middle;
    };
    const Row rows[kMonitor] = {
        {L"Tonhöhe (Pitch)", Signed(v.pitch, 1) + Tr(L" Halbtöne"), (v.pitch + 12.0f) / 24.0f, true},
        {L"Formant (Stimmcharakter)", Signed(v.formant, 1) + Tr(L" Halbtöne"), (v.formant + 12.0f) / 24.0f, true},
        {L"Lautstärke (Gain)", Signed(v.gainDb, 0) + L" dB", (v.gainDb + 24.0f) / 48.0f, true},
        {L"Dry/Wet (Original ↔ Effekt)", std::to_wstring(v.wetPct) + L" %", static_cast<float>(v.wetPct) / 100.0f, false},
    };
    for (int i = 0; i < kMonitor; ++i) {
        const RectF& r = geo.slider[i];
        Text(g, Tr(rows[i].label), *f.small, RectF(r.X + 8 * s, r.Y - 20 * s, 300 * s, 20 * s), C(col::sub));
        Text(g, rows[i].value, *f.small, RectF(r.X + r.Width - 188 * s, r.Y - 20 * s, 180 * s, 20 * s), C(col::text), StringAlignmentFar);
        DrawSlider(g, r, rows[i].frac, drag_ == i, rows[i].middle);
    }

    // ---- Spiegeln / Invertieren
    mirrorAnim_ += ((v.mirror ? 1.0f : 0.0f) - mirrorAnim_) * 0.3f;
    invertAnim_ += ((v.invert ? 1.0f : 0.0f) - invertAnim_) * 0.3f;
    longAnim_ += ((st.voiceLongWindow ? 1.0f : 0.0f) - longAnim_) * 0.3f;
    auto toggleRow = [&](const RectF& row, const RectF& sw, const wchar_t* title, const wchar_t* hint, float anim) {
        Text(g, Tr(title), *f.small, RectF(row.X + 8 * s, row.Y, row.Width - 64 * s, 26 * s), C(col::text));
        Text(g, Tr(hint), *f.tiny, RectF(row.X + 8 * s, row.Y + 26 * s, row.Width - 8 * s, 18 * s), C(col::sub));
        DrawToggle(g, sw, anim);
    };
    toggleRow(geo.mirrorRow, geo.mirror, L"Stimme spiegeln", L"Tief wird hoch und umgekehrt", mirrorAnim_);
    toggleRow(geo.invertRow, geo.invert, L"Polarität invertieren", L"Nur im Mix mit dem Original hörbar", invertAnim_);
    toggleRow(geo.longWinRow, geo.longWin, L"Tiefe Stimmen: bessere Qualität", L"Sauberer unter ca. 150 Hz, dafür 43 statt 21 ms Latenz", longAnim_);

    // ---- Abhoeren
    const RectF mon = R(272, 346, 500, 118);
    FillRound(g, mon, 10 * s, C(col::bg, 120));
    DrawIcon(g, Icon::Headphones, R(286, 354, 20, 20), C(st.voiceMonitorPct > 0 ? col::accent : col::sub), s);
    Text(g, Tr(L"Selbst abhören"), *f.small, R(312, 352, 300, 24), C(col::text));
    Text(g, std::to_wstring(st.voiceMonitorPct) + L" %", *f.small, R(600, 352, 158, 24), C(col::text), StringAlignmentFar);
    DrawSlider(g, geo.slider[kMonitor], static_cast<float>(st.voiceMonitorPct) / 100.0f, drag_ == kMonitor, false);
    Text(g, Tr(L"Nur mit Kopfhörern – über Lautsprecher gibt es eine Rückkopplung."), *f.tiny, R(294, 420, 460, 18), C(st.voiceMonitorPct > 0 ? col::warn : col::sub));
    Text(g, st.voiceLongWindow ? Tr(L"Du hörst dich etwa 90 ms verzögert. Für die anderen beträgt die Effekt-Latenz ca. 43 ms.") : Tr(L"Du hörst dich etwa 70 ms verzögert. Für die anderen beträgt die Effekt-Latenz ca. 21 ms."),
         *f.tiny, R(294, 440, 460, 18), C(col::sub));

    if (!en.MicAlive())
        TextWrap(g, Tr(L"TS3 liefert gerade kein Mikrofon: verbinde dich oder nutze „Test starten“ in den Aufnahme-Einstellungen."), *f.tiny, R(280, 470, 484, 38), C(col::warn));
}

void Page::SetSlider(int which, int x)
{
    const Geo g = MakeGeo(area_, area_.Width / 788.0f);
    Engine&   en = Engine::Get();
    if (which == kMonitor) {
        en.SetVoiceMonitor(SliderValue(g.slider[kMonitor], x, 100, 1));
        return;
    }
    VoiceParams v = en.GetSettings().voice;
    switch (which) {
    case kPitch:   v.pitch   = -12.0f + static_cast<float>(SliderValue(g.slider[kPitch], x, 48, 1)) * 0.5f; break; // Halbton-Haelften
    case kFormant: v.formant = -12.0f + static_cast<float>(SliderValue(g.slider[kFormant], x, 48, 1)) * 0.5f; break;
    case kGain:    v.gainDb  = -24.0f + static_cast<float>(SliderValue(g.slider[kGain], x, 48, 1)); break;
    case kWet:     v.wetPct  = SliderValue(g.slider[kWet], x, 100, 1); break;
    default: break;
    }
    en.SetVoiceParams(v);
}

void Page::BeginEdit(bool rename, const std::wstring& name)
{
    edit_          = true;
    editRename_    = rename;
    editFrom_      = name;
    editText_      = name;
    editSelectAll_ = true;
}

void Page::CommitEdit()
{
    Engine&            en = Engine::Get();
    const PresetResult r  = editRename_ ? en.RenameVoicePreset(editFrom_, editText_) : en.SaveVoicePreset(editText_);
    switch (r) {
    case PresetResult::Ok:
    case PresetResult::Updated:
        edit_ = false;
        SetToast(Tr(L"Stimme gespeichert"), false);
        break;
    case PresetResult::Empty:    SetToast(Tr(L"Bitte einen Namen eingeben"), true); break;
    case PresetResult::Reserved: SetToast(Tr(L"Der Name gehört einer mitgelieferten Stimme"), true); break;
    case PresetResult::Exists:   SetToast(Tr(L"Den Namen gibt es schon"), true); break;
    case PresetResult::Full:     SetToast(Tr(L"Die Liste ist voll"), true); break;
    default: edit_ = false; break;
    }
}

bool Page::MouseDown(int x, int y, float s)
{
    if (!painted_) return false;
    const Geo g = MakeGeo(area_, s);
    if (!In(area_, x, y)) return false;
    Engine&        en = Engine::Get();
    const Settings st = en.GetSettings();

    if (edit_) { // die Eingabe gehoert zum Feld: ein Klick daneben bricht sie ab
        if (In(g.saveBtn, x, y)) return true;
        edit_ = false;
    }
    const voicepresets::Voice* selected = Selected(st);
    const bool                 isOwn    = selected && !voicepresets::FindBuiltin(selected->name);
    const bool                 armed    = GetTickCount64() < deleteArmUntil_;
    deleteArmUntil_ = 0; // "Wirklich loeschen?" verfaellt bei jedem anderen Klick

    if (In(g.list, x, y)) {
        const int row = static_cast<int>((static_cast<float>(y) - g.list.Y + scroll_) / g.rowH);
        const std::wstring name = RowName(row);
        if (!name.empty()) en.ApplyVoicePreset(name);
        return true;
    }
    if (In(g.saveBtn, x, y)) {
        BeginEdit(false, isOwn ? selected->name : st.voicePresets.SuggestName());
        return true;
    }
    if (In(g.renameBtn, x, y)) {
        if (isOwn) BeginEdit(true, selected->name);
        else SetToast(Tr(L"Mitgelieferte Stimmen lassen sich nicht ändern – speichere eine Kopie"), true);
        return true;
    }
    if (In(g.deleteBtn, x, y)) {
        if (!isOwn) SetToast(Tr(L"Mitgelieferte Stimmen lassen sich nicht ändern – speichere eine Kopie"), true);
        else if (armed) en.DeleteVoicePreset(selected->name);
        else deleteArmUntil_ = GetTickCount64() + 3000;
        return true;
    }
    if (In(g.toggleRow, x, y)) {
        en.SetVoiceEnabled(!en.VoiceEnabled());
        return true;
    }
    if (In(g.mirrorRow, x, y)) {
        VoiceParams v = st.voice;
        v.mirror      = !v.mirror;
        en.SetVoiceParams(v);
        return true;
    }
    if (In(g.invertRow, x, y)) {
        VoiceParams v = st.voice;
        v.invert      = !v.invert;
        en.SetVoiceParams(v);
        return true;
    }
    if (In(g.longWinRow, x, y)) {
        en.SetVoiceLongWindow(!st.voiceLongWindow);
        return true;
    }
    for (int i = 0; i < kSliders; ++i)
        if (In(g.slider[i], x, y)) {
            drag_ = i;
            SetSlider(i, x);
            return true;
        }
    return true; // Klicks auf die Karte gehen nicht "durch"
}

void Page::MouseMove(int x, int y, float s)
{
    if (drag_ >= 0) {
        SetSlider(drag_, x);
        return;
    }
    hoverRow_ = hoverBtn_ = -1;
    if (!painted_ || x < 0) return;
    const Geo g = MakeGeo(area_, s);
    if (In(g.list, x, y)) {
        const int row = static_cast<int>((static_cast<float>(y) - g.list.Y + scroll_) / g.rowH);
        if (!RowName(row).empty()) hoverRow_ = row;
    } else if (In(g.saveBtn, x, y)) hoverBtn_ = kBtnSave;
    else if (In(g.renameBtn, x, y)) hoverBtn_ = kBtnRename;
    else if (In(g.deleteBtn, x, y)) hoverBtn_ = kBtnDelete;
}

bool Page::Wheel(int x, int y, int delta, float s)
{
    if (!painted_) return false;
    const Geo g = MakeGeo(area_, s);
    if (!In(g.list, x, y)) return false;
    scroll_ = std::clamp(scroll_ - static_cast<float>(delta) / 120.0f * g.rowH, 0.0f, MaxScroll(g));
    return true;
}

bool Page::Char(wchar_t ch)
{
    if (!edit_) return false;
    if (ch == L'\r') CommitEdit();
    else if (ch == 27) edit_ = false;
    else if (ch == L'\b') {
        if (editSelectAll_) editText_.clear();
        else if (!editText_.empty()) editText_.pop_back();
        editSelectAll_ = false;
    } else if (ch == 22) { // Strg+V
        const std::wstring paste = presets::Sanitize(ClipboardLine());
        if (editSelectAll_) editText_.clear();
        editSelectAll_ = false;
        editText_      = presets::Sanitize(editText_ + paste);
    } else if (ch >= 32 && ch != 127 && ch != L'"') {
        if (editSelectAll_) editText_.clear();
        editSelectAll_ = false;
        if (editText_.size() < presets::kMaxName) editText_ += ch;
    }
    return true;
}

bool Page::Key(WPARAM vk)
{
    if (!edit_) return false;
    if (vk == VK_ESCAPE) edit_ = false;
    else if (vk == VK_DELETE && editSelectAll_) {
        editText_.clear();
        editSelectAll_ = false;
    }
    return true;
}

} // namespace vcpage
