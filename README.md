# VoiceTune (TeamSpeak 3 Plugin)

*Your Sound. Your Voice. Your Way. Your Tune!*

Mit VoiceTune spielst du Musik und Sounds direkt über dein Mikrofon in TeamSpeak 3 ein. Das kann der Ton einer einzelnen App sein (Spotify, Browser, Discord usw.) oder es können eigene Audio-Dateien sein. Dazu gibt es einen Equalizer, ein Soundboard, einen Voicechanger wie auch Profile.

Von einer App wird nur deren Ton aufgenommen, also keine TeamSpeak-Stimmen und kein Echo. Beim Browser ist das allerdings immer der ganze Browser und nicht nur ein Tab.

**Stream Deck:** Das passende Plugin gibt es hier: [VoiceTune-TS3-Streamdeck-API](https://github.com/DavidK2709/VoiceTune-TS3-Streamdeck-API)


Autoren: David K. X ´Tony S.

## Voraussetzungen

- Windows 10 oder 11
- TeamSpeak 3 Client 3.6.x in der 64-Bit-Version (Plugin-API 26)

Die 32-Bit-Version von TS3, TeamSpeak 5 sowie Windows 7 und 8 werden nicht unterstützt.

Einzelne Apps lassen sich erst ab Windows 10 Version 2004 aufnehmen. Davor nimmt das Plugin den gesamten Ton des Standard-Ausgabegeräts auf, also auch die Stimmen aus TeamSpeak. In dem Fall legst du TeamSpeak am besten auf ein anderes Ausgabegerät oder spielst nur Dateien ab.

## Installieren

Den fertigen Installer bekommst du unter [Releases](https://github.com/DavidK2709/VoiceTune-TS3/releases). Runterladen, TeamSpeak beenden und die Datei doppelklicken, den Rest erledigt TeamSpeak.

Falls das Plugin danach nicht von selbst aktiv ist: *Einstellungen → Plugins → VoiceTune* anhaken.


Wenn du selbst gebaut hast (siehe [Selbst bauen](#selbst-bauen)), geht es auch so, bei beendetem TeamSpeak:

- `powershell -File scripts\install.ps1` kopiert das Plugin in deinen TS3-Ordner und entfernt auch die alte Version.
- `scripts\package.ps1` baut den Installer `build\VoiceTune.ts3_plugin`.

## Was alles drin ist

Geöffnet wird das Fenster über *Plugins → VoiceTune → Steuerung öffnen*.

### Quelle & Mix

Hier wählst du die App aus, die eingespielt werden soll, und schaltest *Musik einspielen* ein oder aus. Die Liste aktualisiert sich von selbst. Apps blendest du per Rechtsklick aus. Startet eine gewählte App neu, verbindet sich das Plugin automatisch wieder.

Musik und Mikrofon haben eigene Lautstärkeregler (0 bis 200 %). Mit *Mithören* hörst du die Musik selbst über TeamSpeak, auf Wunsch auch verzögert. *Mikrofon stumm* sendet nur noch die Musik und ist nach einem Neustart wieder aus.

### Equalizer

10, 31 oder 61 Bänder mit je ±12 dB, ein paar fertige Filter und bis zu 50 eigene. Dazu kommen ein Live-Spektrum und eine Tonspur. Mit *Einstellungen anhören* prüfst du, wie die Musik bei den anderen ankommt.

### Dateien

Audio-Dateien einfach ins Fenster ziehen. Abgespielt wird alles, was Windows dekodieren kann (mp3, wav, flac, m4a usw.). Liegt eine `ffmpeg.exe` im PATH oder neben der DLL, wird sie als Reserve genutzt. Du kannst bis zu 50 Wiedergabelisten speichern.

Dateien brauchen *Musik einspielen* nicht und werden zur App dazugemischt.

### Soundboard

Ein frei belegbares Raster mit bis zu 20 × 20 Knöpfen (maximal 400). Datei auf einen Knopf ziehen, fertig. Jeder Knopf hat Name, Farbe und eine eigene Lautstärke. Clips dürfen bis zu 30 Sekunden lang sein, und es laufen bis zu 8 gleichzeitig.

- **Klick** spielt den Clip ab, **Strg + Klick** spielt ihn nur für dich ab.
- **Rechtsklick** öffnet den Editor.
- **Ziehen** verschiebt einen Knopf oder tauscht zwei Knöpfe.
- **Export / Import** speichert das ganze Board als `.vtboard`. Die Audio-Dateien werden dabei nicht mitkopiert.

Die ersten 64 Knöpfe können einen TeamSpeak-Hotkey bekommen. Das Soundboard lässt sich auch als eigenes Fenster abkoppeln.

Clips laufen auch bei stummem Mikrofon. Bist du aber in TeamSpeak stumm geschaltet oder nicht verbunden, hörst nur du sie. Nachgeholt wird nichts.

### Voicechanger

Verändert nur deine Stimme, Musik und Sounds bleiben unberührt.

- Tonhöhe und Formant jeweils ±12 Halbtöne, dazu Gain und Dry/Wet
- Fertige Stimmen (Tiefe Stimme, Helle Stimme, Monster, Kobold, Spiegel) und bis zu 50 eigene
- 21 ms Latenz, mit der Option für tiefe Stimmen 43 ms

Der Voicechanger ist nach jedem Neustart aus. *Selbst abhören* bitte nur mit Kopfhörern benutzen, sonst gibt es eine Rückkopplung.

### Profile

Ein Profil speichert Quelle, Lautstärken, Equalizer, Qualität, Voicechanger-Stimme und das Soundboard-Raster. Bis zu 30 Stück sind möglich. Oben im Fenster blätterst du mit ◀ ▶ durch.

Profile lassen sich auch an einen Server oder Kanal koppeln und werden dann beim Verbinden oder Kanalwechsel automatisch geladen.

### Einstellungen

Alles Weitere liegt hinter dem Zahnrad oben rechts:

- Sprache (Deutsch oder Englisch), Hotkeys und Tray-Symbol
- Ducking: Die Musik wird leiser, wenn ein Sound läuft oder du sprichst
- Lautstärke von Sounds und Titeln angleichen
- Klang für Opus optimieren und Vorschau, wie die Musik nach dem Codec klingt
- *Diagnose kopieren* und *Systemcheck* für den Fall, dass etwas nicht läuft
- Update-Hinweis: fragt höchstens alle 12 Stunden bei GitHub nach einer neuen Version, lädt aber nichts herunter

Dazu gibt es noch ein kleines Mini-Fenster, das immer im Vordergrund bleibt. Es öffnet sich über das Symbol neben dem Zahnrad.

### Hotkeys

Unter *Einstellungen → Hotkeys → Plugins → VoiceTune* findest du: Musik an/aus, Mikrofon stumm, Lautstärke, Datei-Player, Mithören, Profile, Sound 1 bis 64 und *Alle Sounds stoppen*.

## Stream Deck und API

Am einfachsten geht es mit dem [Stream-Deck-Plugin](https://github.com/DavidK2709/VoiceTune-TS3-Streamdeck-API).

Wer selbst etwas bauen will: VoiceTune öffnet lokal eine kleine HTTP-Schnittstelle auf `127.0.0.1:19470`. Alle Befehle stehen in [STREAMDECK-API.md](STREAMDECK-API.md).

## Wenn es nicht gut klingt oder nicht ankommt

- Stell den Kanal möglichst auf **Opus Music**. Opus Voice ist mono und für Musik nicht gedacht. Das Plugin ändert den Kanal nicht selbst, dafür brauchst du die Rechte.
- Lass den **Musikmodus** an. Ohne ihn startet die Sprachaktivierung nicht von selbst, und die Musik käme erst durch, wenn du sprichst. Solange Musik läuft, schaltet das Plugin auf Dauersenden und stellt danach deine alten Werte wieder her.
- Steht bei dir „TS3 sendet die Musik nicht“, schau ins TS3-Log. Dort steht eine Diagnose-Zeile. Fehlt dir auf dem Server die Talk-Power, kann das Plugin nichts machen.
- Bei Aussetzern stellst du die Qualität unter *Einstellungen → Senden* auf *Stabil*.

## Selbst bauen

Du brauchst MSYS2 (UCRT64) mit `mingw-w64-ucrt-x86_64-gcc`, `-cmake`, `-ninja` und `-opus`. Ohne Opus baut das Plugin auch, dann aber ohne die Opus-Vorschau.

```
set MSYSTEM=UCRT64
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed mingw-w64-ucrt-x86_64-opus && cd /c/Pfad/zum/Projekt && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build"
```

Am Ende liegt `build\VoiceTune_win64.dll` da, dazu die Testprogramme. Die Menü-Icons erzeugst du bei Bedarf mit `python scripts\make_icons.py` neu.

## Tests

Die Tests laufen ohne TeamSpeak und landen als `*_test.exe` in `build\`. Ein paar Beispiele:

```
build\settings_test.exe      # INI speichern und laden
build\commands_test.exe      # Befehle und Hotkeys
build\remote_test.exe        # HTTP-Schnittstelle (Port 19471)
build\logic_test.exe         # Ducking, Lautheit, .vtboard
build\voicechanger_test.exe  # Voicechanger
```

`soundboard_test`, `player_test` und `engine_files_test` brauchen einen Ordner mit Testtönen als Argument: `tone.wav` und `tone.mp3`, je 3 s 440 Hz und dann 3 s 880 Hz.

`ts3_host_test` spielt TeamSpeak nach, lädt die DLL und kann auch das Fenster öffnen:

```
build\ts3_host_test.exe build\VoiceTune_win64.dll [--mix 10 out.raw | --monitor 6 | --toggle | --ui shot.bmp]
```

Die Umgebungsvariablen für weitere Abläufe stehen in `tools/ts3_host_test.cpp`.

## Lizenz

Copyright (C) 2026 David K., Tony S.

GNU General Public License v3.0, siehe [LICENSE](LICENSE).
