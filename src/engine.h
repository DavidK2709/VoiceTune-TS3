#pragma once
#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "analysis.h"
#include "equalizer.h"
#include "file_player.h"
#include "gate.h"
#include "i18n.h"
#include "jitter_reader.h"
#include "opus_preview.h"
#include "playlist.h"
#include "process_capture.h"
#include "ring_buffer.h"
#include "settings.h"
#include "local_out.h"
#include "ducker.h"
#include "loudness.h"
#include "soundboard.h"
#include "voice_optimizer.h"

// Kodierung des aktuellen Kanals, wie sie TeamSpeak sendet (fuer die Opus-Vorschau)
struct EncodeInfo {
    int  channels   = 1;     // 1 = Opus Voice (mono), 2 = Opus Music (stereo)
    int  bitrateBps = 32000;
    bool exact      = false; // false: aus der Kanalqualitaet geschaetzt
};

// Zustand des Datei-Players fuer die Oberflaeche (eine Momentaufnahme)
struct PlayerSnapshot {
    std::vector<Track> items;
    int                current  = -1;
    bool               active   = false; // laeuft oder ist pausiert
    bool               paused   = false;
    double             position = 0, duration = -1; // Sekunden des aktuellen Titels
    RepeatMode         repeat   = RepeatMode::Off;
    std::wstring       message; // z.B. "Nicht abspielbar: ..."
};

// Verbindet Einstellungen, App-Capture und Mikrofon-Mix. Ein Watcher-Thread haelt die Aufnahme der gewaehlten App
// am Laufen (startet sie, sobald die App Audio hat bzw. nach einem Neustart der App) und besitzt das ProcessCapture-Objekt.
class Engine {
public:
    static Engine& Get();

    void Init(const std::wstring& configDir);
    void Shutdown();

    void     SetEnabled(bool on);
    void     SetTarget(const std::wstring& exeLower);
    void     SetMusicVolume(int pct);
    void     SetMicVolume(int pct);
    // Mikrofon stumm: deine Stimme wird nicht gesendet, Musik/Dateien laufen weiter. Wird absichtlich nicht gespeichert (beim Laden immer "an").
    void     SetMicMuted(bool on) { micMuted_ = on; }
    bool     IsMicMuted() const { return micMuted_.load(); }
    void     SetAlwaysSend(bool on);
    Settings GetSettings();
    std::wstring Status();

    // Aus ts3plugin_onEditCapturedVoiceDataEvent (TS3-Audio-Thread). `schid` = Server-Verbindung bzw. Test-Verbindung, fuer die der Block kommt.
    void Mix(uint64_t schid, short* samples, int frames, int channels, int* edited);

    // Musikmodus (siehe Settings): TS3-Sprachfilter waehrend der Musik umgehen, plugin-eigenes Mikro-Gate.
    void SetMusicMode(bool on);
    void SetGateDb(int db);
    // Vom Plugin bereitgestellt: schaltet die TS3-Vorverarbeitung. mask 0 = alles wiederherstellen, Bit 1 = Rauschfilter/Auto-Pegel/Echo aus,
    // Bit 2 = Sprachaktivierung aus. Rueckgabe: die Bits, die tatsaechlich umgeschaltet wurden (0 = nichts geaendert/Fehler). Watcher-Thread.
    void SetPreprocessCallback(unsigned (*fn)(uint64_t schid, unsigned mask)) { preCb_ = fn; }
    // Vom Plugin bereitgestellt: Kanal-Codec des aktuellen Kanals als Text ("Opus Voice, Qualität 7"), leer wenn unbekannt.
    void SetCodecCallback(std::wstring (*fn)(uint64_t schid)) { codecCb_ = fn; }
    // Vom Plugin bereitgestellt: aktueller TS3-Zustand der Verbindung (vad, Eingang, stumm, Talk-Power ...) als Text, fuer die Diagnose im Log.
    void SetDiagCallback(std::string (*fn)(uint64_t schid)) { diagCb_ = fn; }
    // Vom Plugin bereitgestellt: Kodierung des aktuellen Kanals (Kanalzahl, Bitrate). false = unbekannt (kein Opus-Kanal / keine Verbindung).
    void SetEncodeCallback(bool (*fn)(uint64_t schid, EncodeInfo* out)) { encodeCb_ = fn; }
    std::wstring CodecInfo();
    bool         CodecIsVoice() const { return codecVoice_.load(); }

