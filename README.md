# VoiceTune (TeamSpeak 3 Plugin)

*Your Sound. Your Voice. Your Way. Your Tune!*

Spielt den Ton einer Windows-App (Spotify, Browser mit YouTube, Discord …) **oder eigener Audio-Dateien** (mp3, wav, flac, m4a … per Drag & Drop) in dein TeamSpeak-Mikrofon ein.
Von einer App wird nur die gewählte (inkl. Kindprozesse, z.B. Chrome-Audio-Service) aufgenommen – keine TeamSpeak-Stimmen, kein Echo.

Voraussetzungen: Windows 10 / 11, TeamSpeak 3 Client 3.6.x (**64-bit**, Plugin-API 26). Nicht unterstützt: die 32-Bit-Version von TS3 und TeamSpeak 5.
Ab **Windows 10 Version 2004** wird nur die gewählte App aufgenommen. Auf älteren Windows-10-Versionen fällt das Plugin automatisch auf den **Gesamt-Ton** des Standard-Ausgabegeräts zurück
(die Statuszeile weist darauf hin; TeamSpeak-Stimmen anderer würden dann mitgesendet – am besten mit Kopfhörer-Ausgabe auf ein anderes Gerät oder nur Dateien abspielen). Windows 7/8 werden nicht unterstützt.
Das Fenster passt sich kleinen Bildschirmen an (z.B. 1366×768).
Autoren: Dave Swift X Michael Schulz. Früher hieß das Plugin „App Audio Mixer“; eine vorhandene `appaudiomixer.ini` wird beim ersten Start übernommen.

## Bauen
MSYS2 (UCRT64) mit `mingw-w64-ucrt-x86_64-gcc`, `-cmake`, `-ninja` und **`mingw-w64-ucrt-x86_64-opus`** (für die Opus-Vorschau; fehlt die Bibliothek, baut das Plugin ohne Vorschau):

```
set MSYSTEM=UCRT64
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed mingw-w64-ucrt-x86_64-opus && cd /c/Pfad/zum/Projekt && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build"
```

Ergebnis: `build\VoiceTune_win64.dll` (statisch gelinkt, nur Windows-System-DLLs; Media Foundation wird zur Laufzeit geladen), dazu die Testprogramme (siehe unten).
Das App-Icon `APP-ICON.png` wird als Ressource in die DLL eingebettet. Die kleinen Menü-Icons neu erzeugen: `python scripts\make_icons.py` (schreibt nach `assets\icons`).

## Installieren
**TeamSpeak zuerst beenden.** Dann `powershell -File scripts\install.ps1` (kopiert DLL + Icons nach `%APPDATA%\TS3Client\plugins` und **entfernt die alte „App Audio Mixer“-Version**)
oder `scripts\package.ps1` ausführen und `build\VoiceTune.ts3_plugin` doppelklicken. Der Doppelklick-Weg entfernt die alte Version **nicht**: *App Audio Mixer* dann in
*Einstellungen → Plugins* deaktivieren und `AppAudioMixer_win64.dll` samt gleichnamigem Ordner aus `%APPDATA%\TS3Client\plugins` löschen (TeamSpeak vorher beenden).
**Weitergeben:** Es reicht die Datei `build\VoiceTune.ts3_plugin` (enthält DLL, Menü-Icons und Beschreibung, keine weiteren Programme nötig). Der Empfänger doppelklickt sie, TeamSpeak installiert sie und aktiviert sie von selbst (Autoload; sonst *Einstellungen → Plugins → VoiceTune* anhaken).
In TeamSpeak: *Einstellungen → Plugins → VoiceTune* aktivieren, falls es nicht von selbst geschieht (das Plugin hat einen neuen Namen). Im Menü *Plugins* steht es danach mit eigenem kleinen Icon.

## Benutzen
*Plugins → VoiceTune → Steuerung öffnen* öffnet das Fenster. Links die **Seitenleiste** mit fünf Symbolen (die Markierung gleitet beim Wechsel mit, die neue Seite blendet weich ein): **Quelle** (Quelle & Mix) und **EQ** (Equalizer) für das, was du beim Musikhören laufend anfasst, **Dateien** und **Sounds** für Dateien und Jingles sowie **Voicechanger** für deine Stimme. Oben links steht der Titel der aktuellen Seite, **oben in der Mitte das aktuelle Profil** (Name mit **◀ ▶** zum Durchblättern; ein Klick auf den Namen öffnet die Profil-Verwaltung).
Alles, was man einmal einstellt (Sprache, Senden, Profile verwalten, Hotkeys, Erweitert), liegt hinter dem **Zahnrad** oben rechts neben dem Status-Schild (AKTIV/WARTET/AUS): Es öffnet ein Popup über der Oberfläche.

