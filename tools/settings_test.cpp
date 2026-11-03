
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
 
// Roundtrip der INI: Einstellungen inkl. eigener EQ-Presets (mit Umlauten im Namen) schreiben, neu lesen, vergleichen.
#include <windows.h>

#include <cstdio>
#include <string>

#include "settings.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

int main()
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring ini = std::wstring(tmp) + L"aam_settings_test.ini";
    DeleteFileW(ini.c_str());

    Settings s;
    s.enabled = true;
    s.targetExe = L"spotify.exe";
    s.eqBands = 31;
    s.eqGains.assign(31, 0.0f);
    s.eqGains[4] = 7.5f;
    s.eqGains[10] = -3.0f;
    s.eqPresetName = L"Wärme für Rock";
    std::vector<float> g61(61, 0.0f);
    g61[20] = 4.5f;
    s.userPresets.Save(L"Wärme für Rock", 31, s.eqGains);
    s.userPresets.Save(L"Klar 61", 61, g61);
    s.userPresets.Save(L"Drittes", 10, std::vector<float>(10, -1.5f));
    s.musicMode = false;
    s.gateDb = -42;
    s.hiddenApps = {L"discord.exe", L"steam.exe", L"medalencoder.exe"};
    s.optimizeForOpus = false;
    s.opusPreview     = false;
    SaveSettings(ini, s);

    Settings r = LoadSettings(ini);
    CHECK(r.enabled && r.targetExe == L"spotify.exe", "Grundwerte");
    CHECK(r.eqBands == 31 && r.eqGains.size() == 31 && r.eqGains[4] == 7.5f && r.eqGains[10] == -3.0f, "EQ-Kurve");
    CHECK(r.eqPresetName == L"Wärme für Rock", "gewaehltes Preset (Umlaute)");
    CHECK(r.userPresets.items.size() == 3, "Anzahl %zu", r.userPresets.items.size());
    if (r.userPresets.items.size() == 3) {
        CHECK(r.userPresets.items[0].name == L"Wärme für Rock" && r.userPresets.items[0].bands == 31 && r.userPresets.items[0].gains[4] == 7.5f, "Preset 1");
        CHECK(r.userPresets.items[1].name == L"Klar 61" && r.userPresets.items[1].bands == 61 && r.userPresets.items[1].gains.size() == 61 && r.userPresets.items[1].gains[20] == 4.5f, "Preset 2");
        CHECK(r.userPresets.items[2].bands == 10 && r.userPresets.items[2].gains[9] == -1.5f, "Preset 3");
    }
    CHECK(!r.musicMode && r.gateDb == -42, "Musikmodus/Gate");
    CHECK(!r.optimizeForOpus && !r.opusPreview, "Opus-Schalter");
    CHECK(r.hiddenApps.size() == 3 && r.hiddenApps[0] == L"discord.exe" && r.hiddenApps[2] == L"medalencoder.exe", "ausgeblendete Apps: %zu", r.hiddenApps.size());

    // Geloeschtes Preset verschwindet nach erneutem Speichern wirklich aus der Datei
    s.userPresets.Delete(L"Klar 61");
    SaveSettings(ini, s);
    r = LoadSettings(ini);
    CHECK(r.userPresets.items.size() == 2 && !r.userPresets.Find(L"Klar 61") && r.userPresets.Find(L"Drittes"), "nach Loeschen %zu", r.userPresets.items.size());

    s.hiddenApps.clear(); // alle wieder eingeblendet
    SaveSettings(ini, s);
    CHECK(LoadSettings(ini).hiddenApps.empty(), "alle eingeblendet");

    // ---- Profile, Sounds, Now-Playing, Sprache -------------------------------------------------------------------------------------------
    {
        Settings t;
        Profile  a;
        a.targetExe = L"spotify.exe";
        a.musicVolPct = 80;
        a.micVolPct = 120;
        a.monitor = true;
        a.monitorDelayMs = 400;
        a.eqEnabled = false;
        a.eqBands = 61;
        a.eqGains.assign(61, 0.0f);
        a.eqGains[7] = -4.5f;
        a.eqPresetName = L"Wärme";
        a.quality = 2;
        a.mono = true;
        a.optimizeForOpus = false;
        a.opusPreview = false;
        CHECK(t.profiles.Save(L"  Stream \"Abend\"  ", a) == PresetResult::Ok, "Profil anlegen");
        CHECK(t.profiles.Find(L"stream 'abend'") != nullptr, "Name bereinigt und ohne Beachtung der Gross-/Kleinschreibung gefunden");
        Profile b = a;
        b.targetExe = L"chrome.exe";
        b.eqBands = 10;
        b.eqGains.assign(10, 2.0f);
        CHECK(t.profiles.Save(L"Gaming", b) == PresetResult::Ok && t.profiles.Save(L"GAMING", b) == PresetResult::Updated && t.profiles.items.size() == 2, "gleicher Name ueberschreibt");
        CHECK(t.profiles.Save(L"   ", a) == PresetResult::Empty, "leerer Name");
        CHECK(t.profiles.Rename(L"Gaming", L"Stream 'Abend'") == PresetResult::Exists, "Umbenennen auf vorhandenen Namen");
        CHECK(t.profiles.Rename(L"Gaming", L"Zocken") == PresetResult::Ok && !t.profiles.Find(L"Gaming") && t.profiles.Find(L"Zocken"), "Umbenennen");
        CHECK(t.profiles.Rename(L"gibt es nicht", L"X") == PresetResult::NotFound, "Umbenennen: nicht gefunden");
        CHECK(t.profiles.SuggestName() == L"Profil 1" && t.profiles.NextAfter(L"Stream 'Abend'") == L"Zocken" && t.profiles.NextAfter(L"Zocken") == L"Stream 'Abend'" &&
                  t.profiles.NextAfter(L"unbekannt") == L"Stream 'Abend'", "Vorschlag und Reihum");
        t.profileName = L"Zocken";
        t.soundVolPct = 150;
        t.sounds[0] = {L"C:\\Sounds\\airhorn.wav", L"Hupe", 0xC0392B, 150};
        t.sounds[7] = {L"D:\\Töne\\Ärger.mp3", L"Ärger | Mix", 0, 100};
        t.sounds[399] = {L"E:\\x.wav", L"Letzter", 0x00FF7F, 20};
        t.soundRows     = 18;
        t.soundCols     = 17;
        t.soundShowKeys = false;
        t.soundPinned   = true;
        t.soundMonitor  = false;
        t.nowPlayingChat    = false;
        t.nowPlayingChannel = true;
        t.nowPlayingMeta    = true;
        t.language          = 2;
        t.advancedOpen      = true;
        SaveSettings(ini, t);
        const Settings u = LoadSettings(ini);
        CHECK(u.profiles.items.size() == 2 && u.profileName == L"Zocken", "Profile geladen: %zu", u.profiles.items.size());
        if (const Profile* p = u.profiles.Find(L"Stream 'Abend'")) {
            CHECK(p->targetExe == L"spotify.exe" && p->musicVolPct == 80 && p->micVolPct == 120 && p->monitor && p->monitorDelayMs == 400 && !p->eqEnabled && p->eqBands == 61 &&
                      p->eqGains.size() == 61 && p->eqGains[7] == -4.5f && p->eqPresetName == L"Wärme" && p->quality == 2 && p->mono && !p->optimizeForOpus && !p->opusPreview,
                  "Profil 1 vollstaendig");
        } else {
            CHECK(false, "Profil 1 fehlt");
        }
        if (const Profile* p = u.profiles.Find(L"Zocken")) CHECK(p->targetExe == L"chrome.exe" && p->eqBands == 10 && p->eqGains.size() == 10 && p->eqGains[3] == 2.0f, "Profil 2");
        else CHECK(false, "Profil 2 fehlt");
        CHECK(u.soundVolPct == 150 && u.sounds[0].path == L"C:\\Sounds\\airhorn.wav" && u.sounds[0].name == L"Hupe" && u.sounds[7].path == L"D:\\Töne\\Ärger.mp3" &&
                  u.sounds[7].name == L"Ärger | Mix" && u.sounds[3].path.empty(), "Soundboard-Slots");
        CHECK(u.sounds[0].color == 0xC0392B && u.sounds[0].volPct == 150 && u.sounds[7].color == 0 && u.sounds[7].volPct == 100 && u.sounds[399].path == L"E:\\x.wav" &&
                  u.sounds[399].name == L"Letzter" && u.sounds[399].color == 0x00FF7F && u.sounds[399].volPct == 20,
              "Knopf-Farbe, Lautstaerke und letzter Knopf (400)");
        CHECK(u.soundRows == 18 && u.soundCols == 17 && !u.soundShowKeys && u.soundPinned && !u.soundMonitor, "Raster, Tastenkuerzel anzeigen, Fenster oben, Clips selbst hoeren aus");
        CHECK(!u.nowPlayingChat && u.nowPlayingChannel && u.nowPlayingMeta && u.language == 2, "Now-Playing und Sprache");
        CHECK(u.advancedOpen, "Erweitert aufgeklappt gespeichert");

        // Standardwerte einer leeren INI
        const Settings d = LoadSettings(ini + L".gibtsnicht");
        CHECK(d.nowPlayingChat && !d.nowPlayingChannel && !d.nowPlayingMeta && d.language == 0 && d.soundVolPct == 100 && d.soundRows == 2 && d.soundCols == 4 && d.soundShowKeys && !d.soundPinned && d.soundMonitor && d.profiles.items.empty() && !d.advancedOpen, "Standardwerte (Erweitert zu)");

        // Obergrenze der Profile
        profiles::ProfileBook many;
        for (size_t i = 0; i < profiles::kMaxProfiles; ++i) many.Save(L"P" + std::to_wstring(i), a);
        CHECK(many.items.size() == profiles::kMaxProfiles && many.Save(L"Zu viel", a) == PresetResult::Full, "maximal %zu Profile", profiles::kMaxProfiles);

        // Geloeschtes Profil verschwindet auch aus der Datei
        t.profiles.Delete(L"Zocken");
        SaveSettings(ini, t);
        CHECK(LoadSettings(ini).profiles.items.size() == 1, "nach Loeschen");
    }

    // ---- Datei-Player: Selbst hoeren, letzte Liste, benannte Listen; Pfade ausserhalb der ANSI-Codepage (INI muss UTF-16 sein) --------------
    {
        CHECK(LoadSettings(ini + L".gibtsnicht").fileMonitor && LoadSettings(ini + L".gibtsnicht").lastPlaylist.empty(), "Standard: Dateien selbst hoeren an, keine Liste");
        Settings t;
        t.fileMonitor  = false;
        t.lastPlaylist = {L"C:\\Musik\\日本語の曲.mp3", L"D:\\Töne\\Ärger.flac", L"E:\\x y\\z.wav"};
        t.lastRepeat   = 2;
        t.playlistName = L"Party";
        t.playlists.Save(L"Party", t.lastPlaylist);
        t.playlists.Save(L"Ruhig", {L"F:\\leise.mp3"});
        t.playlists.Save(L"Leer", {});
        SaveSettings(ini, t);
        const Settings u = LoadSettings(ini);
        CHECK(!u.fileMonitor && u.lastRepeat == 2 && u.playlistName == L"Party", "Schalter, Wiederholen, gewaehlte Liste");
        CHECK(u.lastPlaylist == t.lastPlaylist, "letzte Liste (%zu Pfade, inkl. Japanisch)", u.lastPlaylist.size());
        CHECK(u.playlists.items.size() == 3 && u.playlists.Find(L"Party") && u.playlists.Find(L"Party")->paths == t.lastPlaylist && u.playlists.Find(L"Ruhig")->paths.size() == 1 &&
                  u.playlists.Find(L"Leer") && u.playlists.Find(L"Leer")->paths.empty(),
              "benannte Listen: %zu", u.playlists.items.size());
        t.playlists.Delete(L"Ruhig");
        t.lastPlaylist.clear();
        SaveSettings(ini, t);
        const Settings v = LoadSettings(ini);
        CHECK(v.playlists.items.size() == 2 && !v.playlists.Find(L"Ruhig") && v.lastPlaylist.empty(), "nach Loeschen / Leeren");

        // Eine alte ANSI-INI wird beim Speichern nach UTF-16 umgewandelt, ohne Werte zu verlieren
        DeleteFileW(ini.c_str());
        const char ansi[] = "[VoiceTune]\r\nTargetExe=spotify.exe\r\nMusicVolume=77\r\n";
        HANDLE     f      = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD      put    = 0;
        WriteFile(f, ansi, sizeof(ansi) - 1, &put, nullptr);
        CloseHandle(f);
        Settings w = LoadSettings(ini);
        CHECK(w.targetExe == L"spotify.exe" && w.musicVolPct == 77, "ANSI-INI gelesen");
        w.lastPlaylist = {L"C:\\Musik\\日本語.mp3"};
        SaveSettings(ini, w);
        const Settings x = LoadSettings(ini);
        CHECK(x.targetExe == L"spotify.exe" && x.musicVolPct == 77 && x.lastPlaylist.size() == 1 && x.lastPlaylist[0] == L"C:\\Musik\\日本語.mp3", "nach Umwandlung nach UTF-16");
    }

    DeleteFileW(ini.c_str());
    std::printf(g_fail ? "%d FEHLER\n" : "settings_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