    // Klang fuer Opus optimieren (Hochpass, phasensichere Mono-Summe, Begrenzer) und lokale Opus-Vorschau fuer Mithoeren/Anhoeren.
    void         SetOptimizeForOpus(bool on);
    void         SetOpusPreview(bool on);
    // Beschreibung der Vorschau fuer die Oberflaeche ("Opus Voice · Mono · 38 kbit/s"), leer wenn die Vorschau aus oder nicht verfuegbar ist.
    std::wstring OpusInfo();
    bool         MusicModeEngaged() const { return engagedMask_.load() != 0; }
    // Anteil der Musik-Bloecke (gleitend, 0..1), die TS3 laut Plugin-API selbst als "wird gesendet" meldet. Niedrig = die Sprachaktivierung blockt die Musik.
    float        SendShare() const { return sendEma_.load(); }

    // Mithoeren: eingespielte Musik (nach Lautstaerke) um `delay` verzoegert in die eigene TS3-Wiedergabe mischen.
    void SetMonitor(bool on);
    void SetMonitorDelay(int ms);
    // Aus ts3plugin_onEditMixedPlaybackVoiceDataEvent (TS3-Wiedergabe-Thread).
    void MixMonitor(short* samples, int frames, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask);

    // Equalizer (Raster 10/31/61, je +-12 dB). Aenderungen wirken im naechsten Audio-Block.
    void SetEqEnabled(bool on);
    void SetEqBands(int bands);                    // rechnet die aktuelle Kurve auf das neue Raster um
    void SetEqGain(int band, float db);
    void SetEqGains(const std::vector<float>& db); // z.B. Preset; muss eqBands Werte haben

    // Eigene Equalizer-Presets (Name + Kurve), dazu die mitgelieferten per Index. Alle Aenderungen werden sofort gespeichert.
    PresetResult SaveEqPreset(const std::wstring& name);   // aktuelle Kurve unter diesem Namen sichern (gleicher Name = ueberschreiben) und auswaehlen
    PresetResult RenameEqPreset(const std::wstring& from, const std::wstring& to);
    bool         DeleteEqPreset(const std::wstring& name);
    void         ApplyBuiltinPreset(int index);
    void         ApplyUserPreset(const std::wstring& name);

    // Profile / Szenen: benannte Schnappschuesse (Quelle, Lautstaerken, Mithoeren, Equalizer, Qualitaet, Opus). Alles wird sofort gespeichert.
    PresetResult SaveProfile(const std::wstring& name);    // aktuelle Einstellungen unter diesem Namen sichern (gleicher Name = ueberschreiben) und auswaehlen
    PresetResult RenameProfile(const std::wstring& from, const std::wstring& to);
    bool         DeleteProfile(const std::wstring& name);
    bool         ApplyProfile(const std::wstring& name);   // false, wenn es das Profil nicht gibt
    std::vector<std::wstring> ProfileNames();
    // Naechstes Profil reihum (Hotkey); false, wenn es keine Profile gibt.
    bool         ApplyNextProfile();

    // Qualitaet (plugin-intern): 0 niedrige Latenz (40 ms), 1 ausgewogen (60 ms), 2 stabil (140 ms Puffer); Mono mischt die Musik als Mono ein.
    void SetQuality(int q);
    void SetMono(bool on);

    // "Einstellungen anhoeren": solange aktiv, hoert man die Musik (mit EQ, ohne Verzoegerung) in der eigenen TS3-Wiedergabe.
    // Zwei Wege: gedrueckt halten (SetPreview) oder per Schalter dauerhaft (SetPreviewLatch). IsPreview = einer von beiden.
    void SetPreview(bool hold);
    void SetPreviewLatch(bool on);
    bool IsPreview() const { return preview_.load(); }
    bool IsPreviewLatched() const { return previewLatch_.load(); }

