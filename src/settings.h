#pragma once
#include <string>
#include <vector>

#include "presets.h"
#include "profiles.h"

// Soundboard: kurze Clips (Jingles), per Hotkey/Fenster/Stream Deck ueber der Musik abspielbar
constexpr int kSoundSlots = 8;
struct SoundSlot {
    std::wstring path; // leer = Slot frei
    std::wstring name; // Anzeigename (Standard: Dateiname ohne Endung)
};

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

    // Profile / Szenen (benannte Schnappschuesse der Einstellungen) und das zuletzt gewaehlte
    profiles::ProfileBook profiles;
    std::wstring          profileName;

    // Soundboard
    SoundSlot sounds[kSoundSlots];
    int       soundVolPct = 100; // 0..200

    // Now-Playing: Titelwechsel im eigenen Chat melden (Standard), optional im Kanal-Chat (erreicht andere!) oder in den Client-Metadaten
    bool nowPlayingChat    = true;
    bool nowPlayingChannel = false;
    bool nowPlayingMeta    = false;

    // Oberflaechensprache: 0 = automatisch (nach Windows), 1 = Deutsch, 2 = English
    int language = 0;
};

// Schnappschuss der aktuellen Einstellungen als Profil (Name leer, wird beim Speichern gesetzt)
inline Profile ProfileFromSettings(const Settings& s)
{
    Profile p;
    p.targetExe       = s.targetExe;
    p.musicVolPct     = s.musicVolPct;
    p.micVolPct       = s.micVolPct;
    p.monitor         = s.monitor;
    p.monitorDelayMs  = s.monitorDelayMs;
    p.eqEnabled       = s.eqEnabled;
    p.eqBands         = s.eqBands;
    p.eqGains         = s.eqGains;
    p.eqGains.resize(s.eqBands, 0.0f);
    p.eqPresetName    = s.eqPresetName;
    p.quality         = s.quality;
    p.mono            = s.mono;
    p.optimizeForOpus = s.optimizeForOpus;
    p.opusPreview     = s.opusPreview;
    return p;
}

// Gleiche Einstellungen? (Name und Preset-Name zaehlen nicht, EQ-Kurve mit 0,05 dB Toleranz). Fuer die Anzeige "(geaendert)".
inline bool SameProfile(const Profile& a, const Profile& b)
{
    return a.targetExe == b.targetExe && a.musicVolPct == b.musicVolPct && a.micVolPct == b.micVolPct && a.monitor == b.monitor && a.monitorDelayMs == b.monitorDelayMs &&
           a.eqEnabled == b.eqEnabled && a.eqBands == b.eqBands && presets::CurvesMatch(a.eqGains, b.eqGains, 0.05f) && a.quality == b.quality && a.mono == b.mono &&
           a.optimizeForOpus == b.optimizeForOpus && a.opusPreview == b.opusPreview;
}

// section: Abschnittsname der INI ("VoiceTune"; die Vorgaengerversion nutzte "AppAudioMixer")
Settings LoadSettings(const std::wstring& iniPath, const wchar_t* section = L"VoiceTune");
void     SaveSettings(const std::wstring& iniPath, const Settings& s);
