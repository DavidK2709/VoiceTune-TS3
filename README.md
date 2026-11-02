# VoiceTune (TeamSpeak 3 Plugin)

*Your Sound. Your Voice. Your Way. Your Tune!*

Spielt den Ton einer Windows-App (Spotify, Browser mit YouTube, Discord …) **oder eigener Audio-Dateien** (mp3, wav, flac, m4a … per Drag & Drop) in dein TeamSpeak-Mikrofon ein.
Von einer App wird nur die gewählte (inkl. Kindprozesse, z.B. Chrome-Audio-Service) aufgenommen – keine TeamSpeak-Stimmen, kein Echo.

Voraussetzungen: Windows 10 / 11, TeamSpeak 3 Client 3.6.x (**64-bit**, Plugin-API 26). Nicht unterstützt: die 32-Bit-Version von TS3 und TeamSpeak 5.
Ab **Windows 10 Version 2004** wird nur die gewählte App aufgenommen. Auf älteren Windows-10-Versionen fällt das Plugin automatisch auf den **Gesamt-Ton** des Standard-Ausgabegeräts zurück
(die Statuszeile weist darauf hin; TeamSpeak-Stimmen anderer würden dann mitgesendet – am besten mit Kopfhörer-Ausgabe auf ein anderes Gerät oder nur Dateien abspielen). Windows 7/8 werden nicht unterstützt.
Das Fenster passt sich kleinen Bildschirmen an (z.B. 1366×768).
Autor: Michael Schulz. Früher hieß das Plugin „App Audio Mixer“; eine vorhandene `appaudiomixer.ini` wird beim ersten Start übernommen.

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
*Plugins → VoiceTune → Einstellungen…* öffnet das Fenster mit vier Seiten. Oben rechts wechselst du die Sprache (**DE | EN**), daneben öffnet **„Hotkeys …“** die Hotkey-Einstellungen von TeamSpeak.
Die Oberfläche gibt es auf Deutsch und Englisch; „automatisch“ richtet sich nach der Windows-Sprache (nicht Deutsch → English). Die Plugin-Menüeinträge im Plugins-Menü und die Hotkey-Beschreibungen folgen der Sprache beim nächsten Start von TeamSpeak.

### Seite „Quelle & Mix“
- **Audio-Quelle**: Kachel-Raster aller Apps mit Audio (Icon, Fenstertitel/Songname). Die Liste ist **live** (jede Sekunde): neue Apps erscheinen von selbst, beendete verschwinden,
  Songtitel aktualisieren sich, die übrigen Kacheln rücken weich nach. Spielt eine App gerade Ton, zeigt die Kachel eine kleine animierte Audio-Spur (rein optisch, nicht der echte Pegel).
  Mausrad scrollt. **Rechtsklick auf eine App** blendet sie aus, **Rechtsklick ins Leere** → „Alle Apps einblenden“. Die aktive Quelle lässt sich nicht ausblenden.
- **Musik einspielen**: Hauptschalter (auch im Plugin-Menü: *Musik einspielen: Ein/Aus*). Bei jedem Umschalten erscheint im Chat ein Banner mit Linie oben und unten:
  **grün** „♪ MUSIK AN · Quelle“ bzw. **dunkelrot** „■ MUSIK AUS · Quelle“. Läuft gerade eine Datei, steht ihr Titel statt der App im Banner.
- **Pegel**: zeigt live, was ins Mikrofon gemischt wird.
- **Mithören**: spielt die eingespielte Musik zusätzlich in *deine* TeamSpeak-Ausgabe (Kopfhörer), damit du prüfen kannst, dass alles ankommt.
  Der Regler **Verzögerung** (0–2000 ms) stellt ein, wie spät du sie hörst, z.B. passend zur Verzögerung, mit der die anderen dich hören
  (Netzwerk + Codec, meist 100–300 ms). Kürzer als etwa 40 ms geht technisch nicht. Hörst du die Original-App (z.B. Spotify) ebenfalls direkt über
  die Lautsprecher, überlagern sich beide – dann entweder die App leiser stellen oder die Verzögerung als Echo-Probe nutzen.
  Das Mithören arbeitet nur im aktiven Server-Tab und nur, solange du verbunden bist; es enthält nur die Musik, nicht deine eigene Stimme.
