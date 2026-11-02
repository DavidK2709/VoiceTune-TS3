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

    DeleteFileW(ini.c_str());
    std::printf(g_fail ? "%d FEHLER\n" : "settings_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