    // Apps in der Quellen-Auswahl ausblenden bzw. alle wieder einblenden (wird gespeichert).
    void HideApp(const std::wstring& exeLower);
    void ShowAllApps();

    // ---- Datei-Player: Audio-Dateien (mp3, wav, flac, m4a ...) per Drag & Drop abspielen, zusaetzlich zur App-Aufnahme (beides wird gemischt). Die Dateien
    // laufen durch dieselbe Kette (Lautstaerke, EQ, Mono, Mithoeren, Musikmodus) und brauchen "Musik einspielen" nicht (der Schalter gilt nur fuer die App).
    // Alle Befehle sind aus jedem Thread aufrufbar.
    PlayerSnapshot GetPlayer();
    int  PlayerAdd(const std::vector<std::wstring>& paths); // Rueckgabe: Anzahl uebernommener Dateien
    void PlayerPlay(int index);                              // -1 = fortsetzen bzw. ersten Titel starten
    void PlayerTogglePause();
    void PlayerStop();
    void PlayerNext();
    void PlayerPrev();
    void PlayerSeek(double seconds);
    void PlayerRemove(int index);
    void PlayerClear();
    void PlayerSetRepeat(RepeatMode mode);
    // Benannte Wiedergabelisten (werden sofort gespeichert). Load ersetzt die aktuelle Liste und stoppt die Wiedergabe.
    PresetResult PlaylistSave(const std::wstring& name);
    PresetResult PlaylistRename(const std::wstring& from, const std::wstring& to);
    bool         PlaylistDelete(const std::wstring& name);
    bool         PlaylistLoad(const std::wstring& name);
    std::vector<std::wstring> PlaylistNames();
    // Name der gewaehlten App ("Spotify") fuer das Banner von "Musik einspielen"
    std::wstring SourceLabel();

    // ---- Soundboard (bis zu 400 Knoepfe mit kurzen Clips, die ueber die Musik gespielt werden). Slots sind 0..399 (Knopf = Slot + 1), das Raster
    // (Zeilen x Spalten) legt fest, wie viele davon sichtbar sind. Alle Befehle sind aus jedem Thread aufrufbar.
    struct SoundInfo {
        std::wstring      name, path;
        Soundboard::State state   = Soundboard::State::Empty;
        double            seconds = 0;
        uint32_t          color   = 0;   // 0xRRGGBB, 0 = Standardfarbe
        int               volPct  = 100; // Lautstaerke dieses Knopfes
        double            position = -1; // Sekunden seit Start, solange der Clip spielt; -1 = spielt nicht
    };
    void                   SoundSetSlot(int slot, const std::wstring& path); // Datei zuweisen (leer = Knopf leeren), Name = Dateiname ohne Endung; wird gespeichert
    void                   SoundConfigure(int slot, const std::wstring& path, const std::wstring& name, uint32_t color, int volPct); // Editor: alles auf einmal
    void                   SoundReload(int slot); // Datei erneut laden (nach einem Fehler)
    void                   SoundSwap(int a, int b); // zwei Knoepfe tauschen (Ziehen im Raster)
    void                   SoundRename(int slot, const std::wstring& name);
    void                   SoundPlay(int slot);   // spielt den Clip; unabhaengig von "Musik einspielen" (der Schalter bleibt, wie er ist)
    void                   SoundStop();           // alle Clips stoppen
    void                   SetSoundVolume(int pct); // Gesamtlautstaerke 0..200
    void                   SetSoundGrid(int rows, int cols);
    void                   SetSoundShowKeys(bool on);
    void                   SetSoundPinned(bool on);
    // Selbst hoeren: Soundboard-Clips (SetSoundMonitor) bzw. der Datei-Player (SetFileMonitor) kommen zusaetzlich auf dein Windows-Standard-Ausgabegeraet
    // (eigene Ausgabe des Plugins, nicht ueber TS3). Ist "Mithoeren" an, hoerst du ohnehin ueber TS3 (dann pausiert diese Ausgabe, damit nichts doppelt klingt).
    void                   SetSoundMonitor(bool on);
    void                   SetFileMonitor(bool on);
    SoundInfo              GetSound(int slot);
    std::vector<SoundInfo> GetSounds();

