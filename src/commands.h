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
// Gemeinsame Befehle fuer Hotkeys (TS3), Stream-Deck-Schnittstelle (remote.cpp) und Fenster. Alle sind aus jedem Thread aufrufbar.
//
//   toggle | on | off              "Musik einspielen" umschalten / ein / aus
//   micmute                        Mikrofon stumm <-> laut
//   musicvol_up | musicvol_down    Musik-Lautstaerke +-10 Prozentpunkte (0..200)
//   musicvol/<0-200>               Musik-Lautstaerke absolut
//   micvol/<0-200>                 Mikrofon-Lautstaerke absolut
//   monitor                        Mithoeren ein/aus umschalten
//   playpause | next | prev | stop Datei-Player
//   preset/<Name>                  Equalizer-Preset (mitgeliefert oder eigen)
//   profile/<Name> | profile_next  Profil anwenden bzw. naechstes Profil reihum
//   profilenum/<1-10>              n-tes Profil der Liste anwenden (Hotkeys profile1..profile10)
//   source/<exe>                   Audio-Quelle waehlen (z.B. spotify.exe)
//   sound/<1-400> | soundstop        Soundboard-Clip abspielen / alle stoppen (ohne "Musik einspielen" zu veraendern)
//   soundmonitor                   Selbst hoeren fuer Soundboard-Clips (auf dem Standard-Ausgabegeraet) ein/aus umschalten
//   filemonitor                    Selbst hoeren fuer den Datei-Player ein/aus umschalten
//   playlist/<Name>                gespeicherte Wiedergabeliste laden (ersetzt die aktuelle, startet nicht)
#include <string>
#include <vector>

constexpr int kProfileHotkeys = 10; // Hotkeys "Profil 1 anwenden" ... "Profil 10 anwenden"

// name + optionales Argument. true = Befehl bekannt und Argument gueltig (und ausgefuehrt).
bool RunCommand(const std::string& name, const std::string& arg = "");

// "musicvol/80" oder "preset/Bass%2B" (URL-kodiertes Argument) -> RunCommand
bool RunCommandPath(const std::string& path);

// TS3-Hotkey-Schluesselwort ("toggle", "sound3", "profile_next" ...) -> RunCommand
bool RunHotkey(const std::string& keyword);

// Alle Hotkey-Schluesselwoerter mit deutscher Beschreibung (fuer ts3plugin_initHotkeys); sound1..sound64 gehoeren zu den ersten 64 Soundboard-Knoepfen
struct HotkeyDef {
    const char* keyword;
    const char* description; // bei number > 0 mit "%d" fuer die Nummer (Sound-Knoepfe)
    int         number = 0;
};
const std::vector<HotkeyDef>& HotkeyList();

// URL-Kodierung aufloesen: "%2B" -> "+", "%C3%A4" -> UTF-8-Umlaut; ein "+" bleibt "+" (kein Leerzeichen)
std::string UrlDecode(const std::string& in);

// Rueckmeldung nach einem Befehl fuer die Oberflaeche des TS3-Clients (z.B. Chat-Zeile "Mikrofon stumm"): kind = "micmute", on = neuer Zustand.
void SetCommandNotifier(void (*fn)(const char* kind, bool on));
