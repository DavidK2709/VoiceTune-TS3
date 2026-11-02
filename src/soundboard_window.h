#pragma once

// Eigenes, frei skalierbares Soundboard-Fenster (laeuft in einem eigenen Thread, unabhaengig vom Hauptfenster). Nicht-blockierend; ein zweiter Aufruf
// holt das Fenster nach vorn.
void OpenSoundboardWindow();
// Schliesst das Fenster und wartet auf den Thread (beim Entladen des Plugins).
void CloseSoundboardWindow();