### Seite „Quelle & Mix“
- **Audio-Quelle**: Kachel-Raster aller Apps mit Audio (Icon, Fenstertitel/Songname). Die Liste ist **live** (jede Sekunde): neue Apps erscheinen von selbst, beendete verschwinden,
  Songtitel aktualisieren sich, die übrigen Kacheln rücken weich nach. Spielt eine App gerade Ton, zeigt die Kachel eine kleine animierte Audio-Spur (rein optisch, nicht der echte Pegel).
  Mausrad scrollt. **Rechtsklick auf eine App** blendet sie aus, **Rechtsklick ins Leere** → „Alle Apps einblenden“ (sind Apps ausgeblendet, steht das rechts in der Fußzeile). Die aktive Quelle lässt sich nicht ausblenden.
- **Musik einspielen**: Hauptschalter, klein rechts in der Titelleiste der Karte „Audio-Quelle“ (auch im Plugin-Menü: *Musik einspielen: Ein/Aus*). Daneben steht der Status; das Schild oben rechts zeigt ihn als AKTIV/WARTET/AUS. Bei jedem Umschalten erscheint im Chat ein Banner mit Linie oben und unten:
  **grün** „♪ MUSIK AN · Quelle“ bzw. **dunkelrot** „■ MUSIK AUS · Quelle“ (Quelle = die gewählte App).
  Der Schalter gilt **nur für die App**: Datei-Player und Soundboard laufen unabhängig davon und werden zur App dazugemischt.
  Sendet TeamSpeak die Musik trotz Musikmodus nicht, steht unter dem Schalter in **Orange**: „TS3 sendet die Musik nicht – Hilfe: Einstellungen → Erweitert“. Sonst zeigt die Zeile den normalen Status.
- **Pegel**: zeigt live, was ins Mikrofon gemischt wird.
- **Mithören**: spielt die eingespielte Musik zusätzlich in *deine* TeamSpeak-Ausgabe (Kopfhörer), damit du prüfen kannst, dass alles ankommt.
  Der Regler **Verzögerung** (0–2000 ms) stellt ein, wie spät du sie hörst, z.B. passend zur Verzögerung, mit der die anderen dich hören
  (Netzwerk + Codec, meist 100–300 ms). Kürzer als etwa 40 ms geht technisch nicht. Hörst du die Original-App (z.B. Spotify) ebenfalls direkt über
  die Lautsprecher, überlagern sich beide – dann entweder die App leiser stellen oder die Verzögerung als Echo-Probe nutzen.
  Das Mithören arbeitet nur im aktiven Server-Tab und nur, solange du verbunden bist; es enthält nur die Musik, nicht deine eigene Stimme.
- **Musik-/Mikrofon-Lautstärke**: 0–200 %, rastet bei 100 % ein.
- **Mikrofon stumm** (Mikrofon-Symbol links neben dem Wert der Mikrofon-Lautstärke): deine Stimme wird nicht gesendet, die Musik läuft weiter. Wirkt auch, wenn gerade keine Musik läuft.
  Der Zustand wird **absichtlich nicht gespeichert** – nach einem Neustart ist das Mikrofon wieder an, damit du es nicht versehentlich stumm lässt.
- **Profile**: ein Profil ist ein benannter Schnappschuss von Quelle, Lautstärken, Mithören, Equalizer, Qualität, Opus-Schaltern und der Voicechanger-Stimme (nicht: Stumm, „Musik einspielen“, Musikmodus). Das aktuelle Profil steht **oben in der Mitte des Fensters**; **◀ ▶** blättert durch die Profile und wendet sie sofort an.
  Verwaltet wird es im **Zahnrad-Popup** (Karte *Profile*): **Speichern als …** legt eines an (oder aktualisiert das gewählte), **Umbenennen** und **Löschen** (zweiter Klick zur Sicherheit) gelten für das gewählte. Hast du nach dem Anwenden etwas verändert, zeigt ein orangefarbener Punkt am Namen oben „(geändert)“. Bis zu 30 Profile; per Hotkey „nächstes Profil“ oder Stream Deck wechselst du reihum.

### Seite „Equalizer“
- **Equalizer** mit **10, 31 oder 61 Bändern** (61 = Sechstel-Oktav-Raster, 20 Hz–20 kHz), je ±12 dB. Mit der Maus Bänder ziehen oder eine Kurve über mehrere
  Bänder zeichnen; Rechts-/Doppelklick setzt ein Band auf 0 dB zurück. Beim Wechsel des Rasters wird die Kurve umgerechnet.