- **Musik-/Mikrofon-Lautstärke**: 0–200 %, rastet bei 100 % ein.
- **Mikrofon stumm** (Mikrofon-Symbol links neben dem Wert der Mikrofon-Lautstärke): deine Stimme wird nicht gesendet, die Musik läuft weiter. Wirkt auch, wenn gerade keine Musik läuft.
  Der Zustand wird **absichtlich nicht gespeichert** – nach einem Neustart ist das Mikrofon wieder an, damit du es nicht versehentlich stumm lässt.
- **Musik immer senden**: überträgt die Musik auch ohne Push-to-Talk/Sprachaktivierung.
- **Profile** (rechts unten): ein Profil ist ein benannter Schnappschuss von Quelle, Lautstärken, Mithören, Equalizer, Qualität und Opus-Schaltern (nicht: Stumm, „Musik einspielen“, Musikmodus). **◀ ▶** blättert durch die Profile und wendet sie sofort an,
  **Speichern als …** legt eines an (oder aktualisiert das gewählte), **Umbenennen** und **Löschen** (zweiter Klick zur Sicherheit) gelten für das gewählte. Hast du nach dem Anwenden etwas verändert, steht „(geändert)“ daneben. Bis zu 30 Profile; per Hotkey „nächstes Profil“ oder Stream Deck wechselst du reihum.
- **Titel melden** (links unten): bei einem Titelwechsel (Dateititel oder Titel im Fenster der App, z.B. „Artist - Song“ bei Spotify) erscheint eine Zeile *♪ Spielt: …* – standardmäßig im **eigenen Chat**. Optional **im Kanal-Chat**
  (alle im Kanal sehen es, deshalb standardmäßig aus) oder **in den Client-Metadaten** (dein TeamSpeak-Metadatenfeld, beim Ausschalten wird der alte Wert wiederhergestellt). Gemeldet wird erst, wenn der Titel 3 s stabil ist, nie doppelt, und nur bei eingeschalteter Musik.

### Seite „Equalizer & Qualität“
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
- **Musikmodus** (Standard: an, bitte eingeschaltet lassen): TeamSpeak ruft das Plugin *nach* seiner Vorverarbeitung auf. Die Sprachaktivierung startet daher nie von selbst eine Übertragung, nur weil
  das Plugin Musik einmischt: Die Musik käme erst durch, wenn du einmal sprichst. Solange Musik läuft, schaltet das Plugin deshalb – wie ein Soundboard-Plugin – auf **Dauersenden**:
  Sprachaktivierung aus, Mikrofon-Eingang aktiv (auch bei Push-to-Talk), Änderungen sofort an TS3 übergeben; das alles im Mikrofon-Callback. Dazu schaltet es
  Rauschfilter und Auto-Pegel ab, weil sie Musik verschlechtern. 2,5 s nach dem Ende der Musik stellt es exakt deine bisherigen Werte wieder her (auch beim Beenden). Das Plugin-eigene **Mikro-Gate**
  (Schwelle −70…−25 dB, −70 = aus) hält währenddessen Tastatur- und Raumgeräusche vom Mikrofon fern. Voraussetzung ist „Musik immer senden“.
  Jeder „Test starten“ in den Aufnahme-Einstellungen läuft auf einer **eigenen, neuen Verbindung** (im TS3-Log `schid 2`, `3`, …; dein Server ist meist `schid 1`) mit eigener Vorverarbeitung:
  der Musikmodus folgt dem automatisch, setzt die alte Verbindung zurück und schaltet die neue um.
  Kontrolle: Unter dem Schalter steht, ob TS3 die Musik wirklich sendet („TS3 sendet nicht“ in Orange: dann steht im TS3-Log eine Zeile „Diagnose“ mit vad, Eingang, Stumm und Talk-Power der Verbindung).
  Hinweis: Das Plugin kann nur umgehen, was die Client-Seite entscheidet. Fehlt dir auf dem Server die Sprech-Berechtigung (Talk-Power), oder blockt ein anderes Plugin, kommt nichts an.
- **Kanal-Codec**: unten wird der Codec deines Kanals angezeigt. *Opus Voice* ist mono und sprachoptimiert; für Musik im Kanal besser **Opus Music** (Kanal bearbeiten →
  Codec, möglichst Qualität 10; braucht Kanal-Rechte). Das Plugin ändert den Kanal nicht selbst.
