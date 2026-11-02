#pragma once

// Nicht-blockierend: oeffnet das Einstellungsfenster in einem eigenen Thread (oder holt es nach vorn).
void OpenConfigDialog();
// Schliesst das Fenster und wartet auf den Thread (beim Entladen des Plugins).
void CloseConfigDialog();

// Vom Plugin: oeffnet die Hotkey-Einstellungen von TeamSpeak (Knopf "Hotkeys ..." im Fenster)
void SetHotkeySetupHandler(void (*fn)());
