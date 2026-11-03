
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
// Reiter "Voicechanger" im Hauptfenster - der Spielbereich fuer den Voicechanger-Entwickler. Die Seite ist bewusst in dieser eigenen Datei gekapselt: hier wird
// gezeichnet und bedient, ohne dass config_dialog.cpp angefasst werden muss. Die Klangverarbeitung selbst gehoert nach voicechanger.{h,cpp}.
//
// Verdrahtung in config_dialog.cpp (nur diese Stellen kennen die Seite):
//   Paint:        vcpage::Page::Paint(...)   im Zweig "page == 4"
//   Maus/Tasten:  Page::MouseDown/MouseMove/MouseUp/Wheel/Char/Key  (noch nicht angeschlossen - dort, wo "page == 3" die Soundboard-Ansicht bedient, gleich
//                 daneben fuer "page == 4" aufrufen; die Signaturen sind wie bei sbv::View)
// Koordinaten sind die des Hauptfensters (Pixel, bereits mit der Skalierung `s` multipliziert).
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

    // Eingaben (Client-Koordinaten des Hauptfensters); true = verbraucht. Hier eigene Bedienelemente anschliessen.
    bool MouseDown(int x, int y, float s) { (void)x; (void)y; (void)s; return false; }
    void MouseMove(int x, int y, float s) { (void)x; (void)y; (void)s; }
    void MouseUp(int x, int y, float s) { (void)x; (void)y; (void)s; }
    bool Wheel(int x, int y, int delta, float s) { (void)x; (void)y; (void)delta; (void)s; return false; }
    bool Char(wchar_t ch) { (void)ch; return false; }
    bool Key(WPARAM vk) { (void)vk; return false; }
};

} // namespace vcpage
