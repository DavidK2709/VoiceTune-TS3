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
// Soundboard-Datei (.vtboard): Raster und Knoepfe zum Teilen/Sichern. UTF-8, INI-Stil, Pfade relativ zur Datei (liegen sie auf einem anderen Laufwerk, bleiben sie absolut).
//   [VoiceTuneBoard]
//   Version=1
//   Rows=2
//   Cols=4
//   Slot1=C0392B|100|..\Sounds\hupe.wav|Hupe
#include <string>
#include <vector>

#include "settings.h"

namespace board {

struct Data {
    int                    rows = 2, cols = 4;
    std::vector<SoundSlot> slots = std::vector<SoundSlot>(kSoundSlots);
};

// Pfad `target` relativ zum Ordner `baseDir` ("..\x\y.wav"); anderes Laufwerk oder kein absoluter Pfad: unveraendert
std::wstring RelativePath(const std::wstring& baseDir, const std::wstring& target);
// Relativen Pfad gegen `baseDir` aufloesen und "." / ".." bereinigen; absolute Pfade bleiben
std::wstring ResolvePath(const std::wstring& baseDir, const std::wstring& rel);

// fileDir = Ordner, in dem die Datei liegt
std::string FormatBoard(const Data& d, const std::wstring& fileDir);
bool        ParseBoard(const std::string& utf8, const std::wstring& fileDir, Data* out);

} // namespace board