- **Filter-Auswahl** (Dropdown über dem Plot): *Standard* (Flach, Bass+, Höhen+, Sprache, Loudness, V-Form) und deine **eigenen Filter**.
  - **Speichern als …**: übernimmt die aktuelle Kurve als neuen Filter. Namen tippen (Strg+V geht), Enter speichert, Esc bricht ab. Ist gerade ein eigener Filter gewählt,
    ist sein Name vorbelegt und markiert: Enter aktualisiert ihn, ein neuer Name legt einen weiteren an (bis 50, Namen bis 28 Zeichen; die Namen der Standard-Filter sind reserviert).
  - **Umbenennen** und **Löschen** gibt es nur für eigene Filter. Löschen fragt per zweitem Klick nach („Wirklich löschen?“, verfällt nach 3 s).
  - Wurde die Kurve nach dem Laden eines Filters verändert, steht „(geändert)“ daneben. Eigene Filter merken sich ihr Raster (10/31/61) und werden beim Laden umgerechnet.
  - Gespeichert wird in `voicetune.ini` (Abschnitt `[Presets]`).
- Hinter den Reglern läuft das **Live-Spektrum** der Musik (nach EQ und Lautstärke), darunter die **Tonspur** (Wellenform der letzten ~10 s in Echtzeit).
- **Einstellungen anhören**: Knopf gedrückt halten, oder daneben den Schalter **Dauerhaft** an – du hörst die Musik mit EQ und Qualität (ohne Verzögerung) in deiner TeamSpeak-Ausgabe.
  Dabei gilt, was wirklich gesendet wird: bei einem Mono-Mikrofon also die Mono-Summe. Der Schalter geht beim Schließen des Fensters wieder aus.
- **Kanal-Codec**: unten wird der Codec deines Kanals angezeigt. *Opus Voice* ist mono und sprachoptimiert; für Musik im Kanal besser **Opus Music** (Kanal bearbeiten →
  Codec, möglichst Qualität 10; braucht Kanal-Rechte). Das Plugin ändert den Kanal nicht selbst.
- Unten: Diagnosezeile (Puffer, Aussetzer, Takt-Korrektur). Dieselben Werte stehen alle 5 s im TS3-Log (Kanal `VoiceTune`).

### Seite „Dateien“
- Audio-Dateien **ins Fenster ziehen** (mehrere gleichzeitig, auch ganze Auswahlen) oder **„Dateien öffnen“**. Es entsteht eine Wiedergabeliste; Nicht-Audio-Dateien werden ignoriert, Duplikate nicht doppelt aufgenommen.
- Bedienung: Zurück / Play-Pause / Stopp / Weiter, **Suchleiste** zum Springen (auch nach dem Ende der Liste), **Wiederholen** (Aus / Alle / Titel), **Liste leeren**, × an einer Zeile entfernt den Titel.
  Zurück springt bei mehr als 3 s Spielzeit zum Anfang des Titels. Titelwechsel sind nahtlos; eine defekte Datei wird mit Meldung übersprungen.
- Die Dateien laufen durch dieselbe Kette wie App-Ton (Lautstärke, Equalizer, Optimierung, Mithören, Musikmodus) und werden **zur App dazugemischt** (App + Datei + Soundboard gleichzeitig).
  Sie brauchen *Musik einspielen* nicht: Play schaltet den Hauptschalter nicht ein, und Ausschalten pausiert die Datei nicht. Eine pausierte Datei blockiert die App nicht.
- **Selbst hören** (Schalter oben rechts, Standard: an): du hörst die Datei zusätzlich auf deinem Windows-Standard-Ausgabegerät. Ist *Mithören* an, hörst du sie stattdessen über TeamSpeak.
- **Wiedergabelisten**: Die aktuelle Liste bleibt über einen Neustart erhalten (sie startet nicht von selbst). Im Kopf der Karte *Wiedergabeliste* wählst du gespeicherte Listen aus dem Dropdown (lädt sie, ersetzt die aktuelle);
  **Speichern als …** sichert die aktuelle Liste (ist eine Liste gewählt, ist ihr Name vorbelegt: Enter aktualisiert sie), **Umbenennen** und **Löschen** (zweiter Klick zur Sicherheit) gelten für die gewählte. Bis zu 50 Listen à 500 Titel,
  gespeichert in `voicetune.ini` (Abschnitte `[Playlist]` und `[Playlists]`). Per Stream Deck: `/cmd/playlist/<Name>`.
- Formate: alles, was Windows (Media Foundation) dekodiert – mp3, wav, flac, m4a/aac, wma, in der Regel auch ogg/opus je nach installierten Windows-Codecs. Ist `ffmpeg.exe` im PATH oder neben der DLL, wird es als Reserve genutzt.

Die Auswahl wird gespeichert; startet die App später neu, verbindet sich das Plugin automatisch wieder.
Bei Browsern wird der gesamte Browser-Ton aufgenommen (nicht nur ein Tab).

