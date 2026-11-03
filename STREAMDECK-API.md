# VoiceTune – Stream-Deck-Schnittstelle (lokale HTTP-API)

VoiceTune öffnet beim Start in TeamSpeak einen kleinen HTTP-Server, mit dem sich das Plugin von außen steuern lässt, z.B. von einem Stream Deck.

- **Adresse:** `http://127.0.0.1:19470` (fester Port, nur vom eigenen PC erreichbar, nie aus dem Netzwerk)
- **Format:** Antworten sind JSON, Befehle sind `POST`-Anfragen, der Zustand kommt per `GET`.
- **Pflicht-Header für Befehle:** `X-VoiceTune: 1` (beliebiger Wert). Ohne ihn antwortet die API mit `403`. Das schützt davor, dass eine fremde Webseite im Browser Befehle auslöst.

## Zustand abfragen

```
GET /state
```

Antwort:

```json
{"enabled":true,"micMuted":false,"musicVol":100,"micVol":100,"playing":false,"monitor":false,"soundMonitor":true,"fileMonitor":true,"playlist":"","profile":"Abend","preset":"Flach","source":"Spotify"}
```

| Feld | Bedeutung |
|---|---|
| `enabled` | „Musik einspielen“ ist an |
| `micMuted` | Mikrofon ist stumm geschaltet |
| `musicVol` | Musik-Lautstärke in Prozent (0–200) |
| `micVol` | Mikrofon-Lautstärke in Prozent (0–200) |
| `playing` | Der Datei-Player spielt gerade (nicht pausiert) |
| `monitor` | Mithören ist an |
| `soundMonitor` | „Selbst hören“ für Soundboard-Clips ist an |
| `fileMonitor` | „Selbst hören“ für den Datei-Player ist an |
| `playlist` | Name der zuletzt gespeicherten/geladenen Wiedergabeliste (leer = keine) |
| `profile` | Name des gewählten Profils (leer = keines) |
| `preset` | Name des gewählten Equalizer-Presets (leer = eigene Kurve) |
| `source` | Name der gewählten App (für „Musik einspielen“) |

## Befehle

```
POST /cmd/<befehl>
POST /cmd/<befehl>/<argument>      (Argument URL-kodiert, z.B. Bass%2B für „Bass+“)
```

Jeder Befehl antwortet mit dem neuen Zustand (gleiches JSON wie `/state`).

| Befehl | Wirkung |
|---|---|
| `toggle` | „Musik einspielen“ ein/aus umschalten |
| `on` / `off` | „Musik einspielen“ gezielt ein- bzw. ausschalten |
| `micmute` | Mikrofon stumm/laut umschalten (wird nicht gespeichert; im TeamSpeak-Chat erscheint eine Zeile „MIKROFON STUMM/AKTIV“) |
| `musicvol_up` / `musicvol_down` | Musik-Lautstärke um 10 Prozentpunkte erhöhen / senken (0–200) |
| `musicvol/<0-200>` | Musik-Lautstärke absolut, z.B. `musicvol/80` |
| `micvol/<0-200>` | Mikrofon-Lautstärke absolut |
| `monitor` | Mithören ein/aus umschalten |
| `preset/<Name>` | Equalizer-Preset wählen (mitgelieferte: `Flach`, `Bass+`, `Höhen+`, `Sprache`, `Loudness`, `V-Form`, oder eigene Filter; Groß-/Kleinschreibung egal) |
| `profile/<Name>` | Profil anwenden (Quelle, Lautstärken, Mithören, Equalizer, Qualität, Opus) |
| `profile_next` | Nächstes Profil reihum |
| `source/<exe>` | Audio-Quelle wählen, z.B. `source/spotify.exe` |
| `sound/<1-400>` | Soundboard-Clip sofort abspielen (leere Plätze tun nichts; „Musik einspielen“ bleibt, wie es ist) |
| `soundmonitor` | „Selbst hören“ für Soundboard-Clips ein/aus |
| `filemonitor` | „Selbst hören“ für den Datei-Player ein/aus |
| `playlist/<Name>` | Gespeicherte Wiedergabeliste laden (ersetzt die aktuelle Liste, startet nicht von selbst) |
| `soundstop` | Alle Soundboard-Clips stoppen |
| `playpause` | Datei-Player: Pause/Weiter bzw. ersten Titel starten (unabhängig von „Musik einspielen“; wird zur App dazugemischt) |
| `next` / `prev` | Datei-Player: nächster / vorheriger Titel |
| `stop` | Datei-Player stoppen |

