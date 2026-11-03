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
