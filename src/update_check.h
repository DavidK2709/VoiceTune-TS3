
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
