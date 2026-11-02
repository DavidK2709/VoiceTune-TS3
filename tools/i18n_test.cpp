// Selbsttest der Uebersetzungstabelle: eindeutige Schluessel, gleiche Platzhalter/Umbrueche/Randleerzeichen, kein Deutsch im Englischen, Rueckfall, Sprachumschaltung.
#include <cstdio>
#include <cwchar>
#include <set>
#include <string>

#include "i18n.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static int Count(const std::wstring& s, const std::wstring& what)
{
    int n = 0;
    for (size_t p = 0; (p = s.find(what, p)) != std::wstring::npos; p += what.size()) ++n;
    return n;
}

int main()
{
    size_t      count = 0;
    const auto* table = i18n::Table(&count);
    CHECK(count > 150, "Tabelle hat %zu Eintraege", count);

    // Texte, die in beiden Sprachen gleich lauten duerfen (Eigennamen, Fachwoerter)
    const std::set<std::wstring> same = {L"Equalizer", L"Soundboard", L"Sounds", L"Loudness", L"Bass+", L"Hotkeys …"};
    std::set<std::wstring>       keys;
    for (size_t i = 0; i < count; ++i) {
        const std::wstring de = table[i].de, en = table[i].en;
        CHECK(!de.empty() && !en.empty(), "Eintrag %zu leer", i);
        CHECK(keys.insert(de).second, "doppelter Schluessel '%ls'", de.c_str());
        CHECK(de != en || same.count(de), "'%ls' ist nicht uebersetzt", de.c_str());
        CHECK(Count(de, L"%s") == Count(en, L"%s"), "Platzhalter %%s passen nicht: '%ls'", de.c_str());
        CHECK(Count(de, L"\n") == Count(en, L"\n"), "Zeilenumbrueche passen nicht: '%ls'", de.c_str());
        CHECK((de.front() == L' ') == (en.front() == L' ') && (de.back() == L' ') == (en.back() == L' '), "Randleerzeichen passen nicht: '%ls' / '%ls'", de.c_str(), en.c_str());
        for (wchar_t ch : en) CHECK(!(ch == L'ä' || ch == L'ö' || ch == L'ü' || ch == L'Ä' || ch == L'Ö' || ch == L'Ü' || ch == L'ß'), "deutsches Zeichen im Englischen: '%ls'", en.c_str());
    }

    // Deutsch: unveraendert (sogar derselbe Zeiger)
    i18n::SetSetting(1);
    CHECK(i18n::Current() == i18n::Lang::German && i18n::Setting() == 1, "Deutsch gewaehlt");
    const wchar_t* de = L"Löschen";
    CHECK(i18n::Tr(de) == de && std::wstring(i18n::Tr(L"Abbrechen")) == L"Abbrechen", "Deutsch bleibt Deutsch");
    CHECK(i18n::Tr8("Einstellungen...") == "Einstellungen...", "Tr8 Deutsch");

    // English
    i18n::SetSetting(2);
    CHECK(i18n::Current() == i18n::Lang::English, "English gewaehlt");
    CHECK(std::wstring(i18n::Tr(L"Löschen")) == L"Delete" && std::wstring(i18n::Tr(L"Abbrechen")) == L"Cancel", "Uebersetzung");
    CHECK(std::wstring(i18n::Tr(L"Löschen")) == L"Delete", "zweiter Aufruf (Zeiger-Cache) gleich");
    CHECK(i18n::Tr(std::wstring(L"Umbenennen")) == L"Rename", "Tr(wstring)");
    CHECK(i18n::Tr8("Einstellungen...") == "Settings...", "Tr8 English");
    CHECK(i18n::Tr8("♪  MUSIK AN") == "♪  MUSIC ON", "Tr8 mit Sonderzeichen: '%s'", i18n::Tr8("♪  MUSIK AN").c_str());
    CHECK(i18n::Tr8("VoiceTune: Sound 3 abspielen") == "VoiceTune: Play sound 3", "Hotkey-Beschreibung");

    // Rueckfall: unbekannte Texte bleiben, auch dynamische Namen (Profilnamen, App-Namen)
    const wchar_t* unknown = L"Mein ganz eigenes Profil";
    CHECK(i18n::Tr(unknown) == unknown, "unbekannter Text: derselbe Zeiger");
    CHECK(i18n::Tr(std::wstring(L"Spotify")) == L"Spotify" && i18n::Tr8("Daft Punk - Get Lucky") == "Daft Punk - Get Lucky", "Namen bleiben");
    CHECK(i18n::Tr(static_cast<const wchar_t*>(nullptr)) == nullptr && i18n::Tr8(nullptr).empty(), "nullptr");

    // Eigene Filternamen wie mitgelieferte duerfen nicht versehentlich uebersetzt werden, wenn sie anders heissen
    CHECK(std::wstring(i18n::Tr(L"Flach")) == L"Flat" && std::wstring(i18n::Tr(L"Flach 2")) == L"Flach 2", "Preset-Namen");

    // Mehrzahl-Schluessel der Titelanzeige: ' Titel' (1) und ' Titel ' (mehrere)
    CHECK(std::wstring(i18n::Tr(L" Titel")) == L" track" && std::wstring(i18n::Tr(L" Titel ")) == L" tracks ", "Einzahl/Mehrzahl");

    // Ungueltige Einstellung wird auf Automatik zurueckgesetzt
    i18n::SetSetting(7);
    CHECK(i18n::Setting() == 0, "ungueltig -> automatisch");
    i18n::SetSetting(-1);
    CHECK(i18n::Setting() == 0, "negativ -> automatisch");

    i18n::SetSetting(1);
    std::printf(g_fail ? "%d FEHLER\n" : "i18n_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