### Seite „Sounds“ (Soundboard) und Soundboard-Fenster
- Ein **Raster aus frei belegbaren Knöpfen** wie bei RP Soundboard: oben stellst du **Zeilen** und **Spalten** (bis 20 × 20, höchstens 400 Knöpfe) mit − / + ein und ob die **Tastenkürzel auf den Knöpfen** angezeigt werden. Standard: 2 × 4. Passt das Raster nicht ins Fenster, scrollt es.
- **Knopf anlegen:** einen leeren Knopf (nur ein „+“) anklicken oder eine Audio-Datei darauf ziehen (jedes Format der Dateien-Seite, Clips bis 30 s) – es öffnet sich der **Editor** mit *Name*, *Datei*, *Farbe*, *Lautstärke* (0–200 %, pro Knopf) und *Tastenkürzel*. Mehrere Dateien auf einmal füllen freie Knöpfe der Reihe nach (ohne Editor, Name = Dateiname).
- **Klick** spielt den Clip über Musik und Mikrofon (bis zu 8 gleichzeitig; derselbe Clip startet von vorn). Während er läuft, zeigt der Knopf einen Fortschrittsbalken und „Position / Dauer“. **Rechtsklick** öffnet den Editor (dort auch *Entfernen*). **Ziehen** eines Knopfes auf einen anderen tauscht bzw. verschiebt ihn. **Alle stoppen** beendet alles; der Regler **Lautstärke** unten gilt für alle Knöpfe zusätzlich.
- **Suchen:** das Feld links unten in der Werkzeugleiste dimmt alle Knöpfe, deren Name nicht passt. **Vorhören:** **Strg + Klick** auf einen Knopf (oder *Vorhören* im Editor) spielt den Clip **nur auf deiner eigenen Ausgabe**, nie ins Mikrofon.
- **Export / Import:** *Export …* speichert Raster und alle Knöpfe (Name, Farbe, Lautstärke, Pfad) als `.vtboard`-Datei, *Import …* lädt sie wieder (nach Rückfrage; ersetzt das aktuelle Board). Pfade werden relativ zur Datei gespeichert, so wandert ein Board samt Sound-Ordner auf einen anderen Rechner. Die Audiodateien selbst werden nicht kopiert.
- **Tastenkürzel:** die ersten 64 Knöpfe haben je einen TeamSpeak-Hotkey (*Sound 1 … 64*). Im Editor legst du ihn per **ändern …** fest (öffnet den Aufnahme-Dialog von TeamSpeak), auf dem Knopf steht er, wenn die Anzeige an ist. Alle Hotkeys findest du außerdem unter *Einstellungen → Hotkeys → Plugins → VoiceTune*.
- **Abkoppeln:** der Knopf rechts oben (und *Plugins → VoiceTune → Soundboard öffnen* oder der Knopf auf der Seite *Quelle*) öffnet das Soundboard als **eigenes, frei skalierbares Fenster** – es bleibt offen, auch wenn du das Hauptfenster schließt. Dort gibt es zusätzlich den Schalter **Immer oben** (Fenster bleibt vor anderen Fenstern).
- Clips funktionieren auch ohne Musikquelle und bei stummem Mikrofon; sie gehen durch Optimierung/Begrenzer und werden beim Mithören mitgehört. „Musik einspielen“ bleibt dabei, wie es ist.
- **Selbst hören** (Schalter unten, Standard: an): du hörst die Clips zusätzlich auf deinem Windows-Standard-Ausgabegerät (bei *Mithören* stattdessen über TeamSpeak).
- **Sofort statt Warteschlange:** Bist du in TeamSpeak stumm geschaltet, in einem Kanal ohne Sprechrecht oder nicht verbunden, ruft TeamSpeak das Plugin nicht auf. Clips und Dateien laufen dann trotzdem sofort
  und nach der Uhr weiter (du hörst sie selbst); gesendet wird in dieser Zeit nichts, und es wird auch nichts später nachgeholt.
- Ablage: Pfade, Namen, Farben und Lautstärken stehen in der INI (Abschnitt `[Sounds]`, ein Eintrag je Knopf). Die ersten 64 Clips werden beim Start geladen, die übrigen beim ersten Klick (kurze Verzögerung); bei sehr vielen Clips verwirft das Plugin die am längsten nicht gespielten (Speichergrenze 192 MB) und lädt sie beim nächsten Klick neu. Per Stream Deck: `/cmd/soundid/<id>` über die feste Knopf-ID, die auch beim Verschieben gleich bleibt (oder `/cmd/sound/<1-400>` über den Platz).

