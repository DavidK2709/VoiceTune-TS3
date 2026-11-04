
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
 
#pragma once
#include <cstdint>
#include <climits>
#include <string>
#include <vector>

#include "playlists.h"
#include "presets.h"
#include "profiles.h"
#include "voice_presets.h"

// Soundboard: kurze Clips (Jingles), per Hotkey/Fenster/Stream Deck ueber der Musik abspielbar. Die Knoepfe liegen zeilenweise in einem Raster
// (Slot = Zeile * Spalten + Spalte); die ersten kSoundHotkeys Knoepfe koennen ein TS3-Tastenkuerzel bekommen.
constexpr int kSoundSlots       = 400; // = Soundboard::kSlots
constexpr int kSoundHotkeys     = 64;
constexpr int kSoundMaxRows     = 20;
constexpr int kSoundMaxCols     = 20;
struct SoundSlot {
    std::wstring path;          // leer = Slot frei
    std::wstring name;          // Anzeigename (Standard: Dateiname ohne Endung)
    uint32_t     color = 0;     // 0xRRGGBB, 0 = Standardfarbe
    int          volPct = 100;  // Lautstaerke dieses Knopfes, 0..200
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
    std::vector<SoundSlot> sounds = std::vector<SoundSlot>(kSoundSlots);
    int       soundVolPct   = 100; // Gesamtlautstaerke, 0..200
    int       soundRows     = 2;   // Raster 1..kSoundMaxRows x 1..kSoundMaxCols (Zeilen * Spalten <= kSoundSlots)
    int       soundCols     = 4;
    bool      soundShowKeys = true; // Tastenkuerzel auf den Knoepfen anzeigen
    bool      soundPinned   = false; // Soundboard-Fenster immer im Vordergrund
    bool      soundMonitor  = true;  // Selbst hoeren: Soundboard-Clips zusaetzlich auf dem Windows-Standard-Ausgabegeraet

    // Datei-Player
    bool                      fileMonitor = true;  // Selbst hoeren: Datei-Player zusaetzlich auf dem Windows-Standard-Ausgabegeraet
    std::vector<std::wstring> lastPlaylist;        // aktuelle Wiedergabeliste (wird beim Start wiederhergestellt, nicht abgespielt)
    int                       lastRepeat = 0;      // RepeatMode: 0 aus, 1 alle, 2 eins
    playlists::PlaylistBook   playlists;           // benannte Wiedergabelisten
    std::wstring              playlistName;        // zuletzt gespeicherte/geladene Liste

    // Now-Playing: Titelwechsel im eigenen Chat melden (Standard), optional im Kanal-Chat (erreicht andere!) oder in den Client-Metadaten
    bool nowPlayingChat    = true;
    bool nowPlayingChannel = false;
    bool nowPlayingMeta    = false;

    // Oberflaechensprache: 0 = automatisch (nach Windows), 1 = Deutsch, 2 = English
    int language = 0;

    // Seite "Einstellungen": Bereich "Erweitert" aufgeklappt (Musikmodus, Mikro-Gate, Opus) - Standard zu
    bool advancedOpen = false;

    // Fensterpositionen (kNoPos = noch nie gespeichert): Hauptfenster, Soundboard-Fenster (Position und Groesse), Mini-Fenster
    static constexpr int kNoPos = INT_MIN;
    int winX = kNoPos, winY = kNoPos;
    int sbX = kNoPos, sbY = kNoPos, sbW = 0, sbH = 0;
    int miniX = kNoPos, miniY = kNoPos;

    // Update-Hinweis: Nach neuer Version suchen, zuletzt gesehene neueste Version und Zeitpunkt der letzten Abfrage (Sekunden seit 1970)
    bool         updateCheck     = true;
    std::wstring updateLatest;
    long long    updateCheckedAt = 0;

    // Erststart-Systemcheck schon gezeigt
    bool firstRunDone = false;

    // Ducking: Musik leiser, solange ein Sound laeuft bzw. du sprichst (Tiefe in dB, positiv = Absenkung)
    bool duckSoundOn  = true;
    int  duckSoundDb  = 12;
    bool duckMicOn    = false;
    int  duckMicDb    = 10;
    int  duckMicThresholdDb = -45; // Sprachschwelle (Pegel des Mikrofons), -70..-20

    // Lautstaerke angleichen: Sounds bzw. Datei-Titel auf einen Zielpegel (dB, RMS) bringen; je Titelpfad die gemessene Lautheit
    bool normSounds   = false;
    bool normTracks   = false;
    int  normTargetDb = -18;
    std::vector<std::pair<std::wstring, float>> trackGain; // Pfad (klein geschrieben) -> gemessene Lautheit (RMS, dB)

    // Profil automatisch nach Server/Kanal waehlen; Tray-Symbol
    bool autoProfile = true;
    bool trayIcon    = false;

    // Voicechanger: aktuelle Regler, gewaehlte Stimme (leer = keine), eigene Stimmen, Lautstaerke fuers Abhoeren (0..100).
    // Ein/Aus wird absichtlich nicht gespeichert (wie "Mikrofon stumm"): nach einem Neustart klingst du wieder wie du selbst.
    VoiceParams        voice;
    std::wstring       voicePresetName;
    voicepresets::Book voicePresets;
    int                voiceMonitorPct = 0;
    bool               voiceLongWindow = false; // bessere Qualitaet fuer tiefe Stimmen, 43 statt 21 ms Effekt-Latenz
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
    p.soundRows       = s.soundRows;
    p.soundCols       = s.soundCols;
    p.soundVolPct     = s.soundVolPct;
    p.voicePreset     = s.voicePresetName;
    return p;
}

// Gleiche Einstellungen? (Name und Preset-Name zaehlen nicht, EQ-Kurve mit 0,05 dB Toleranz). Fuer die Anzeige "(geaendert)".
inline bool SameProfile(const Profile& a, const Profile& b)
{
    return a.targetExe == b.targetExe && a.musicVolPct == b.musicVolPct && a.micVolPct == b.micVolPct && a.monitor == b.monitor && a.monitorDelayMs == b.monitorDelayMs &&
           a.eqEnabled == b.eqEnabled && a.eqBands == b.eqBands && presets::CurvesMatch(a.eqGains, b.eqGains, 0.05f) && a.quality == b.quality && a.mono == b.mono &&
           a.optimizeForOpus == b.optimizeForOpus && a.opusPreview == b.opusPreview && a.soundRows == b.soundRows && a.soundCols == b.soundCols && a.soundVolPct == b.soundVolPct &&
           (a.voicePreset.empty() || b.voicePreset.empty() || presets::EqualNoCase(a.voicePreset, b.voicePreset)); // aeltere Profile ohne Stimme lassen die Stimme in Ruhe
}

// section: Abschnittsname der INI ("VoiceTune"; die Vorgaengerversion nutzte "AppAudioMixer")
Settings LoadSettings(const std::wstring& iniPath, const wchar_t* section = L"VoiceTune");
void     SaveSettings(const std::wstring& iniPath, const Settings& s);
