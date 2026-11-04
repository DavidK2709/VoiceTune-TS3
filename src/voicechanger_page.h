
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

#pragma once
// Reiter "Voicechanger" im Hauptfenster: links die Stimmen (mitgelieferte und eigene), rechts Ein/Aus, die Regler und das Abhoeren. Die Seite haelt nur Anzeigezustand,
// die Daten liegen in der Engine; die Klangverarbeitung steht in voicechanger.{h,cpp}. Koordinaten sind die des Hauptfensters (Pixel, bereits mit der Skalierung `s` multipliziert).
#include <windows.h>

#include <objidl.h>

#include <string>

#include "ui_theme.h"

namespace vcpage {

struct Fonts {
    const Gdiplus::Font *title, *head, *body, *small, *tiny;
};

class Page {
public:
    void Paint(Gdiplus::Graphics& g, const Gdiplus::RectF& area, float s, const Fonts& f);

    // Eingaben (Client-Koordinaten des Hauptfensters); true = verbraucht.
    bool MouseDown(int x, int y, float s);
    void MouseMove(int x, int y, float s);
    void MouseUp() { drag_ = -1; }
    bool Wheel(int x, int y, int delta, float s);
    bool Char(wchar_t ch);
    bool Key(WPARAM vk);

    bool Dragging() const { return drag_ >= 0; } // ein Regler wird gezogen (der Host haelt die Maus fest)
    bool WantsKeys() const { return edit_; }     // die Namenseingabe ist offen
    void CancelEdit() { edit_ = false; }

private:
    enum Slider { kPitch, kFormant, kGain, kWet, kMonitor, kSliders };
    struct Geo {
        Gdiplus::RectF list, saveBtn, renameBtn, deleteBtn, toggle, toggleRow, slider[kSliders], mirror, mirrorRow, invert, invertRow, longWin, longWinRow;
        float          rowH = 0;
    };
    Geo   MakeGeo(const Gdiplus::RectF& area, float s) const;
    void  SetSlider(int which, int x);
    void  BeginEdit(bool rename, const std::wstring& name);
    void  CommitEdit();
    void  SetToast(const std::wstring& t, bool bad);
    float MaxScroll(const Geo& g) const;
    std::wstring RowName(int row) const;

    Gdiplus::RectF area_;
    bool           painted_ = false;
    int            hoverRow_ = -1, hoverBtn_ = -1;
    int            drag_ = -1;
    float          scroll_ = 0, toggleAnim_ = 0, mirrorAnim_ = 0, invertAnim_ = 0, longAnim_ = 0;

    bool         edit_ = false, editRename_ = false, editSelectAll_ = false;
    std::wstring editText_, editFrom_;
    ULONGLONG    deleteArmUntil_ = 0;
    std::wstring toast_;
    bool         toastBad_   = false;
    ULONGLONG    toastUntil_ = 0;
};

} // namespace vcpage