### Mini-Fenster und Tray-Symbol
- **Mini-Fenster** (Symbol links neben dem Zahnrad oder *Plugins → VoiceTune → Mini-Fenster öffnen*): ein kleines, rahmenloses Fenster, das immer im Vordergrund bleibt – mit Quelle, Musik-Lautstärke, Mikrofon-stumm-Knopf und „Musik einspielen“. Am Hintergrund ziehen verschiebt es, die Position wird gemerkt, Esc oder × schließt.
- **Tray-Symbol** (Einstellungen → Allgemein → *Tray-Symbol anzeigen*, Standard aus): Linksklick öffnet die Steuerung, Rechtsklick ein Menü (Musik ein/aus, Mikrofon stumm, nächstes Profil, Soundboard, Mini-Fenster).

### Seite „Voicechanger“
Verfremdet deine Stimme live. Musik, Dateien und Soundboard-Clips bleiben davon unberührt, sie werden erst danach eingemischt.
- **Ein/Aus**: Schalter oben rechts auf der Seite. Der Zustand wird **absichtlich nicht gespeichert** – nach einem Neustart klingst du wieder wie du selbst. Solange der Voicechanger aus ist und nicht abgehört wird, bleibt das Mikrofon-Signal unberührt und es entsteht keine Latenz.
- **Stimmen**: Links stehen die mitgelieferten Stimmen (Unverändert, Tiefe Stimme, Helle Stimme, Monster, Kobold, Spiegel) und deine eigenen. Ein Klick wendet eine Stimme an; ein orangefarbener Punkt zeigt, dass die Regler davon abweichen. **Speichern als …** legt eine eigene Stimme an oder aktualisiert sie (bis 50, Namen bis 28 Zeichen; Enter speichert, Esc bricht ab, Strg+V fügt ein), **Umbenennen** und **Löschen** (zweiter Klick zur Sicherheit) gelten für eigene Stimmen.
- **Tonhöhe (Pitch)**: ±12 Halbtöne, die Formanten bleiben dabei stehen. **Formant (Stimmcharakter)**: ±12 Halbtöne, die Tonhöhe bleibt dabei stehen; beides zusammen klingt wie schneller oder langsamer abgespielt.
- **Lautstärke (Gain)**: ±24 dB. **Dry/Wet**: Anteil von Original und Effekt, zeitlich aufeinander ausgerichtet.
- **Stimme spiegeln**: spiegelt das Spektrum um knapp 4 kHz, tiefe Frequenzen werden hoch und umgekehrt. Das klingt sehr fremd und ist schwer verständlich; Inhalt über 4 kHz entfällt.
- **Polarität invertieren**: dreht das Vorzeichen des Effekt-Anteils. Allein ist das nicht hörbar, im Mix mit dem Original gibt es Auslöschungen (bei 50 % Dry/Wet und neutralen Reglern Stille).
- **Latenz**: Der Effekt verzögert deine Stimme um 21 ms. **Tiefe Stimmen: bessere Qualität** verlängert das Analysefenster auf 43 ms: Unter etwa 150 Hz trennt das kurze Fenster die Obertöne nicht mehr sauber, die Tonhöhe wird ungenauer und die Stimme rauer. Bei tiefer Stimme also ausprobieren.
- **Selbst abhören** (0–100 %, 0 = aus): spielt deine Stimme so, wie sie gesendet wird, auf dem Windows-Standard-Ausgabegerät; auch bei ausgeschaltetem Voicechanger, zum Vergleichen. **Nur mit Kopfhörern**, sonst gibt es eine Rückkopplung. Du hörst dich rund 70 ms (mit „Tiefe Stimmen“ rund 90 ms) verzögert.
- **Grenze**: Der Effekt arbeitet im Mikrofon-Callback von TeamSpeak. Liefert TS3 kein Mikrofon (nicht verbunden, stumm geschaltet), gibt es auch nichts abzuhören; die Seite weist darauf hin. Zum Ausprobieren ohne Server: *Einstellungen → Aufnahme → Test starten*.
- **Profile** merken sich die gewählte Stimme (nicht, ob der Voicechanger an ist). Ältere Profile ohne Stimme lassen sie beim Anwenden in Ruhe.

Code: die Seite in `src/voicechanger_page.{h,cpp}`, die Klangverarbeitung in `src/voicechanger.{h,cpp}`, die Stimmen in `src/voice_presets.h`, das Einhängen in `Engine::ProcessVoice`.

### Einstellungen (Zahnrad-Popup)
Das Zahnrad öffnet das Popup, ein zweiter Klick auf das Zahnrad, ein Klick neben das Panel oder **Esc** schließt es. Oben drei Reiter: **Allgemein**, **Audio**, **Profile**. Ein kleiner blauer Punkt am Zahnrad zeigt, dass eine neue Version bereitsteht.
- **Allgemein**: die **Oberflächensprache** (*Automatisch* richtet sich nach Windows – nicht Deutsch → English –, *Deutsch*, *English*; wirkt sofort) und der Knopf **Hotkeys festlegen …**, der die Hotkey-Einstellungen von TeamSpeak öffnet.
  Die Plugin-Menüeinträge im Plugins-Menü und die Hotkey-Beschreibungen folgen der Sprache erst beim nächsten Start von TeamSpeak.