    // Soundboard-Datei importieren (ersetzt alle Knoepfe und das Raster; ein Speichervorgang) und lokales Vorhoeren (nie ins Mikrofon)
    bool                   SoundImport(const std::vector<SoundSlot>& slots, int rows, int cols);
    void                   SoundPreview(int slot);
    void                   SoundPreviewStop();

    // Ducking (Musik leiser, solange ein Clip laeuft bzw. du sprichst) und Lautstaerke angleichen (Sounds / Datei-Titel). Alles wird gespeichert.
    void SetDuckSound(bool on, int db);
    void SetDuckMic(bool on, int db, int thresholdDb);
    void SetNormalize(bool sounds, bool tracks, int targetDb);
    float DuckGain() const { return duckShown_.load(std::memory_order_relaxed); } // aktuelle Absenkung der Musik (1 = keine), fuer die Anzeige
    bool  MicSpeaking() const { return micSpeaking_.load(std::memory_order_relaxed); }

    // Fensterpositionen (which: 0 Hauptfenster, 1 Soundboard-Fenster, 2 Mini-Fenster; w/h nur beim Soundboard-Fenster), Update-Hinweis, Erststart, Automatik
    void SetWindowRect(int which, int x, int y, int w, int h);
    void SetUpdateInfo(const std::wstring& latest, long long checkedAt);
    void SetUpdateCheck(bool on);
    void SetFirstRunDone();
    void SetAutoProfile(bool on);
    void SetTrayIcon(bool on);
    // Profil an einen Server (eindeutige ID) und optional einen Kanal koppeln; leere uid loest die Kopplung. false, wenn es das Profil nicht gibt.
    bool SetProfileAuto(const std::wstring& profile, const std::wstring& uid, const std::wstring& serverName, const std::wstring& channel);
    // Server/Kanal gewechselt: wendet das passende Profil an (nur wenn es nicht schon das aktuelle ist). Liefert dessen Namen, sonst leer.
    std::wstring AutoProfileFor(const std::wstring& uid, const std::wstring& channel);
    bool ApplyProfileNumber(int n); // n-tes Profil (1-basiert) anwenden

    // Now-Playing: meldet einen Titelwechsel (Dateititel oder Fenstertitel der App, bereinigt), sobald er >= 3 s stabil ist und "Musik einspielen" an ist.
    // Leerer Titel = nichts mehr zu melden (Musik aus). Aufruf aus dem Watcher-Thread. Welche Ziele (Chat, Kanal, Metadaten) gelten, steht in den Einstellungen.
    void SetNowPlayingCallback(void (*fn)(const std::wstring& title)) { nowPlayingCb_ = fn; }
    // Oberflaechensprache: 0 = automatisch, 1 = Deutsch, 2 = English (wird gespeichert und sofort uebernommen)
    void SetLanguage(int setting);
    // Bereich "Erweitert" der Einstellungsseite auf-/zuklappen (wird gespeichert)
    void SetAdvancedOpen(bool open);

    void SetNowPlayingChat(bool on);
    void SetNowPlayingChannel(bool on);
    void SetNowPlayingMeta(bool on);

    // Zustandswechsel "Musik einspielen" (aus Menue oder Fenster), z.B. fuer eine Chat-Meldung. Vor Init setzen.
    void SetStateCallback(void (*fn)(bool enabled)) { stateCb_ = fn; }

    // Live-Daten fuer die Anzeige (Spektrum: neueste Mono-Samples; Tonspur: Min/Max je 20 ms, aelteste zuerst).
    void GetRecentSamples(float* out, int n) const { analysis_.CopyRecent(out, n); }
    void GetEnvelope(float* mn, float* mx, int n) const { analysis_.CopyEnvelope(mn, mx, n); }

