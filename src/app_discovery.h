#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct AppInfo {
    DWORD        pid = 0;       // Wurzelprozess der App (Browser/Electron: Haupt-Prozess)
    std::wstring exe;           // z.B. "spotify.exe" (klein geschrieben)
    std::wstring path;          // voller Pfad der Exe (fuer das Icon), leer wenn nicht ermittelbar
    std::wstring title;         // Fenstertitel des Hauptfensters (kann leer sein)
    bool         playing = false; // Audio-Session aktuell aktiv
};

// Listet alle Apps mit Audio-Session auf einem aktiven Wiedergabegeraet (eigener Prozess ausgenommen).
// Aktive (spielende) Apps stehen vorn. Initialisiert COM bei Bedarf selbst.
std::vector<AppInfo> DiscoverApps();

// "spotify.exe - Artist - Song  [spielt]"
std::wstring DisplayName(const AppInfo& app);

// Suche nach Exe-Name (case-insensitive) in einer Liste.
const AppInfo* FindByExe(const std::vector<AppInfo>& apps, const std::wstring& exeLower);

std::wstring ToLower(std::wstring s);

// "spotify.exe" -> "Spotify", "ts3client_win64.exe" -> "TeamSpeak 3"
std::wstring PrettyExeName(const std::wstring& exeLower);