- **Senden**:
  - **Musik immer senden**: überträgt die Musik auch ohne Push-to-Talk/Sprachaktivierung.
  - **Mono einspielen** mischt die Musik als Mono ein.
  - **Qualität** (plugin-intern): *Niedrige Latenz* (40 ms Puffer), *Ausgewogen* (60 ms), *Stabil* (140 ms, für wackelige Rechner/Apps).
- **Wartung** (Reiter Allgemein):
  - **Diagnose kopieren** legt einen Text in die Zwischenablage: Versionen, Einstellungen (**ohne Dateipfade und Namen**), Status, Kanal-Codec, Stream-Deck-Port, die letzten Logzeilen. Den schickst du bei Problemen mit.
  - **Systemcheck …** zeigt eine Ampel für Aufnahme (Pro-App-Aufnahme ab Windows 10 Version 2004), TeamSpeak-Kanal-Codec (*Opus Voice* ist mono → gelb), Stream-Deck-Schnittstelle und Version. Er öffnet sich beim allerersten Start von selbst.
  - **Nach Updates suchen** (Standard an): fragt höchstens alle 12 Stunden bei GitHub nach dem neuesten Release; ist es neuer, steht „Neue Version …“ (anklickbar) in der Fußzeile und am Zahnrad. Es wird nichts heruntergeladen oder installiert.
  - **Tray-Symbol anzeigen** (Standard aus).
- **Fenster merken sich Position und Größe** (Hauptfenster, Soundboard-Fenster, Mini-Fenster); liegt der gemerkte Platz nicht mehr auf einem Bildschirm, öffnen sie am Standardort.
- **Info**: zeigt, ob die Stream-Deck-Schnittstelle läuft („aktiv · Port 19470“) oder der Port belegt ist, dazu die Plugin-Version, die Autoren (**Dave Swift × Michael Schulz**, in Schreibschrift; dezent auch unten rechts in der Fußzeile) und das Motto *Your Sound. Your Voice. Your Way. Your Tune!*.
- **Audio** (Reiter):
  - **Ducking – Musik automatisch leiser:** *Bei Sounds* (Standard an, −12 dB): solange ein Soundboard-Knopf läuft, wird nur die Musik abgesenkt und kommt danach weich zurück. *Wenn du sprichst* (Standard aus, −10 dB): das Plugin erkennt Sprache am Mikrofonpegel (einstellbare Schwelle, Standard −45 dB, 400 ms Nachlauf) und senkt die Musik ab.
  - **Lautstärke angleichen** (Standard aus): *Sounds angleichen* misst jeden Clip beim Laden, *Titel angleichen* misst Datei-Titel beim ersten Abspielen im Hintergrund (die Messung wird gemerkt); beides bringt sie auf den **Zielpegel** (Standard −18 dB). Die Verstärkung ist auf ±12 dB begrenzt und übersteuert nie.
  - **Erweitert** (immer sichtbar): Dinge, die man normalerweise nicht anfasst – deshalb hier und nicht auf den Hauptseiten. Geht beim Musikmodus etwas schief, erscheint auf der Seite *Quelle & Mix* ein orangefarbener Hinweis, der hierher verweist.
  - **Musikmodus** (Standard: an, bitte eingeschaltet lassen): TeamSpeak ruft das Plugin *nach* seiner Vorverarbeitung auf. Die Sprachaktivierung startet daher nie von selbst eine Übertragung, nur weil
    das Plugin Musik einmischt: Die Musik käme erst durch, wenn du einmal sprichst. Solange Musik läuft, schaltet das Plugin deshalb – wie ein Soundboard-Plugin – auf **Dauersenden**:
    Sprachaktivierung aus, Mikrofon-Eingang aktiv (auch bei Push-to-Talk), Änderungen sofort an TS3 übergeben; das alles im Mikrofon-Callback. Dazu schaltet es
    Rauschfilter und Auto-Pegel ab, weil sie Musik verschlechtern. Etwa 1 s nach dem Ende der Musik stellt es exakt deine bisherigen Werte wieder her (auch beim Beenden). Das Plugin-eigene **Mikro-Gate**
    (Schwelle −70…−25 dB, −70 = aus) hält währenddessen Tastatur- und Raumgeräusche vom Mikrofon fern. Voraussetzung ist „Musik immer senden“.
    Jeder „Test starten“ in den Aufnahme-Einstellungen läuft auf einer **eigenen, neuen Verbindung** (im TS3-Log `schid 2`, `3`, …; dein Server ist meist `schid 1`) mit eigener Vorverarbeitung:
    der Musikmodus folgt dem automatisch, setzt die alte Verbindung zurück und schaltet die neue um.
    Kontrolle: Unter dem Schalter steht, ob TS3 die Musik wirklich sendet („TS3 sendet nicht“ in Orange: dann steht im TS3-Log eine Zeile „Diagnose“ mit vad, Eingang, Stumm und Talk-Power der Verbindung).
    Hinweis: Das Plugin kann nur umgehen, was die Client-Seite entscheidet. Fehlt dir auf dem Server die Sprech-Berechtigung (Talk-Power), oder blockt ein anderes Plugin, kommt nichts an.
  - **Klang für Opus optimieren** (Standard: an): bereitet die Musik auf die Übertragung vor, damit der Codec möglichst wenig kaputt macht: Hochpass bei 25 Hz (Gleichanteil und Rumpeln kosten nur Bits),
    **phasensichere Mono-Summe** (gegenphasige Stereo-Anteile löschen sich bei der einfachen Summe aus – sie werden erkannt, der Pegel bleibt erhalten) und ein weicher **Begrenzer** (≈ −1,7 dBFS), weil Opus an Pegelspitzen hörbar verzerrt.
    Die Mono-Summe gilt bei Mono-Mikrofon, bei „Mono einspielen“ und bei einem Opus-Voice-Kanal; sonst bleibt Stereo erhalten (nur Hochpass + Begrenzer). Intern bleibt die Musik bis zu diesem Punkt Stereo (Equalizer, Pegel, Tonspur).
  - **Vorschau wie Opus** (Standard: an): *Mithören* und *Einstellungen anhören* schicken die Musik lokal durch einen Opus-Encoder/-Decoder, damit du hörst, wie sie nach dem Codec klingt
    (Opus Voice = Mono, Opus Music = Stereo). Die Bitrate liest das Plugin von TeamSpeak (`getEncodeConfigValue`), alle 2 s aktualisiert; die Statuszeile zeigt z.B. „Opus Voice · Mono · 34 kbit/s“.
    Meldet TS3 keine Bitrate (z.B. bei „Test starten“), steht dort „(geschätzt)“ – dann wird sie aus der Kanalqualität abgeschätzt.
    **Das ist eine Näherung, kein bitgenaues Abbild:** TeamSpeaks genaue Encoder-Einstellungen (Komplexität, Paketverlust, Bandbreite) sind nicht bekannt. Die Vorschau fügt ca. 20 ms Verzögerung hinzu.
    Auf die tatsächlich gesendete Musik hat sie keinen Einfluss.
    **Grenze:** Ist der Kanal auf *Opus Voice* gestellt, bleibt die Übertragung **mono** (SDK: 48 kHz, mono) – für alle Hörer. Stereo kommt nur mit dem Kanal-Codec *Opus Music* an.