    // Pegel der eingespielten Musik (0..1, Block-Spitzenwert nach Lautstaerke) fuer die Anzeige.
    void  GetLevels(float* left, float* right) const;
    // Kurze Diagnosezeile (Puffer, Aussetzer, Takt) fuer das Fenster.
    std::wstring Diagnostics();
    // Log-Ausgabe der Statistik (z.B. ins TS3-Log); darf nur vor Init gesetzt werden.
    void SetLogger(void (*fn)(const char*)) { logger_ = fn; }

private:
    Engine() : ring_(48000), reader_(ring_), monitorRing_(48000 * 3), monitorReader_(monitorRing_), fileRing_(48000), fileReader_(fileRing_) {}
    bool SourceOn() const { return AppOn() || fileActive_.load() || sounds_.Active(); } // irgendeine Quelle (App, Datei oder Soundboard) liefert Audio
    bool AppOn() const { return capturing_.load() && enabledA_.load(); }
    // Mikrofon-Callback bzw. Pump (beide unter renderMutex_): liest die Quellen eines Blocks nach gained_ (App + Datei nach Mono/EQ/Lautstaerke, dazu die
    // Clips) und schreibt fuer "Selbst hoeren" Datei + Clips (ohne App) auf die lokale Ausgabe. withApp = false: die App-Aufnahme nicht lesen.
    // *audible: eine Musikquelle ist vor der Lautstaerke hoerbar (fuer das Nachhalten des Sende-Bits).
    void RenderSources(int frames, bool withApp, bool* music, bool* voices, bool* audible);
    void PumpMain(); // Ersatztakt, solange TS3 den Mikrofon-Callback nicht aufruft (stumm, keine Sprechberechtigung, nicht verbunden)
    void UpdateStats();
    void WatcherMain();
    void SetStatus(const std::wstring& s);
    void SaveLocked();

    std::mutex   mutex_; // schuetzt settings_ und status_
    Settings     settings_;
    std::wstring status_ = L"Aus";
    std::wstring iniPath_;

    std::atomic<float> musicGain_{1.0f};
    std::atomic<float> micGain_{1.0f};
    std::atomic<bool>  micMuted_{false};
    Soundboard         sounds_;
    std::atomic<float> soundGain_{1.0f};
    // Ducking und Angleichen (Audio-Thread liest nur Atomics; Ducker/Detektor gehoeren der Render-Region unter renderMutex_)
    Ducker               duck_;
    SpeechDetector       speech_;
    std::atomic<bool>    duckSoundOn_{true}, duckMicOn_{false}, micSpeaking_{false};
    std::atomic<float>   duckSoundDb_{12.0f}, duckMicDb_{10.0f}, duckMicThr_{-45.0f}, duckShown_{1.0f};
    std::atomic<float>   fileTrackGain_{1.0f};  // Angleich-Verstaerkung des laufenden Titels (linear)
    std::vector<int16_t> previewScratch_;
    std::wstring         currentTrackPath_;     // unter playerMutex_
    void                 ApplyTrackGainLocked(); // unter playerMutex_: Verstaerkung des laufenden Titels setzen, ggf. Messung anstossen
    void                 MeasureTrackAsync(const std::wstring& path);
    std::mutex           loudMutex_;
    std::vector<std::wstring> loudQueue_;
    std::thread          loudThread_;
    std::atomic<bool>    loudRunning_{false}, loudQuit_{false};
    LocalOut           localOut_;               // Selbst hoeren (eigene Ausgabe)
    std::atomic<bool>  soundMonitor_{true}, fileMonitor_{true};
    std::vector<int16_t> soundScratch_, localMix_, fileScratch_; // unter renderMutex_: Clips eines Blocks, Mix fuer die lokale Ausgabe, Datei-Block
    std::mutex         renderMutex_;            // schuetzt, was Mix() und die Pump gemeinsam anfassen (Reader, EQ, Stimmen, Puffer, monitorRing_-Schreiben)
    std::atomic<ULONGLONG> lastMixTick_{0};     // letzter Aufruf des Mikrofon-Callbacks (GetTickCount64)
    std::thread        pump_;
    std::atomic<bool>  pumpQuit_{false};
    std::atomic<bool>  alwaysSend_{true};
    std::atomic<bool>  capturing_{false};

