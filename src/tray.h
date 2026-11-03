#pragma once

// Tray-Symbol (Infobereich der Taskleiste) mit Schnellzugriff: Linksklick oeffnet die Steuerung, Rechtsklick ein Menue (Musik ein/aus, Mikrofon stumm, naechstes Profil,
// Soundboard, Mini-Fenster). Laeuft in einem eigenen Thread; TraySetEnabled(false) entfernt das Symbol und wartet auf den Thread.
void TraySetEnabled(bool on);
bool TrayIsEnabled();
