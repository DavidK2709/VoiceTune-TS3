
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
// Voicechanger: Klangverarbeitung fuer das Mikrofon-Signal - der Spielbereich fuer den Voicechanger-Entwickler (Oberflaeche: voicechanger_page.{h,cpp}).
//
// Stand: Geruest ohne Wirkung (Process laesst das Signal unveraendert), noch NICHT in die Engine eingehaengt. Zum Anschliessen:
//   1. Engine (engine.h) bekommt ein Mitglied `VoiceChanger voice_;`.
//   2. In Engine::Mix (engine.cpp) ruft man `voice_.Process(buffer, frames)` an der Stelle auf, die der Effekt braucht
//      (z.B. direkt nach der Musik-Lautstaerke, vor Optimierer/Begrenzer).
//   3. Einstellungen (settings.h/.cpp) um die Parameter erweitern, die Oberflaeche in voicechanger_page.cpp bauen.
// Regeln fuer Process: laeuft im Audio-Thread - nicht blockieren (keine Locks, kein new/delete, kein Datei-/Netz-Zugriff), nur Atomics fuer Parameter.
#include <atomic>
#include <cstdint>

class VoiceChanger {
public:
    // Dem Signal Effekte aufpraegen. io = interleaved Stereo, 48 kHz, 16 bit; frames = Anzahl der Frames. true = Signal wurde veraendert.
    bool Process(int16_t* io, int frames);

    void SetEnabled(bool on) { enabled_ = on; }
    bool Enabled() const { return enabled_.load(); }

private:
    std::atomic<bool> enabled_{false};
};
