
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

// Kleines, rahmenloses Mini-Fenster (immer im Vordergrund): Quelle/Titel, Musik-Lautstaerke, Mikrofon stumm, Musik ein/aus. Eigener Thread; ein zweiter Aufruf
// holt es nach vorn. Ziehen am Hintergrund verschiebt es, die Position wird gemerkt.
void OpenMiniWindow();
void CloseMiniWindow(); // schliesst das Fenster und wartet auf den Thread (beim Entladen des Plugins)
