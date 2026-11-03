// Selbsttest der gemeinsamen Befehle (Hotkeys, Stream-Deck-API): jeder Befehl veraendert den erwarteten Zustand, ungueltige Argumente werden abgelehnt.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "commands.h"
#include "engine.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static std::vector<std::pair<std::string, bool>> g_notes;

int main()
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring cfg = std::wstring(tmp) + L"aam_commands_test";
    CreateDirectoryW(cfg.c_str(), nullptr);
    DeleteFileW((cfg + L"\\voicetune.ini").c_str());
    DeleteFileW((cfg + L"\\appaudiomixer.ini").c_str());

    Engine& en = Engine::Get();
    en.SetLogger([](const char*) {});
    en.Init(cfg);
    SetCommandNotifier([](const char* kind, bool on) { g_notes.push_back({kind, on}); });

    // URL-Dekodierung
    CHECK(UrlDecode("Bass%2B") == "Bass+" && UrlDecode("a+b") == "a+b" && UrlDecode("%C3%A4") == "\xC3\xA4" && UrlDecode("%zz") == "%zz" && UrlDecode("x%2") == "x%2" &&
              UrlDecode("") == "" && UrlDecode("Mein%20Preset") == "Mein Preset",
          "UrlDecode");

    // An/Aus
    CHECK(RunCommand("on") && en.GetSettings().enabled, "on");
    CHECK(RunCommand("off") && !en.GetSettings().enabled, "off");
    CHECK(RunCommand("toggle") && en.GetSettings().enabled && RunCommand("toggle") && !en.GetSettings().enabled, "toggle");

    // Stumm + Rueckmeldung
    CHECK(!en.IsMicMuted() && RunCommand("micmute") && en.IsMicMuted() && g_notes.size() == 1 && g_notes[0] == std::make_pair(std::string("micmute"), true), "micmute an + Meldung");
    CHECK(RunCommand("micmute") && !en.IsMicMuted() && g_notes.size() == 2 && g_notes[1].second == false, "micmute aus + Meldung");

    // Lautstaerke relativ und absolut
    en.SetMusicVolume(100);
    CHECK(RunCommand("musicvol_up") && en.GetSettings().musicVolPct == 110, "musicvol_up");
    CHECK(RunCommand("musicvol_down") && RunCommand("musicvol_down") && en.GetSettings().musicVolPct == 90, "musicvol_down");
    en.SetMusicVolume(195);
    CHECK(RunCommand("musicvol_up") && en.GetSettings().musicVolPct == 200, "obere Grenze");
    en.SetMusicVolume(5);
    CHECK(RunCommand("musicvol_down") && en.GetSettings().musicVolPct == 0, "untere Grenze");
    CHECK(RunCommandPath("musicvol/80") && en.GetSettings().musicVolPct == 80, "musicvol/80");
    CHECK(RunCommand("musicvol", "0") && en.GetSettings().musicVolPct == 0 && RunCommand("musicvol", "200") && en.GetSettings().musicVolPct == 200, "Grenzwerte 0 und 200");
    CHECK(!RunCommandPath("musicvol/201") && !RunCommandPath("musicvol/-1") && !RunCommandPath("musicvol/abc") && !RunCommandPath("musicvol/") && !RunCommandPath("musicvol") &&
              !RunCommandPath("musicvol/8 0") && !RunCommandPath("musicvol/1234567") && en.GetSettings().musicVolPct == 200,
          "ungueltige Lautstaerke wird abgelehnt und aendert nichts (%d)", en.GetSettings().musicVolPct);
    CHECK(RunCommandPath("micvol/50") && en.GetSettings().micVolPct == 50 && !RunCommandPath("micvol/300"), "micvol");

    // Mithoeren
    const bool mon = en.GetSettings().monitor;
    CHECK(RunCommand("monitor") && en.GetSettings().monitor == !mon && RunCommand("monitor") && en.GetSettings().monitor == mon, "monitor");
    const bool smon = en.GetSettings().soundMonitor;
    CHECK(RunCommand("soundmonitor") && en.GetSettings().soundMonitor == !smon && RunCommand("soundmonitor") && en.GetSettings().soundMonitor == smon, "soundmonitor");
    const bool fmon = en.GetSettings().fileMonitor;
    CHECK(RunCommand("filemonitor") && en.GetSettings().fileMonitor == !fmon && en.GetSettings().soundMonitor == smon && RunCommand("filemonitor") && en.GetSettings().fileMonitor == fmon,
          "filemonitor (unabhaengig von soundmonitor)");
    CHECK(!RunCommandPath("playlist/gibt%20es%20nicht") && !RunCommand("playlist"), "unbekannte Wiedergabeliste");
    CHECK(en.PlaylistSave(L"Cmd Liste") == PresetResult::Ok && RunCommandPath("playlist/cmd%20liste") && en.GetSettings().playlistName == L"Cmd Liste" && en.PlaylistDelete(L"Cmd Liste"),
          "playlist/<Name> laedt eine gespeicherte Liste");

    // EQ-Presets: mitgelieferte (Gross-/Kleinschreibung egal, URL-kodiert) und eigene
    const std::wstring builtin = eq::PresetName(1);
    std::string        builtinUtf8(WideCharToMultiByte(CP_UTF8, 0, builtin.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, builtin.c_str(), -1, &builtinUtf8[0], static_cast<int>(builtinUtf8.size()), nullptr, nullptr);
    builtinUtf8.resize(builtinUtf8.size() - 1);
    std::string encoded; // alles als %XX kodieren
    for (unsigned char c : builtinUtf8) {
        char b[8];
        std::snprintf(b, sizeof(b), "%%%02X", c);
        encoded += b;
    }
    en.ApplyBuiltinPreset(0);
    CHECK(RunCommandPath("preset/" + encoded) && en.GetSettings().eqPresetName == builtin, "mitgeliefertes Preset '%ls'", en.GetSettings().eqPresetName.c_str());
    en.SaveEqPreset(L"Mein Preset");
    en.ApplyBuiltinPreset(0);
    CHECK(RunCommandPath("preset/mein%20preset") && en.GetSettings().eqPresetName == L"Mein Preset", "eigenes Preset");
    CHECK(!RunCommandPath("preset/gibt%20es%20nicht") && !RunCommandPath("preset/") && !RunCommandPath("preset"), "unbekanntes Preset");

    // Profile
    CHECK(!RunCommand("profile_next") && !RunCommandPath("profile/X"), "ohne Profile geht nichts");
    en.SetMusicVolume(60);
    en.SaveProfile(L"Abend");
    en.SetMusicVolume(120);
    en.SaveProfile(L"Zocken 2");
    CHECK(RunCommandPath("profile/abend") && en.GetSettings().musicVolPct == 60 && en.GetSettings().profileName == L"Abend", "profile/<Name>");
    CHECK(RunCommandPath("profile/Zocken%202") && en.GetSettings().musicVolPct == 120, "Profilname mit Leerzeichen");
    CHECK(RunCommand("profile_next") && en.GetSettings().profileName == L"Abend" && RunCommand("profile_next") && en.GetSettings().profileName == L"Zocken 2", "profile_next reihum");
    CHECK(!RunCommandPath("profile/gibt%20es%20nicht"), "unbekanntes Profil");
    CHECK(RunCommandPath("profilenum/1") && en.GetSettings().profileName == L"Abend" && RunCommandPath("profilenum/2") && en.GetSettings().profileName == L"Zocken 2" && RunCommandPath("profilenum/9") &&
              en.GetSettings().profileName == L"Zocken 2" && !RunCommandPath("profilenum/0") && !RunCommandPath("profilenum/11") && !RunCommandPath("profilenum/x") && RunHotkey("profile1") &&
              en.GetSettings().profileName == L"Abend",
          "profilenum/<n> und Hotkeys profile1..10");

    // Quelle
    CHECK(RunCommandPath("source/Spotify.EXE") && en.GetSettings().targetExe == L"spotify.exe" && !RunCommandPath("source/"), "source");

    // Soundboard (leere Slots: gueltiges Argument, aber nichts zu spielen)
    CHECK(RunCommandPath("sound/1") && RunCommandPath("sound/400") && !RunCommandPath("sound/0") && !RunCommandPath("sound/401") && !RunCommandPath("sound/x") && !RunCommandPath("sound"), "sound-Argumente");
    CHECK(RunCommand("soundstop"), "soundstop");

    // Datei-Player ohne Titel: darf nichts kaputt machen
    CHECK(RunCommand("playpause") && RunCommand("next") && RunCommand("prev") && RunCommand("stop"), "Player-Befehle ohne Liste");

    // Unbekanntes
    CHECK(!RunCommand("gibtsnicht") && !RunCommandPath("") && !RunCommandPath("/") && !RunCommandPath("toggle2"), "unbekannte Befehle");

    // Hotkeys: jedes Schluesselwort der Liste wird verstanden (profile_next hat Profile, sound-Slots sind leer = ok)
    const auto& list = HotkeyList();
    CHECK(list.size() == 9 + static_cast<size_t>(kSoundHotkeys) + 1 + kProfileHotkeys, "%d Hotkeys erwartet, nicht %zu", 9 + kSoundHotkeys + 1 + kProfileHotkeys, list.size());
    for (const auto& h : list) {
        CHECK(h.keyword && h.description && std::string(h.description).size() > 5 && std::string(h.description).size() < 127, "Beschreibung von %s", h.keyword);
        CHECK(RunHotkey(h.keyword), "Hotkey '%s' wird nicht verstanden", h.keyword);
    }
    CHECK(RunHotkey("sound9") && RunHotkey("sound400") && !RunHotkey("sound0") && !RunHotkey("sound401") && !RunHotkey("sound") && !RunHotkey("sounds") && !RunHotkey("nix"), "ungueltige Hotkeys");

    en.Shutdown();
    DeleteFileW((cfg + L"\\voicetune.ini").c_str());
    std::printf(g_fail ? "%d FEHLER\n" : "commands_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
