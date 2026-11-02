#pragma once
// Now-Playing: aus dem Fenstertitel einer App den Titel des laufenden Stuecks gewinnen. Reine Logik ohne Windows-Abhaengigkeit (testbar).
#include <cwctype>
#include <string>

namespace nowplaying {

inline std::wstring Lower(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

inline bool EndsWith(const std::wstring& s, const std::wstring& suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

inline std::wstring Trim(const std::wstring& s)
{
    const size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    return s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
}

// Fenstertitel -> Titel des Stuecks; leer, wenn kein Titel erkennbar ist (Pause, nur Programmname, Standardtitel).
//   "Artist - Song"                        (Spotify, spielend)    -> unveraendert
//   "Spotify Premium" / "Spotify Free"     (Spotify, pausiert)    -> leer
//   "(3) Video - YouTube - Google Chrome"  (Browser)              -> "Video - YouTube"   (Zaehler und Browsername entfernt)
inline std::wstring CleanTrackTitle(std::wstring title, const std::wstring& exeLower)
{
    title = Trim(title);
    // fuehrender Benachrichtigungszaehler "(3) "
    if (title.size() > 3 && title[0] == L'(') {
        size_t i = 1;
        while (i < title.size() && std::iswdigit(title[i])) ++i;
        if (i > 1 && i - 1 <= 3 && i + 1 < title.size() && title[i] == L')' && title[i + 1] == L' ') title = Trim(title.substr(i + 1)); // hoechstens 3 Ziffern: "(2023) Live" bleibt
    }
    // Browsername am Ende
    static const wchar_t* const kBrowsers[] = {L"Google Chrome", L"Microsoft Edge", L"Mozilla Firefox", L"Brave", L"Opera", L"Vivaldi", L"Chromium"};
    for (const wchar_t* b : kBrowsers)
        for (const wchar_t* dash : {L" - ", L" – ", L" — "}) {
            const std::wstring suffix = std::wstring(dash) + b;
            if (EndsWith(title, suffix)) {
                title = Trim(title.substr(0, title.size() - suffix.size()));
                break;
            }
        }
    const std::wstring low = Lower(title);
    // Standardtitel: Programmname allein bedeutet "kein Stueck"
    static const wchar_t* const kIdle[] = {L"spotify", L"spotify free", L"spotify premium", L"youtube", L"youtube music", L"tidal", L"deezer", L"soundcloud", L"vlc media player", L"foobar2000", L"winamp"};
    for (const wchar_t* idle : kIdle)
        if (low == idle) return L"";
    (void)exeLower;
    return title;
}

} // namespace nowplaying