    FrameRing                      ring_;
    JitterReader                   reader_;   // nur vom Audio-Thread gelesen/zurueckgesetzt
    std::atomic<bool>              resetRequested_{false};

    // Mithoeren: Mix() (Mikrofon-Thread) schreibt die Musik fortlaufend, MixMonitor() (Wiedergabe-Thread) liest sie verzoegert.
    FrameRing                      monitorRing_;
    JitterReader                   monitorReader_;

    // Datei-Player: eigener Ring und Reader, damit die App-Aufnahme nicht gestoert wird
    FrameRing                      fileRing_;
    JitterReader                   fileReader_;
    std::atomic<bool>              fileActive_{false};         // Datei-Quelle ist gewaehlt (laeuft, pausiert oder leert gerade den Puffer)
    std::atomic<bool>              fileResetRequested_{false};
    std::atomic<bool>              filePaused_{false};         // Kopie von playerPaused_ fuer Audio-Thread/Pump
    std::atomic<bool>              enabledA_{false};           // "Musik einspielen" (Kopie fuer den Audio-Thread)
    bool                           appWasOn_  = false;         // unter renderMutex_: App-Quelle war im letzten Block aktiv
    bool                           fileWasOn_ = false;         // unter renderMutex_: Datei-Quelle war im letzten Block aktiv
    std::mutex                     playerMutex_;               // schuetzt playlist_, player_, playerPaused_, playerMsg_ ...
    playlist::Playlist             playlist_;
    std::unique_ptr<FilePlayer>    player_;
    bool                           playerPaused_ = false;
    std::wstring                   playerMsg_;
    size_t                         failStreak_   = 0;
    ULONGLONG                      drainUntil_   = 0;          // nach dem letzten Titel noch kurz den Puffer leerspielen
    std::thread                    prober_;                    // ermittelt die Laenge neu hinzugefuegter Dateien im Hintergrund
    std::atomic<bool>              proberRunning_{false};
    void StartTrackLocked(int index, double startSec, bool gapless);
    void StopPlayerLocked(bool drain);
    void HandlePlayerEvents(); // Watcher: Titelende -> naechster Titel, Puffer leeren
    void StartProber();
    void RememberPlaylistLocked(); // aktuelle Liste in settings_.lastPlaylist uebernehmen (playerMutex_ gehalten; nimmt mutex_)
    void SetPausedLocked(bool paused); // playerPaused_ + Decoder + filePaused_ gemeinsam setzen (playerMutex_ gehalten)
    std::atomic<bool>              monitor_{false};
    std::atomic<bool>              monitorResetRequested_{false};
    std::vector<int16_t>           monScratch_; // Wiedergabe-Thread
    std::unique_ptr<ProcessCapture> capture_; // nur vom Watcher-Thread angefasst
    std::wstring                   captureExe_;
    std::vector<int16_t>           scratch_;  // nur vom Audio-Thread angefasst
    std::vector<int16_t>           gained_;   // Musikblock nach EQ und Lautstaerke (Audio-Thread)

    void ApplyEqIfChanged(); // Audio-Thread
    eq::Equalizer         equalizer_, fileEq_;  // unter renderMutex_: App bzw. Datei (getrennt, damit "Selbst hoeren" die Datei ohne App bekommt; EQ ist linear)
    std::atomic<uint32_t> eqVersion_{1};
    uint32_t              eqApplied_ = 0;       // Audio-Thread
    std::vector<float>    eqGainsTmp_;          // Audio-Thread (vorreserviert)
    mutable Analysis      analysis_;
    std::atomic<bool>     mono_{false};
    std::atomic<bool>     preview_{false}; // wirksam = previewHold_ || previewLatch_
    std::atomic<bool>     previewHold_{false}, previewLatch_{false};
    void                  UpdatePreview();
    std::vector<int16_t>  monBlock_; // Audio-Thread: Musikblock so, wie er gesendet wird (bei Mono-Mikrofon als Mono)