Unbekannter Befehl, unbekannter Name oder ungültiges Argument (z.B. `musicvol/999`, `sound/401`): `404` mit `{"error":"unknown command"}`. Fehlender Header: `403`.

## Listen abfragen

Damit sich Knöpfe passend zu den vorhandenen Namen belegen lassen:

| Anfrage | Antwort |
|---|---|
| `GET /presets` | `{"presets":["Flach","Bass+", …],"current":"Flach"}` (mitgelieferte zuerst, dann eigene) |
| `GET /profiles` | `{"profiles":["Abend","Zocken"],"current":"Abend"}` |
| `GET /playlists` | `{"playlists":["Party","Ruhig"],"current":"Party"}` |
| `GET /sounds` | `{"rows":2,"cols":4,"sounds":[{"slot":1,"name":"Hupe","ready":true}, …]}` – nur belegte Knöpfe, `slot` = Knopfnummer (zeilenweise im Raster) |

## Beispiele

**curl** (Git Bash, Linux, macOS):

```
curl http://127.0.0.1:19470/state
curl -X POST -H "X-VoiceTune: 1" http://127.0.0.1:19470/cmd/toggle
curl -X POST -H "X-VoiceTune: 1" http://127.0.0.1:19470/cmd/micmute
curl -X POST -H "X-VoiceTune: 1" http://127.0.0.1:19470/cmd/musicvol_up
curl -X POST -H "X-VoiceTune: 1" http://127.0.0.1:19470/cmd/musicvol/80
curl -X POST -H "X-VoiceTune: 1" http://127.0.0.1:19470/cmd/profile/Abend
curl -X POST -H "X-VoiceTune: 1" http://127.0.0.1:19470/cmd/sound/3
curl http://127.0.0.1:19470/profiles
```

**PowerShell:**

```
Invoke-RestMethod http://127.0.0.1:19470/state
Invoke-RestMethod -Method Post -Headers @{ 'X-VoiceTune' = '1' } http://127.0.0.1:19470/cmd/toggle
Invoke-RestMethod -Method Post -Headers @{ 'X-VoiceTune' = '1' } 'http://127.0.0.1:19470/cmd/preset/Bass%2B'
```

## Am Stream Deck einrichten

Ein Stream-Deck-Knopf muss eine HTTP-**POST**-Anfrage mit dem Header `X-VoiceTune: 1` an `http://127.0.0.1:19470/cmd/<befehl>` schicken.
Die in die Elgato-Software eingebaute Aktion „Website“ öffnet nur eine Seite im Browser (GET) und kann das meines Wissens nicht; dafür braucht es ein Stream-Deck-Plugin,
das HTTP-Anfragen mit eigenen Headern senden kann (z.B. „API Ninja“), oder ein kleines Skript, das per „System → Öffnen“ gestartet wird (z.B. die PowerShell-Zeile oben).
Zum Prüfen, ob die Schnittstelle läuft, reicht im Browser `http://127.0.0.1:19470/state`.

## Hotkeys statt Stream Deck

Dieselben Befehle gibt es als TeamSpeak-Hotkeys (*Einstellungen → Hotkeys → Plugins → VoiceTune*, oder im VoiceTune-Fenster der Knopf „Hotkeys …“): Musik an/aus, Mikrofon stumm, Musik lauter/leiser, Play/Pause, Weiter/Zurück,
Mithören, nächstes Profil, Sound 1–64 und „Alle Sounds stoppen“. Ein Hotkey braucht kein Stream Deck und keinen Port.

## Hinweise

- **Zweites TeamSpeak:** Der Port ist fest. Läuft ein zweites TeamSpeak mit VoiceTune (oder belegt ein anderes Programm den Port), kann die Schnittstelle in diesem TeamSpeak nicht starten. VoiceTune zeigt dann eine
  orange Meldung im TeamSpeak-Chat (beim Start und noch einmal beim ersten Verbinden mit einem Server) und schreibt eine Zeile ins TS3-Log; alles andere im Plugin funktioniert normal. Das Stream Deck steuert nur die Instanz, die den Port zuerst bekommen hat.
- Die Schnittstelle gehört zum Plugin und startet und endet mit ihm (Plugin deaktivieren = Schnittstelle aus).
- Es gibt keine Anmeldung außer dem Header; Schutz bietet vor allem, dass nur der eigene PC (`127.0.0.1`) zugreifen kann.
