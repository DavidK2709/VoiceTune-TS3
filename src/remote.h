
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
void RemoteStart(unsigned short port = 19470);
void RemoteStop();
// Laeuft die Schnittstelle (Port frei und Server gestartet)? Fuer die Anzeige in den Einstellungen.
bool RemoteIsRunning();
unsigned short RemotePort();
// Wird aufgerufen, wenn die Schnittstelle nicht starten konnte (z.B. Port belegt). Vor RemoteStart setzen.
void RemoteSetUnavailableCallback(void (*fn)(unsigned short port));
