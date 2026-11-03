
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
#include <string>

// Nicht-blockierend: oeffnet das Einstellungsfenster in einem eigenen Thread (oder holt es nach vorn).
void OpenConfigDialog();
// Schliesst das Fenster und wartet auf den Thread (beim Entladen des Plugins).
void CloseConfigDialog();

// Vom Plugin: oeffnet die Hotkey-Einstellungen von TeamSpeak (Knopf "Hotkeys ..." im Fenster)
void SetHotkeySetupHandler(void (*fn)());
// Vom Plugin: baut den Text fuer "Diagnose kopieren" (Versionen, Einstellungen ohne Pfade, Zustand, Log)
void SetDiagnosticsHandler(std::wstring (*fn)());
// Vom Plugin: eindeutige ID, Name des Servers und Name des eigenen Kanals der aktiven Verbindung; false = nicht verbunden (fuer die Profil-Automatik)
void SetServerInfoHandler(bool (*fn)(std::wstring* uid, std::wstring* serverName, std::wstring* channel));