- **Qualität** (plugin-intern): *Niedrige Latenz* (40 ms Puffer), *Ausgewogen* (60 ms), *Stabil* (140 ms, für wackelige Rechner/Apps). **Mono einspielen** mischt die Musik als Mono ein.
- **Klang für Opus optimieren** (Standard: an): bereitet die Musik auf die Übertragung vor, damit der Codec möglichst wenig kaputt macht: Hochpass bei 25 Hz (Gleichanteil und Rumpeln kosten nur Bits),
  **phasensichere Mono-Summe** (gegenphasige Stereo-Anteile löschen sich bei der einfachen Summe aus – sie werden erkannt, der Pegel bleibt erhalten) und ein weicher **Begrenzer** (≈ −1,7 dBFS), weil Opus an Pegelspitzen hörbar verzerrt.
  Die Mono-Summe gilt bei Mono-Mikrofon, bei „Mono einspielen“ und bei einem Opus-Voice-Kanal; sonst bleibt Stereo erhalten (nur Hochpass + Begrenzer). Intern bleibt die Musik bis zu diesem Punkt Stereo (Equalizer, Pegel, Tonspur).
- **Vorschau wie Opus** (Standard: an): *Mithören* und *Einstellungen anhören* schicken die Musik lokal durch einen Opus-Encoder/-Decoder, damit du hörst, wie sie nach dem Codec klingt
  (Opus Voice = Mono, Opus Music = Stereo). Die Bitrate liest das Plugin von TeamSpeak (`getEncodeConfigValue`), alle 2 s aktualisiert; die Statuszeile zeigt z.B. „Opus Voice · Mono · 34 kbit/s“.
  Meldet TS3 keine Bitrate (z.B. bei „Test starten“), steht dort „(geschätzt)“ – dann wird sie aus der Kanalqualität abgeschätzt.
  **Das ist eine Näherung, kein bitgenaues Abbild:** TeamSpeaks genaue Encoder-Einstellungen (Komplexität, Paketverlust, Bandbreite) sind nicht bekannt. Die Vorschau fügt ca. 20 ms Verzögerung hinzu.
  Auf die tatsächlich gesendete Musik hat sie keinen Einfluss.
  **Grenze:** Ist der Kanal auf *Opus Voice* gestellt, bleibt die Übertragung **mono** (SDK: 48 kHz, mono) – für alle Hörer. Stereo kommt nur mit dem Kanal-Codec *Opus Music* an.
- Unten: Diagnosezeile (Puffer, Aussetzer, Takt-Korrektur). Dieselben Werte stehen alle 5 s im TS3-Log (Kanal `VoiceTune`).

### Seite „Dateien“
- Audio-Dateien **ins Fenster ziehen** (mehrere gleichzeitig, auch ganze Auswahlen) oder **„Dateien öffnen“**. Es entsteht eine Wiedergabeliste; Nicht-Audio-Dateien werden ignoriert, Duplikate nicht doppelt aufgenommen.
- Bedienung: Zurück / Play-Pause / Stopp / Weiter, **Suchleiste** zum Springen (auch nach dem Ende der Liste), **Wiederholen** (Aus / Alle / Titel), **Liste leeren**, × an einer Zeile entfernt den Titel.
  Zurück springt bei mehr als 3 s Spielzeit zum Anfang des Titels. Titelwechsel sind nahtlos; eine defekte Datei wird mit Meldung übersprungen.
- Die Dateien laufen durch dieselbe Kette wie App-Ton (Lautstärke, Equalizer, Optimierung, Mithören, Musikmodus). Solange eine Datei läuft, ersetzt sie die App-Quelle; der Hauptschalter *Musik einspielen* pausiert sie.
- Formate: alles, was Windows (Media Foundation) dekodiert – mp3, wav, flac, m4a/aac, wma, in der Regel auch ogg/opus je nach installierten Windows-Codecs. Ist `ffmpeg.exe` im PATH oder neben der DLL, wird es als Reserve genutzt.

Die Auswahl wird gespeichert; startet die App später neu, verbindet sich das Plugin automatisch wieder.
Bei Browsern wird der gesamte Browser-Ton aufgenommen (nicht nur ein Tab).

