#pragma once
#include <string>
#include <vector>

#include "presets.h"

struct Settings {
    bool         enabled     = false; // Musik einspielen
    std::wstring targetExe;           // z.B. "spotify.exe" (klein geschrieben)
    int          musicVolPct = 100;   // 0..200
    int          micVolPct   = 100;   // 0..200
    bool         alwaysSend  = true;  // Musik auch senden, wenn PTT/Sprachaktivierung nicht ausloest
    bool         monitor     = false; // Mithoeren: eingespielte Musik verzoegert auf der eigenen TS3-Wiedergabe
    int          monitorDelayMs = 250; // 0..2000

    // Equalizer (Raster 10 / 31 / 61 Baender, je +-12 dB)
    bool               eqEnabled = true;
    int                eqBands   = 31;
    std::vector<float> eqGains;       // eqBands Werte in dB (leer = flach)
    std::wstring       eqPresetName;  // zuletzt gewaehltes Preset (mitgeliefert oder eigen); leer = eigene Kurve
    presets::PresetBook userPresets;  // eigene, benannte Filter

    // Ausgeblendete Apps in der Quellen-Auswahl (Exe-Namen, klein geschrieben); Rechtsklick -> "Alle Apps einblenden"
    std::vector<std::wstring> hiddenApps;

    // Musikmodus: solange Musik laeuft, werden die Sprachfilter von TS3 (Rauschfilter, Auto-Pegel, Echo-Reduktion; mit "Musik immer senden"
    // auch die Sprachaktivierung) abgeschaltet und danach wiederhergestellt. gateDb ist dann die Schwelle des plugin-eigenen Mikro-Gates.
    bool musicMode = true;
    int  gateDb    = -50; // -70 = Gate aus, bis -25

    // Klang: fuer die Opus-Uebertragung aufbereiten (phasensichere Mono-Summe, Begrenzer) und Mithoeren/Anhoeren ueber einen lokalen Opus-Coder
    // schicken, damit es annaehernd so klingt wie bei den anderen.
    bool optimizeForOpus = true;
    bool opusPreview     = true;

    // Qualitaet (plugin-intern): 0 = niedrige Latenz, 1 = ausgewogen, 2 = stabil; Mono mischt die Musik als Mono ein
    int  quality = 1;
    bool mono    = false;
};

// section: Abschnittsname der INI ("VoiceTune"; die Vorgaengerversion nutzte "AppAudioMixer")
Settings LoadSettings(const std::wstring& iniPath, const wchar_t* section = L"VoiceTune");
void     SaveSettings(const std::wstring& iniPath, const Settings& s);