    // Musikmodus
    unsigned            (*preCb_)(uint64_t, unsigned) = nullptr;
    std::wstring        (*codecCb_)(uint64_t)         = nullptr;
    std::string         (*diagCb_)(uint64_t)          = nullptr;
    std::atomic<uint64_t> engagedSchid_{0};     // Verbindung, auf der der Musikmodus gerade umgeschaltet ist (jeder "Test starten" bekommt eine neue)
    uint64_t              seenSchid_ = 0;       // Audio-Thread: zuletzt gesehene Verbindung und wie lange sie schon stabil ist
    int                   seenCount_ = 0;
    std::atomic<uint64_t> lastSchid_{0};
    std::atomic<bool>     musicMode_{true};     // Einstellung "Musikmodus" (fuer den Audio-Thread)
    std::atomic<unsigned> engagedMask_{0};      // aktuell umgeschaltete TS3-Einstellungen (nur vom Audio-Thread geaendert, beim Beenden vom TS3-Thread)
    int64_t               graceFrames_ = 0;     // Audio-Thread: Nachlauf, bevor die TS3-Einstellungen wiederhergestellt werden
    std::atomic<float>    sendEma_{1.0f};
    std::atomic<uint32_t> musicBlocks_{0}, openBlocks_{0}; // Diagnose: Musik-Bloecke und davon die, die TS3 selbst als "wird gesendet" meldete
    std::atomic<bool>     gateOn_{false};       // Mikro-Gate aktiv (Sprachaktivierung umgangen)
    std::atomic<float>    gateDb_{-50.0f};
    std::atomic<bool>     codecVoice_{false};
    ULONGLONG             codecTick_ = 0;       // Watcher
    std::wstring          codecInfo_;           // unter mutex_
    bool                (*encodeCb_)(uint64_t, EncodeInfo*) = nullptr;
    std::shared_ptr<OpusPreview> opus_;         // Zugriff nur ueber std::atomic_load/_store: Watcher tauscht aus, Wiedergabe-Thread benutzt
    EncodeInfo            opusCfg_;             // Watcher: Einstellung der aktuellen Vorschau-Instanz
    std::atomic<bool>     opusPreviewOn_{true}, optimize_{true};
    std::atomic<bool>     opusForce_{false};    // Vorschau neu anlegen/entfernen lassen (Schalter geaendert)
    std::wstring          opusInfo_;            // unter mutex_
    VoiceOptimizer        optimizer_;           // Audio-Thread
    bool                  optimizerActive_ = false; // Audio-Thread: Filterzustand zuruecksetzen, wenn die Musik pausiert hat
    void                  UpdateOpus();
    MicGate               gate_;                // Audio-Thread
    void                  ManageMusicMode(uint64_t schid, int frames); // Audio-Thread: TS3-Filter/Sprachaktivierung umgehen bzw. wiederherstellen
    void                  UpdateCodec();
    void                  UpdateNowPlaying(); // Watcher
    void                (*nowPlayingCb_)(const std::wstring&) = nullptr;
    std::wstring          npLast_, npCand_;   // Watcher: zuletzt gemeldeter bzw. gerade beobachteter Titel
    ULONGLONG             npTick_ = 0, npSince_ = 0;
    void                (*stateCb_)(bool) = nullptr;
    int64_t                        holdFrames_ = 0; // Audio-Thread: Rest-Haltezeit des Sende-Bits

    std::atomic<bool>     dirty_{false};   // Einstellungen geaendert, noch nicht gespeichert (Regler)
    std::atomic<float>    levelL_{0}, levelR_{0};
    std::atomic<uint32_t> callbacks_{0};
    std::atomic<int>      lastBlock_{0};
    void                (*logger_)(const char*) = nullptr;
    std::wstring          diag_;           // unter mutex_
    ULONGLONG             statsTick_ = 0;  // nur Watcher
    uint32_t              statsCallbacks_ = 0, statsUnderruns_ = 0, statsOverruns_ = 0;

    std::thread       watcher_;
    std::atomic<bool> quit_{false};
    HANDLE            wake_ = nullptr;
};
