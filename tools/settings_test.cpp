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
        t.sounds[0] = {L"C:\\Sounds\\airhorn.wav", L"Hupe"};
        t.sounds[7] = {L"D:\\Töne\\Ärger.mp3", L"Ärger"};
        t.nowPlayingChat    = false;
        t.nowPlayingChannel = true;
        t.nowPlayingMeta    = true;
        t.language          = 2;
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
                  u.sounds[7].name == L"Ärger" && u.sounds[3].path.empty(), "Soundboard-Slots");
        CHECK(!u.nowPlayingChat && u.nowPlayingChannel && u.nowPlayingMeta && u.language == 2, "Now-Playing und Sprache");

        // Standardwerte einer leeren INI
        const Settings d = LoadSettings(ini + L".gibtsnicht");
        CHECK(d.nowPlayingChat && !d.nowPlayingChannel && !d.nowPlayingMeta && d.language == 0 && d.soundVolPct == 100 && d.profiles.items.empty(), "Standardwerte");

        // Obergrenze der Profile
        profiles::ProfileBook many;
        for (size_t i = 0; i < profiles::kMaxProfiles; ++i) many.Save(L"P" + std::to_wstring(i), a);
        CHECK(many.items.size() == profiles::kMaxProfiles && many.Save(L"Zu viel", a) == PresetResult::Full, "maximal %zu Profile", profiles::kMaxProfiles);

        // Geloeschtes Profil verschwindet auch aus der Datei
        t.profiles.Delete(L"Zocken");
        SaveSettings(ini, t);
        CHECK(LoadSettings(ini).profiles.items.size() == 1, "nach Loeschen");
    }

    DeleteFileW(ini.c_str());
    std::printf(g_fail ? "%d FEHLER\n" : "settings_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
