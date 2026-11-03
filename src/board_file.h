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
