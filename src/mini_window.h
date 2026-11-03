#pragma once

// Kleines, rahmenloses Mini-Fenster (immer im Vordergrund): Quelle/Titel, Musik-Lautstaerke, Mikrofon stumm, Musik ein/aus. Eigener Thread; ein zweiter Aufruf
// holt es nach vorn. Ziehen am Hintergrund verschiebt es, die Position wird gemerkt.
void OpenMiniWindow();
void CloseMiniWindow(); // schliesst das Fenster und wartet auf den Thread (beim Entladen des Plugins)
