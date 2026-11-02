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
