#pragma once
// Update-Hinweis: fragt (hoechstens alle 12 Stunden, in einem eigenen Thread) bei GitHub nach dem neuesten Release und merkt sich den Tag in den
// Einstellungen. Netzfehler bleiben still. Die Oberflaeche zeigt "Neue Version" nur, wenn updatelogic::IsNewer(updateLatest, VOICETUNE_VERSION_W).
#include <string>

namespace updatecheck {

// Startet die Abfrage, falls eingeschaltet und die letzte laenger als 12 h her ist (force = immer). Kehrt sofort zurueck.
void Start(bool force = false);
// Beendet einen laufenden Thread (beim Entladen des Plugins).
void Stop();
// Gibt es laut Einstellungen eine neuere Version? latestOut = deren Tag.
bool NewerAvailable(std::wstring* latestOut = nullptr);

} // namespace updatecheck