### Profile: Automatik
Im Reiter **Profile** koppelst du das gewählte Profil an einen Server (eindeutige Server-ID, bleibt auch bei Umbenennung stabil) und optional einen Kanal: *Mit diesem Server koppeln* / *Server + Kanal koppeln* (du musst verbunden sein), *Kopplung lösen*. Beim Verbinden, Server-Tab-Wechsel und Kanalwechsel wendet das Plugin das passende Profil an und meldet es im Chat (ein Profil mit Kanal geht vor dem für den ganzen Server). *Profil nach Server und Kanal wählen* schaltet die Automatik ab. Außerdem sind die Profile auch per Hotkey erreichbar: *Profil 1 … 10 anwenden*. Ein Profil speichert jetzt auch Soundboard-Raster und Sound-Lautstärke.

### Hotkeys
Im TeamSpeak-Hotkey-Dialog (*Einstellungen → Hotkeys → Plugins → VoiceTune* – oder über „Hotkeys …“ im Fenster) gibt es: Musik an/aus, Mikrofon stumm/laut, Musik lauter/leiser, Datei-Player Play/Pause, nächster/vorheriger Titel, Mithören an/aus, nächstes Profil, Profil 1–10 anwenden, Sound 1–64, Alle Sounds stoppen.
Beim Stumm-Hotkey erscheint eine Chat-Zeile „MIKROFON STUMM/AKTIV“.

### Stream Deck / Fernsteuerung
Das Plugin lässt sich über eine lokale HTTP-Schnittstelle (`127.0.0.1:19470`) steuern: Musik an/aus, Mikrofon stumm, Lautstärke (relativ und absolut), Mithören, EQ-Preset, Profil, Quelle, Soundboard und Datei-Player; dazu Listen der Presets, Profile und Sounds.
Befehle, Beispiele und Hinweise: [STREAMDECK-API.md](STREAMDECK-API.md).

