#pragma once

// Nicht-blockierend: oeffnet das Einstellungsfenster in einem eigenen Thread (oder holt es nach vorn).
void OpenConfigDialog();
// Schliesst das Fenster und wartet auf den Thread (beim Entladen des Plugins).
void CloseConfigDialog();
