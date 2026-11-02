// Selbsttest der Titelbereinigung fuer Now-Playing.
#include <cstdio>
#include <string>

#include "nowplaying.h"

static int g_fail = 0;
static void Expect(const wchar_t* in, const wchar_t* exe, const wchar_t* want)
{
    const std::wstring got = nowplaying::CleanTrackTitle(in, exe);
    if (got != want) {
        std::wprintf(L"FAIL '%ls' (%ls): erwartet '%ls', bekommen '%ls'\n", in, exe, want, got.c_str());
        ++g_fail;
    }
}

int main()
{
    Expect(L"Daft Punk - Get Lucky", L"spotify.exe", L"Daft Punk - Get Lucky");
    Expect(L"Spotify Premium", L"spotify.exe", L"");
    Expect(L"Spotify Free", L"spotify.exe", L"");
    Expect(L"  Spotify  ", L"spotify.exe", L"");
    Expect(L"", L"spotify.exe", L"");
    Expect(L"Rick Astley - Never Gonna Give You Up - YouTube - Google Chrome", L"chrome.exe", L"Rick Astley - Never Gonna Give You Up - YouTube");
    Expect(L"(3) Lofi Beats - YouTube - Google Chrome", L"chrome.exe", L"Lofi Beats - YouTube");
    Expect(L"(12) Song – Mozilla Firefox", L"firefox.exe", L"Song");
    Expect(L"Mix — Microsoft Edge", L"msedge.exe", L"Mix");
    Expect(L"YouTube - Google Chrome", L"chrome.exe", L"");
    Expect(L"(5) YouTube", L"chrome.exe", L"");
    Expect(L"(Remix) Song", L"x.exe", L"(Remix) Song");   // Klammer ohne Zahl bleibt
    Expect(L"(2023) Live", L"x.exe", L"(2023) Live");     // vierstellig = Jahreszahl, kein Zaehler
    Expect(L"Chromium Notes", L"x.exe", L"Chromium Notes"); // Browsername nur als Suffix mit Trenner
    std::printf(g_fail ? "%d FEHLER\n" : "nowplaying_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