### Tipps für guten Klang
- Für Musik im Kanal-Codec **Opus Music** wählen; der Voice-Codec klingt für Musik schlecht und ist mono.
- **Sprachaktivierung** schneidet leise Stellen ab; dafür hält das Plugin das Sende-Signal 300 ms nach. Bei Problemen auf *Dauersenden* umstellen.
- Der Audio-Puffer (≈ 60 ms) gleicht Jitter und Takt-Unterschiede zwischen App-Audio und Mikrofon aus; Aussetzer zeigt die Diagnosezeile.

## Testen ohne TeamSpeak
Alle Selbsttests laufen ohne TeamSpeak und enden mit „alle Tests ok“ bzw. Exit-Code 0:
```
build\mixer_test.exe                               # Mixer / Ring-Buffer
build\eq_test.exe                                  # Equalizer (10/31/61 Bänder), FFT, Tonspur-Puffer
build\settings_test.exe                            # INI-Roundtrip (Einstellungen, eigene Presets, Profile, Sounds)
build\commands_test.exe                            # gemeinsame Befehle (Hotkeys, Stream-Deck-API)
build\remote_test.exe                              # Stream-Deck-Schnittstelle ueber echtes HTTP (Port 19471): /sounds, /state, feste Knopf-IDs
build\soundboard_test.exe build\pt                # Soundboard (Mischen, Stimmen, Laden aus Dateien, Angleichen, Vorhören)
build\logic_test.exe                               # Ducking, Sprach-Erkennung, Lautheit, Soundboard-Datei (.vtboard), Update-Vergleich
build\nowplaying_test.exe                          # Titelbereinigung für „Titel melden“
build\i18n_test.exe                                # Übersetzungstabelle Deutsch/English
build\voicechanger_test.exe                        # Voicechanger: Tonhöhe, Formant, Gain, Spiegeln, Dry/Wet, Latenz, Umschalten, Stimmen, Engine
build\playlist_test.exe                            # Wiedergabeliste
build\jitter_test.exe                              # Simulation: alte vs. neue Puffer-Logik, Mithör-Verzögerung
build\optimizer_test.exe                           # Klang-Optimierung: Mono-Summe, Hochpass, Begrenzer
build\opus_test.exe                                # Opus-Vorschau: Bitrate, Bandbreite, Verzögerung, Mono/Stereo
build\player_test.exe build\pt                     # Datei-Player (tone.wav/mp3/flac/… in build\pt: 3 s 440 Hz, dann 3 s 880 Hz)
build\engine_files_test.exe build\pt               # Engine in Echtzeit: Dateien, Springen, Stumm, Opus-Vorschau, Soundboard, Profile, Now-Playing (dauert ~1,5 Minuten)
build\capture_test.exe --list                      # erkannte Apps; capture_test.exe spotify.exe 5 spotify.wav nimmt 5 s auf
build\ts3_host_test.exe build\VoiceTune_win64.dll [--mix 10 out.raw | --monitor 6 | --toggle | --ui shot.bmp]
```
`ts3_host_test` stellt TeamSpeak nach: lädt die DLL, ruft die Exports in Client-Reihenfolge auf und treibt den Mikrofon-Callback im 20-ms-Takt (`--mix`),
zusätzlich den Wiedergabe-Callback für das Mithören (`--monitor`), löst die Menü-Aktion samt Chat-Meldung aus (`--toggle`) oder öffnet das Fenster und speichert Screenshots (`--ui`).
Mit den Umgebungsvariablen `AAM_FILES=<Ordner>` (Dateien-Seite per simuliertem Drop), `AAM_UI3=<Ordner>` (Profile, Soundboard mit Editor, Ziehen, Raster und Soundboard-Fenster; Exit-Code 1 bei Abweichung) und `AAM_SET=1` (alle Seiten und das Einstellungs-Popup auf Deutsch und Englisch fotografieren, jedes Element des Popups bedienen und die gespeicherten Werte prüfen; Exit-Code 1 bei Abweichung) werden bestimmte Abläufe geprüft bzw. fotografiert
(`AAM_HOLD=<Sekunden>` lässt das Fenster zum Anschauen offen, `AAM_LIVE=<Sekunden>` lässt es bedienbar offen, um es selbst auszuprobieren);
`AAM_BITRATE=<bit/s>` setzt die vom nachgestellten TS3 gemeldete Opus-Bitrate. Das Testfenster ist für echte Maus- und Tastatureingaben gesperrt; die Einstellungen landen in `%TEMP%`, nicht in deinem TS3-Ordner.


## Lizenz
Copyright (C) 2026 David K., Tony S.
Dieses Projekt steht unter der GNU General Public License v3.0 (siehe LICENSE).