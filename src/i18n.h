#pragma once
// Oberflaechensprache: Der Quelltext ist Deutsch; Tr("deutscher Text") liefert bei Sprache English die Uebersetzung aus der Tabelle (i18n.cpp) und faellt
// ohne Eintrag auf den deutschen Text zurueck. Die Engine behaelt ihre deutschen Statustexte (die Oberflaeche wertet sie aus), uebersetzt wird erst bei der Anzeige.
#include <cstddef>
#include <string>

namespace i18n {

enum class Lang { German, English };

// 0 = automatisch (Windows-Anzeigesprache nicht Deutsch -> English), 1 = Deutsch, 2 = English
void SetSetting(int setting);
int  Setting();
Lang Current();

const wchar_t* Tr(const wchar_t* de);     // Zeiger bleibt gueltig (Tabelle bzw. das Argument selbst)
std::string    Tr8(const char* deUtf8);   // fuer UTF-8-Texte (Chat, Menue)
std::wstring   Tr(const std::wstring& de); // fuer zusammengesetzte Texte: nur wenn der ganze Text in der Tabelle steht

struct Pair {
    const wchar_t* de;
    const wchar_t* en;
};
const Pair* Table(size_t* count); // fuer Tests

} // namespace i18n

using i18n::Tr;
using i18n::Tr8;