### Seite „Sounds“ (Soundboard)
- 8 Plätze für kurze Clips (Jingles, bis 30 s, jedes Format der Dateien-Seite). Eine Datei auf eine Kachel ziehen belegt sie, mehrere Dateien füllen freie Plätze; eine leere Kachel anklicken öffnet den Datei-Dialog.
- **Klick** spielt den Clip über Musik und Mikrofon (bis zu 4 gleichzeitig; derselbe Clip startet von vorn). **Rechtsklick**: Umbenennen, Datei ersetzen, Entfernen. **Alle stoppen** beendet alles. Der Regler **Sound-Lautstärke** (0–200 %) gilt für alle Clips.
- Clips funktionieren auch ohne Musikquelle und bei stummem Mikrofon; sie gehen durch Optimierung/Begrenzer und werden beim Mithören mitgehört. Beim ersten Abspielen schaltet sich „Musik einspielen“ bei Bedarf ein.
- Hotkeys *Sound 1 … 8* und *Alle Sounds stoppen*, per Stream Deck `/cmd/sound/<1-8>`. Die Zuordnung (Pfade und Namen) wird gespeichert; Clips werden beim Start neu geladen.

### Hotkeys
Im TeamSpeak-Hotkey-Dialog (*Einstellungen → Hotkeys → Plugins → VoiceTune* – oder über „Hotkeys …“ im Fenster) gibt es: Musik an/aus, Mikrofon stumm/laut, Musik lauter/leiser, Datei-Player Play/Pause, nächster/vorheriger Titel, Mithören an/aus, nächstes Profil, Sound 1–8, Alle Sounds stoppen.
Beim Stumm-Hotkey erscheint eine Chat-Zeile „MIKROFON STUMM/AKTIV“.

### Stream Deck / Fernsteuerung
Das Plugin lässt sich über eine lokale HTTP-Schnittstelle (`127.0.0.1:19470`) steuern: Musik an/aus, Mikrofon stumm, Lautstärke (relativ und absolut), Mithören, EQ-Preset, Profil, Quelle, Soundboard und Datei-Player; dazu Listen der Presets, Profile und Sounds.
Befehle, Beispiele und Hinweise: [STREAMDECK-API.md](STREAMDECK-API.md).

### Tipps für guten Klang
- Für Musik im Kanal-Codec **Opus Music** wählen; der Voice-Codec klingt für Musik schlecht und ist mono.
- **Sprachaktivierung** schneidet leise Stellen ab; dafür hält das Plugin das Sende-Signal 600 ms nach. Bei Problemen auf *Dauersenden* umstellen.
- Der Audio-Puffer (≈ 60 ms) gleicht Jitter und Takt-Unterschiede zwischen App-Audio und Mikrofon aus; Aussetzer zeigt die Diagnosezeile.

## Testen ohne TeamSpeak
Alle Selbsttests laufen ohne TeamSpeak und enden mit „alle Tests ok“ bzw. Exit-Code 0:
```
build\mixer_test.exe                               # Mixer / Ring-Buffer
build\eq_test.exe                                  # Equalizer (10/31/61 Bänder), FFT, Tonspur-Puffer
build\settings_test.exe                            # INI-Roundtrip (Einstellungen, eigene Presets, Profile, Sounds)
build\commands_test.exe                            # gemeinsame Befehle (Hotkeys, Stream-Deck-API)
build\soundboard_test.exe build\pt                # Soundboard (Mischen, Stimmen, Laden aus Dateien)
build\nowplaying_test.exe                          # Titelbereinigung für „Titel melden“
build\i18n_test.exe                                # Übersetzungstabelle Deutsch/English
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
Mit den Umgebungsvariablen `AAM_FILES=<Ordner>` (Dateien-Seite per simuliertem Drop), `AAM_NEW=1` (Stumm-Knopf, Opus-Zeile), `AAM_UI3=<Ordner>` (Profile, Soundboard) und `AAM_EN=1` (alle Seiten auf Englisch) werden bestimmte Abläufe fotografiert
(`AAM_HOLD=<Sekunden>` lässt das Fenster zum Anschauen offen);
`AAM_BITRATE=<bit/s>` setzt die vom nachgestellten TS3 gemeldete Opus-Bitrate. Das Testfenster ist für echte Maus- und Tastatureingaben gesperrt; die Einstellungen landen in `%TEMP%`, nicht in deinem TS3-Ordner.
