#include "engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#include "app_discovery.h"
#include "mixer.h"
#include "nowplaying.h"

namespace {
constexpr int     kAudiblePeak    = 8;     // Quellpegel (vor Lautstaerke) ab dem die Musik als hoerbar gilt
constexpr int64_t kSendHoldFrames = 14400; // 300 ms Sende-Bit nach dem letzten hoerbaren Block

size_t QualityFrames(int q)
{
    static const size_t frames[] = {1920, 2880, 6720}; // 40 / 60 / 140 ms Puffer
    return frames[std::min(2, std::max(0, q))];
}
} // namespace

Engine& Engine::Get()
{
    static Engine e;
    return e;
}

void Engine::Init(const std::wstring& configDir)
{
    iniPath_ = configDir + L"\\voicetune.ini";
    const std::wstring oldIni = configDir + L"\\appaudiomixer.ini"; // Vorgaengerversion "App Audio Mixer": Einstellungen uebernehmen
    bool migrated = false;
    if (GetFileAttributesW(iniPath_.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(oldIni.c_str()) != INVALID_FILE_ATTRIBUTES) {
        settings_ = LoadSettings(oldIni, L"AppAudioMixer");
        migrated  = true;
    } else {
        settings_ = LoadSettings(iniPath_);
    }
    if (migrated) SaveSettings(iniPath_, settings_);
    musicGain_ = settings_.musicVolPct / 100.0f;
    micGain_   = settings_.micVolPct / 100.0f;
    alwaysSend_ = settings_.alwaysSend;
    monitor_    = settings_.monitor;
    mono_       = settings_.mono;
    gateDb_     = static_cast<float>(settings_.gateDb);
    musicMode_  = settings_.musicMode;
    i18n::SetSetting(settings_.language);
    soundGain_  = settings_.soundVolPct / 100.0f;
    for (int i = 0; i < kSoundSlots; ++i) {
        const SoundSlot& b = settings_.sounds[i];
        if (b.path.empty()) continue;
        sounds_.SetVolume(i, b.volPct);
        if (i < kSoundHotkeys) sounds_.Load(i, b.path); // Tastenkuerzel-Knoepfe sofort bereit, der Rest laedt beim ersten Klick
        else sounds_.Register(i, b.path);
    }
    optimize_   = settings_.optimizeForOpus;
    opusPreviewOn_ = settings_.opusPreview;
    settings_.eqGains.resize(settings_.eqBands, 0.0f);
    eqGainsTmp_.reserve(eq::kMaxBands);
    reader_.SetTarget(QualityFrames(settings_.quality));
    reader_.Reset();
    fileReader_.SetTarget(QualityFrames(settings_.quality));
    fileReader_.LimitPullAhead(9600); // 200 ms: der Dekoder soll nicht die ganze Datei in den Reader kippen
    fileReader_.Reset();
    enabledA_ = settings_.enabled;
    monitorReader_.SetTarget(static_cast<size_t>(settings_.monitorDelayMs) * 48);
    monitorReader_.Reset();
    status_     = settings_.enabled ? L"Warte auf App..." : L"Aus";

    soundMonitor_ = settings_.soundMonitor;
    fileMonitor_  = settings_.fileMonitor;
    {
        std::lock_guard<std::mutex> lk(playerMutex_); // zuletzt benutzte Wiedergabeliste wiederherstellen (nicht abspielen)
        playlist_.Clear();
        for (const auto& p : settings_.lastPlaylist) playlist_.Add(p);
        playlist_.repeat = static_cast<RepeatMode>(std::min(2, std::max(0, settings_.lastRepeat)));
    }
    if (!settings_.lastPlaylist.empty()) StartProber();
    localOut_.SetLogger(logger_);
    localOut_.Start();
    sounds_.Start(); // nach einem Shutdown wieder bereit (Plugin deaktiviert und wieder aktiviert)
    quit_     = false;
    pumpQuit_ = false;
    wake_     = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    watcher_  = std::thread(&Engine::WatcherMain, this);
    pump_     = std::thread(&Engine::PumpMain, this);
}

void Engine::Shutdown()
{
    quit_ = true;
    if (wake_) SetEvent(wake_);
    if (watcher_.joinable()) watcher_.join();
    pumpQuit_ = true;
    if (pump_.joinable()) pump_.join(); // Ersatztakt (benutzt Soundboard, Datei-Ring und lokale Ausgabe)
    sounds_.Stop(); // Lade-Thread des Soundboards (muss vor dem Entladen der DLL weg sein)
    localOut_.Stop(); // ebenso der Thread der lokalen Ausgabe
    std::atomic_store(&opus_, std::shared_ptr<OpusPreview>());
    if (engagedMask_.exchange(0) && preCb_) preCb_(lastSchid_.load(), 0); // TS3-Einstellungen des Nutzers wiederherstellen
    gateOn_ = false;
    if (dirty_.exchange(false)) {
        std::lock_guard<std::mutex> lk(mutex_);
        SaveLocked();
    }
    {
        std::lock_guard<std::mutex> lk(playerMutex_);
        player_.reset(); // beendet den Decoder-Thread, bevor das Event geschlossen wird
        fileActive_ = false;
    }
    if (prober_.joinable()) prober_.join();
    if (wake_) CloseHandle(wake_);
    wake_ = nullptr;
    capture_.reset();
    capturing_ = false;
    ring_.Clear();
    levelL_ = levelR_ = 0;
}

void Engine::SaveLocked() { SaveSettings(iniPath_, settings_); }

void Engine::SetEnabled(bool on)
{
    bool changed;
    enabledA_ = on;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        changed           = settings_.enabled != on;
        settings_.enabled = on;
        SaveLocked();
    }
    if (wake_) SetEvent(wake_);
    if (changed && stateCb_) stateCb_(on); // der Schalter gilt nur fuer die App; Datei-Player und Soundboard laufen unabhaengig davon
}

void Engine::SetEqEnabled(bool on)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.eqEnabled = on;
    dirty_              = true;
    ++eqVersion_;
}

void Engine::SetEqBands(int bands)
{
    if (!eq::ValidBandCount(bands)) return;
    std::lock_guard<std::mutex> lk(mutex_);
    if (bands == settings_.eqBands) return;
    settings_.eqGains.resize(settings_.eqBands, 0.0f);
    settings_.eqGains = eq::ResampleCurve(settings_.eqBands, settings_.eqGains.data(), bands);
    settings_.eqBands = bands;
    dirty_            = true;
    ++eqVersion_;
}

void Engine::SetEqGain(int band, float db)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (band < 0 || band >= settings_.eqBands) return;
    settings_.eqGains.resize(settings_.eqBands, 0.0f);
    settings_.eqGains[band] = std::clamp(std::round(db * 2.0f) / 2.0f, -eq::kMaxDb, eq::kMaxDb);
    dirty_                  = true;
    ++eqVersion_;
}

void Engine::SetEqGains(const std::vector<float>& db)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (static_cast<int>(db.size()) != settings_.eqBands) return;
    settings_.eqGains = db;
    dirty_            = true;
    ++eqVersion_;
}

PresetResult Engine::SaveEqPreset(const std::wstring& name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.eqGains.resize(settings_.eqBands, 0.0f);
    const PresetResult r = settings_.userPresets.Save(name, settings_.eqBands, settings_.eqGains);
    if (r == PresetResult::Ok || r == PresetResult::Updated) {
        settings_.eqPresetName = presets::Sanitize(name);
        SaveLocked();
    }
    return r;
}

PresetResult Engine::RenameEqPreset(const std::wstring& from, const std::wstring& to)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const PresetResult r = settings_.userPresets.Rename(from, to);
    if (r == PresetResult::Ok) {
        if (presets::EqualNoCase(settings_.eqPresetName, from)) settings_.eqPresetName = presets::Sanitize(to);
        SaveLocked();
    }
    return r;
}

bool Engine::DeleteEqPreset(const std::wstring& name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const bool ok = settings_.userPresets.Delete(name);
    if (ok) {
        if (presets::EqualNoCase(settings_.eqPresetName, name)) settings_.eqPresetName.clear();
        SaveLocked();
    }
    return ok;
}

void Engine::ApplyBuiltinPreset(int index)
{
    if (index < 0 || index >= eq::kPresetCount) return;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.eqGains      = eq::PresetGains(index, settings_.eqBands);
    settings_.eqPresetName = eq::PresetName(index);
    ++eqVersion_;
    SaveLocked();
}

void Engine::ApplyUserPreset(const std::wstring& name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const EqPreset* p = settings_.userPresets.Find(name);
    if (!p) return;
    settings_.eqGains      = presets::CurveFor(*p, settings_.eqBands);
    settings_.eqPresetName = p->name;
    ++eqVersion_;
    SaveLocked();
}

PresetResult Engine::SaveProfile(const std::wstring& name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const PresetResult r = settings_.profiles.Save(name, ProfileFromSettings(settings_));
    if (r == PresetResult::Ok || r == PresetResult::Updated) {
        settings_.profileName = presets::Sanitize(name);
        SaveLocked();
    }
    return r;
}

PresetResult Engine::RenameProfile(const std::wstring& from, const std::wstring& to)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const PresetResult r = settings_.profiles.Rename(from, to);
    if (r == PresetResult::Ok) {
        if (presets::EqualNoCase(settings_.profileName, from)) settings_.profileName = presets::Sanitize(to);
        SaveLocked();
    }
    return r;
}

bool Engine::DeleteProfile(const std::wstring& name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const bool ok = settings_.profiles.Delete(name);
    if (ok) {
        if (presets::EqualNoCase(settings_.profileName, name)) settings_.profileName.clear();
        SaveLocked();
    }
    return ok;
}

// Setzt alle Werte des Profils ueber die normalen Setter (wirken sofort im Audio-Thread); Stumm, Ein/Aus und Musikmodus bleiben unberuehrt.
bool Engine::ApplyProfile(const std::wstring& name)
{
    Profile p;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const Profile* f = settings_.profiles.Find(name);
        if (!f) return false;
        p = *f;
    }
    SetTarget(p.targetExe);
    SetMusicVolume(p.musicVolPct);
    SetMicVolume(p.micVolPct);
    SetMonitorDelay(p.monitorDelayMs);
    SetMonitor(p.monitor);
    SetEqBands(p.eqBands);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        p.eqGains.resize(settings_.eqBands, 0.0f);
        settings_.eqGains      = p.eqGains;
        settings_.eqPresetName = p.eqPresetName;
        settings_.eqEnabled    = p.eqEnabled;
        ++eqVersion_;
        dirty_ = true;
    }
    SetQuality(p.quality);
    SetMono(p.mono);
    SetOptimizeForOpus(p.optimizeForOpus);
    SetOpusPreview(p.opusPreview);
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.profileName = p.name;
    SaveLocked();
    return true;
}

std::vector<std::wstring> Engine::ProfileNames()
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::wstring>   names;
    for (const auto& p : settings_.profiles.items) names.push_back(p.name);
    return names;
}

bool Engine::ApplyNextProfile()
{
    std::wstring next;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        next = settings_.profiles.NextAfter(settings_.profileName);
    }
    return !next.empty() && ApplyProfile(next);
}

void Engine::SetQuality(int q)
{
    q = std::min(2, std::max(0, q));
    reader_.SetTarget(QualityFrames(q));
    fileReader_.SetTarget(QualityFrames(q));
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.quality = q;
    dirty_            = true;
}

void Engine::SetMono(bool on)
{
    mono_ = on;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.mono = on;
    dirty_         = true;
}

void Engine::SetPreview(bool hold)
{
    previewHold_ = hold;
    UpdatePreview();
}

void Engine::SetPreviewLatch(bool on)
{
    previewLatch_ = on;
    UpdatePreview();
}

void Engine::HideApp(const std::wstring& exeLower)
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto& h = settings_.hiddenApps;
    if (std::find(h.begin(), h.end(), exeLower) == h.end()) h.push_back(exeLower);
    SaveLocked();
}

void Engine::ShowAllApps()
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.hiddenApps.clear();
    SaveLocked();
}

void Engine::UpdatePreview()
{
    const bool on = previewHold_.load() || previewLatch_.load();
    if (preview_.exchange(on) == on) return;
    Settings s = GetSettings();
    monitorReader_.SetTarget(on ? JitterReader::kMinTarget : static_cast<size_t>(s.monitorDelayMs) * 48);
    monitor_ = on || s.monitor;
    monitorRing_.Clear();
    monitorResetRequested_ = true; // neues Ziel sofort uebernehmen
}

// Audio-Thread bzw. Pump (unter renderMutex_): neue EQ-Werte uebernehmen (try_lock: blockiert nie auf die Oberflaeche).
void Engine::ApplyEqIfChanged()
{
    const uint32_t v = eqVersion_.load(std::memory_order_relaxed);
    if (v == eqApplied_ || !mutex_.try_lock()) return;
    const int  bands = settings_.eqBands;
    const bool en    = settings_.eqEnabled;
    eqGainsTmp_.assign(settings_.eqGains.begin(), settings_.eqGains.end());
    mutex_.unlock();
    eqGainsTmp_.resize(bands, 0.0f);
    equalizer_.Configure(bands, eqGainsTmp_.data(), en);
    fileEq_.Configure(bands, eqGainsTmp_.data(), en);
    eqApplied_ = v;
}

void Engine::SetTarget(const std::wstring& exeLower)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (settings_.targetExe == exeLower) return;
        settings_.targetExe = exeLower;
        SaveLocked();
    }
    if (wake_) SetEvent(wake_);
}

void Engine::SetMusicVolume(int pct)
{
    pct = std::min(200, std::max(0, pct));
    musicGain_ = pct / 100.0f;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.musicVolPct = pct;
    dirty_ = true; // beim Ziehen des Reglers nicht bei jeder Mausbewegung speichern
}

void Engine::SetMicVolume(int pct)
{
    pct = std::min(200, std::max(0, pct));
    micGain_ = pct / 100.0f;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.micVolPct = pct;
    dirty_ = true;
}

void Engine::SetMonitor(bool on)
{
    if (on) {
        monitorRing_.Clear();
        monitorResetRequested_ = true;
    }
    monitor_ = on || preview_.load();
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.monitor = on;
    SaveLocked();
}

void Engine::SetMonitorDelay(int ms)
{
    ms = std::min(2000, std::max(0, ms));
    if (!preview_.load()) monitorReader_.SetTarget(static_cast<size_t>(ms) * 48); // waehrend "Anhoeren" gilt die kurze Vorschau-Verzoegerung
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.monitorDelayMs = ms;
    dirty_ = true;
}

void Engine::MixMonitor(short* samples, int frames, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    constexpr unsigned kLeft = 0x1, kRight = 0x2, kCenter = 0x4; // SPEAKER_FRONT_LEFT/RIGHT/CENTER
    if (!monitor_.load(std::memory_order_relaxed) || !SourceOn() || frames <= 0 || channels <= 0) return;
    if (monitorResetRequested_.exchange(false)) monitorReader_.Reset();
    if (monScratch_.size() < static_cast<size_t>(frames) * 2) monScratch_.resize(static_cast<size_t>(frames) * 2);
    if (!monitorReader_.Read(monScratch_.data(), frames)) return; // Vorpuffern oder Quelle still
    if (opusPreviewOn_.load(std::memory_order_relaxed)) // so klingen auch die anderen: durch Opus kodiert und wieder dekodiert (Voice = Mono)
        if (const auto opus = std::atomic_load(&opus_)) opus->Process(monScratch_.data(), frames);

    for (int c = 0; c < channels; ++c) {
        const unsigned spk = channelSpeakerArray ? channelSpeakerArray[c] : (c == 0 ? kLeft : c == 1 ? kRight : 0);
        if (!(spk & (kLeft | kRight | kCenter))) continue; // Surround-/Subwoofer-Kanaele auslassen
        const bool filled = !channelFillMask || (*channelFillMask & (1u << c));
        for (int i = 0; i < frames; ++i) {
            const float l = monScratch_[2 * i], r = monScratch_[2 * i + 1];
            const float add = (spk & kLeft) ? l : (spk & kRight) ? r : 0.5f * (l + r);
            samples[i * channels + c] = mixer::SoftClip((filled ? samples[i * channels + c] : 0) + add);
        }
        if (channelFillMask) *channelFillMask |= (1u << c);
    }
}

void Engine::SetAlwaysSend(bool on)
{
    alwaysSend_ = on;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.alwaysSend = on;
    SaveLocked();
}

Settings Engine::GetSettings()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return settings_;
}

std::wstring Engine::Status()
{
    std::wstring app;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        app = status_;
    }
    // Datei spielt (nicht pausiert): sie hat Vorrang in der Anzeige; laeuft zugleich die App, steht das dabei ("+ App")
    if (fileActive_.load() && !filePaused_.load()) {
        std::lock_guard<std::mutex> lk(playerMutex_);
        if (playlist_.current >= 0 && playlist_.current < static_cast<int>(playlist_.items.size()))
            return std::wstring(AppOn() ? L"Aktiv: Datei + App – " : L"Aktiv: Datei – ") + playlist_.items[playlist_.current].title;
    }
    return app;
}

void Engine::SetStatus(const std::wstring& s)
{
    std::lock_guard<std::mutex> lk(mutex_);
    status_ = s;
}

void Engine::WatcherMain()
{
    auto dropCapture = [this] {
        capture_.reset();
        capturing_ = false;
        ring_.Clear();
        resetRequested_ = true; // der Audio-Thread setzt den Reader zurueck
        monitorRing_.Clear();
        monitorResetRequested_ = true;
        levelL_ = levelR_ = 0;
    };
    while (!quit_) {
        Settings s = GetSettings();

        if (capture_ && !capture_->IsRunning()) dropCapture(); // App beendet oder Geraet weg

        if (!s.enabled || s.targetExe.empty()) {
            if (capture_) dropCapture();
            SetStatus(!s.enabled ? L"Aus" : L"Keine App gewählt");
        } else if (capture_ && captureExe_ != s.targetExe) { // Ziel gewechselt -> neu verbinden
            dropCapture();
        }

        if (!capture_ && s.enabled && !s.targetExe.empty()) {
            auto  apps = DiscoverApps();
            auto* app  = FindByExe(apps, s.targetExe);
            if (!app) {
                SetStatus(L"Warte auf " + s.targetExe + L" (App starten und Ton abspielen)");
            } else {
                auto         cap = std::make_unique<ProcessCapture>(ring_);
                std::wstring err;
                if (cap->Start(app->pid, &err)) {
                    capture_    = std::move(cap);
                    captureExe_ = s.targetExe;
                    capturing_  = true;
                    if (capture_->SystemWide()) // Windows vor 10 (2004): keine App-Aufnahme moeglich
                        SetStatus(L"Aktiv: gesamter PC-Ton – dein Windows kann keine einzelne App aufnehmen (ab Windows 10 2004). TeamSpeak-Stimmen werden mitgesendet!");
                    else
                        SetStatus(L"Aktiv: " + s.targetExe + L" (PID " + std::to_wstring(app->pid) + L")");
                } else {
                    SetStatus(s.targetExe + L": " + err);
                }
            }
        }

        UpdateCodec();
        UpdateNowPlaying();
        HandlePlayerEvents();
        UpdateStats();
        if (dirty_.exchange(false)) {
            std::lock_guard<std::mutex> lk(mutex_);
            SaveLocked();
        }
        WaitForSingleObject(wake_, fileActive_.load() ? 150 : 1000); // mit Datei-Player enger takten (Titelwechsel, Puffer leeren)
    }
}

// Kanal-Codec fuer den Hinweis im Fenster und die Opus-Vorschau (alle 2 s, oder sofort nach Umschalten der Vorschau).
void Engine::UpdateCodec()
{
    const ULONGLONG now   = GetTickCount64();
    const bool      force = opusForce_.exchange(false);
    if (!force && now - codecTick_ <= 2000) return;
    codecTick_        = now;
    std::wstring info = codecCb_ ? codecCb_(lastSchid_.load()) : std::wstring();
    codecVoice_       = info.find(L"Voice") != std::wstring::npos;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        codecInfo_ = info;
    }
    UpdateOpus();
}

// Watcher: haelt die Opus-Vorschau passend zur Kodierung des Kanals (Kanalzahl, Bitrate). Der Audio-Thread bekommt eine fertig konfigurierte Instanz.
void Engine::UpdateOpus()
{
    std::wstring text;
    if (!OpusPreview::Available() || !opusPreviewOn_.load()) {
        if (std::atomic_load(&opus_)) std::atomic_store(&opus_, std::shared_ptr<OpusPreview>());
    } else {
        EncodeInfo e; // unbekannt (keine Verbindung, anderer Codec): Opus Voice mit mittlerer Bitrate annehmen
        if (!encodeCb_ || !encodeCb_(lastSchid_.load(), &e)) e = EncodeInfo{};
        const auto cur = std::atomic_load(&opus_);
        // Kleine Schwankungen der gemeldeten Bitrate nicht nachziehen (jede neue Instanz bedeutet einen kurzen Einschwingvorgang)
        const bool same = cur && cur->Channels() == e.channels && std::abs(cur->Bitrate() - e.bitrateBps) * 10 <= e.bitrateBps;
        if (!same) std::atomic_store(&opus_, OpusPreview::Create(e.channels, e.bitrateBps));
        if (const auto now = std::atomic_load(&opus_)) {
            wchar_t kbit[32];
            std::swprintf(kbit, 32, L"%d kbit/s", (now->Bitrate() + 500) / 1000);
            text = std::wstring(now->Channels() == 2 ? L"Opus Music · Stereo · " : L"Opus Voice · Mono · ") + kbit + (e.exact ? L"" : L" (geschätzt)");
        }
    }
    std::lock_guard<std::mutex> lk(mutex_);
    opusInfo_ = text;
}

void Engine::SetLanguage(int setting)
{
    setting = std::min(2, std::max(0, setting));
    i18n::SetSetting(setting);
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.language = setting;
    SaveLocked();
}

void Engine::SetAdvancedOpen(bool open)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.advancedOpen = open;
    dirty_                 = true;
}

void Engine::SetNowPlayingChat(bool on)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.nowPlayingChat = on;
    dirty_                   = true;
}

void Engine::SetNowPlayingChannel(bool on)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.nowPlayingChannel = on;
    dirty_                      = true;
}

void Engine::SetNowPlayingMeta(bool on)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.nowPlayingMeta = on;
    dirty_                   = true;
}

// Watcher: erkennt Titelwechsel der laufenden Quelle (Datei oder App) und meldet sie dem Plugin. Gleicher Titel wird nicht doppelt gemeldet;
// nach "Musik aus" (oder aus allen Zielen abgeschaltet) beginnt es von vorn.
void Engine::UpdateNowPlaying()
{
    if (!nowPlayingCb_) return;
    const Settings s      = GetSettings();
    // Datei-Player laeuft unabhaengig von "Musik einspielen": auch dann melden
    const bool     fileOn = fileActive_.load() && !filePaused_.load();
    const bool     wanted = (s.enabled || fileOn) && (s.nowPlayingChat || s.nowPlayingChannel || s.nowPlayingMeta);
    if (!wanted) {
        npCand_.clear();
        if (!npLast_.empty()) {
            npLast_.clear();
            nowPlayingCb_(L""); // z.B. Client-Metadaten wieder zuruecksetzen
        }
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - npTick_ < 1000) return;
    npTick_ = now;

    std::wstring title;
    if (fileOn) {
        std::lock_guard<std::mutex> lk(playerMutex_);
        if (player_ && !playerPaused_ && playlist_.current >= 0 && playlist_.current < static_cast<int>(playlist_.items.size())) title = playlist_.items[playlist_.current].title;
    } else if (s.enabled && capturing_.load() && !s.targetExe.empty()) {
        const auto  apps = DiscoverApps();
        const auto* app  = FindByExe(apps, s.targetExe);
        if (app && app->playing) title = nowplaying::CleanTrackTitle(app->title, s.targetExe);
    }
    if (title != npCand_) {
        npCand_  = title;
        npSince_ = now;
    }
    if (!title.empty() && title != npLast_ && now - npSince_ >= 3000) {
        npLast_ = title;
        nowPlayingCb_(title);
    }
}

std::wstring Engine::OpusInfo()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return opusInfo_;
}

void Engine::SetOptimizeForOpus(bool on)
{
    optimize_ = on;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.optimizeForOpus = on;
    dirty_                    = true;
}

void Engine::SetOpusPreview(bool on)
{
    opusPreviewOn_ = on;
    opusForce_     = true;
    if (wake_) SetEvent(wake_);
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.opusPreview = on;
    dirty_                = true;
}

// Musikmodus, im Mikrofon-Callback (TS3-Thread) ausgefuehrt, so wie es auch das Soundboard-Plugin macht: TS3 ruft diesen Callback NACH seiner
// Vorverarbeitung auf. Die Sprachaktivierung startet eine Uebertragung nie von selbst, nur weil das Plugin Musik einmischt; sie muss ausgeschaltet
// und der Eingang aktiv sein (= Dauersenden), solange die Musik laeuft. Danach (ca. 1 s Nachlauf) stellen wir die Werte des Nutzers wieder her.
void Engine::ManageMusicMode(uint64_t schid, int frames)
{
    constexpr int64_t kGraceFrames = 33600; // 0,7 s (zusammen mit kSendHoldFrames ca. 1 s nach dem letzten Ton)
    const bool        active = SourceOn() && holdFrames_ > 0;
    graceFrames_             = active ? kGraceFrames : std::max<int64_t>(0, graceFrames_ - frames);
    unsigned mask = 0;
    if (musicMode_.load() && SourceOn() && (active || graceFrames_ > 0)) mask = 1u | (alwaysSend_.load() ? 2u : 0u);
    // Jeder "Test starten" laeuft auf einer neuen Verbindung (eigene Vorverarbeitung, eigener Eingangsstatus): der Musikmodus muss ihr folgen. Erst nach ein paar
    // stabilen Bloecken wechseln, damit zwei abwechselnd rufende Verbindungen nicht in jedem Block hin- und hergeschaltet werden.
    if (schid != seenSchid_) {
        seenSchid_ = schid;
        seenCount_ = 0;
    } else if (seenCount_ < 1000) {
        ++seenCount_;
    }
    const bool sameHandle = engagedSchid_.load() == schid;
    if (!preCb_ || seenCount_ < 5) return;
    if (mask == engagedMask_.load() && (mask == 0 || sameHandle)) return;
    const unsigned applied = preCb_(schid, mask); // schaltet bei Verbindungswechsel zuerst die alte zurueck
    gateOn_                = (applied & 2u) != 0; // Gate nur, wenn wir die Sprachaktivierung wirklich umgangen haben
    engagedMask_           = mask;                // auch bei Fehlschlag merken, damit nicht in jedem Block neu versucht wird
    engagedSchid_          = mask ? schid : 0;
}

std::wstring Engine::CodecInfo()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return codecInfo_;
}

void Engine::SetMusicMode(bool on)
{
    musicMode_ = on;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.musicMode = on;
    dirty_              = true;
}

void Engine::SetGateDb(int db)
{
    db        = std::min(-25, std::max(-70, db));
    gateDb_   = static_cast<float>(db);
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.gateDb = db;
    dirty_           = true;
}

// Alle 5 s: Diagnosezeile fuer das Fenster, Statistik ins Log.
void Engine::UpdateStats()
{
    const ULONGLONG now = GetTickCount64();
    if (statsTick_ == 0) statsTick_ = now;
    if (now - statsTick_ < 5000) return;
    const double secs = (now - statsTick_) / 1000.0;
    statsTick_        = now;

    const uint32_t cb        = callbacks_.load();
    JitterReader&  rd        = AppOn() || !fileActive_.load() ? reader_ : fileReader_; // Diagnose: App vor Datei
    const uint32_t underruns = rd.underruns.load(), overruns = rd.overruns.load();
    const size_t   minFill   = rd.minFill.exchange(SIZE_MAX), maxFill = rd.maxFill.exchange(0);
    const double   perSec    = (cb - statsCallbacks_) / secs;
    char           line[256], shortLine[128];
    if (SourceOn() && minFill != SIZE_MAX) {
        const float drift = (rd.ratioNow.load() - 1.0f) * 100.0f;
        std::snprintf(line, sizeof(line), "Puffer %zu-%zu ms (Ziel %zu), Aussetzer %u (+%u), Ueberlaeufe %u, Takt-Korrektur %+.2f %%, %.0f Bloecke/s a %d Frames",
                      minFill / 48, maxFill / 48, rd.Target() / 48, underruns, underruns - statsUnderruns_, overruns, drift, perSec, lastBlock_.load());
        const uint32_t mb = musicBlocks_.exchange(0), ob = openBlocks_.exchange(0);
        char           send[48] = "";
        if (mb) std::snprintf(send, sizeof(send), "  |  TS3 sendet %u %%", ob * 100 / mb);
        std::snprintf(shortLine, sizeof(shortLine), "Puffer %zu-%zu ms  |  Aussetzer %u  |  Takt %+.2f %%%s", minFill / 48, maxFill / 48, underruns, drift, send);
        std::snprintf(line + std::strlen(line), sizeof(line) - std::strlen(line), ", TS3-Sendefreigabe %u %% (%u Musik-Bloecke), Musikmodus %s, schid %llu", mb ? ob * 100 / mb : 0, mb,
                      engagedMask_.load() ? "AN" : "aus", static_cast<unsigned long long>(lastSchid_.load()));
        if (logger_) {
            logger_(line);
            if (mb > 20 && ob * 100 / mb < 60 && engagedMask_.load() && diagCb_) { // Musikmodus an, aber TS3 sendet nicht: Zustand der Verbindung ins Log
                const std::string diag = "Diagnose (TS3 sendet die Musik nicht): " + diagCb_(lastSchid_.load());
                logger_(diag.c_str());
            }
        }
    } else {
        std::snprintf(shortLine, sizeof(shortLine), "Keine Aufnahme aktiv");
    }
    statsCallbacks_ = cb;
    statsUnderruns_ = underruns;
    statsOverruns_  = overruns;

    wchar_t wide[256];
    MultiByteToWideChar(CP_ACP, 0, shortLine, -1, wide, 256);
    std::lock_guard<std::mutex> lk(mutex_);
    diag_ = wide;
}

std::wstring Engine::Diagnostics()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return diag_;
}

void Engine::GetLevels(float* left, float* right) const
{
    *left  = levelL_.load();
    *right = levelR_.load();
}

// Unter renderMutex_ (Mikrofon-Callback oder Pump). Liest App (withApp) und Datei, bereitet sie auf (Mono -> EQ -> Lautstaerke), mischt die Clips dazu und
// legt das Ergebnis nach gained_ (genau das, was ins Mikrofon und ins Mithoeren geht). "Selbst hoeren": Datei-Musik und Clips (ohne App-Ton, den hoerst du
// ohnehin selbst) zusaetzlich auf die eigene Ausgabe; bei "Mithoeren" hoerst du ueber TS3, dann bleibt sie still.
void Engine::RenderSources(int frames, bool withApp, bool* music, bool* voices, bool* audible)
{
    const size_t need = static_cast<size_t>(frames) * 2;
    if (scratch_.size() < need) scratch_.resize(need);
    if (fileScratch_.size() < need) fileScratch_.resize(need);
    if (gained_.size() < need) gained_.resize(need);
    if (soundScratch_.size() < need) soundScratch_.resize(need);
    if (localMix_.size() < need) localMix_.resize(need);

    const bool appOn  = withApp && AppOn();
    const bool fileOn = fileActive_.load(std::memory_order_relaxed);
    if (resetRequested_.exchange(false)) { // Watcher hat die Aufnahme neu verbunden
        reader_.Reset();
        analysis_.Clear();
    }
    if (appOn != appWasOn_) { // App kommt (wieder) dazu: alte Daten verwerfen, sauber neu puffern
        appWasOn_ = appOn;
        if (appOn) {
            ring_.Clear();
            reader_.Reset();
        }
    }
    if (fileResetRequested_.exchange(false)) fileReader_.Reset();
    if (fileOn != fileWasOn_) {
        fileWasOn_ = fileOn;
        if (fileOn) fileReader_.Reset();
    }

    const bool appMusic  = appOn && reader_.Read(scratch_.data(), frames);
    const bool fileMusic = fileOn && fileReader_.Read(fileScratch_.data(), frames);
    int        pl = 0, pr = 0, peak = 0;
    if (appMusic) {
        mixer::PeakStereo(scratch_.data(), frames, &pl, &pr);
        peak = std::max(pl, pr);
    }
    if (fileMusic) {
        mixer::PeakStereo(fileScratch_.data(), frames, &pl, &pr);
        peak = std::max(peak, std::max(pl, pr));
    }
    *audible = peak > kAudiblePeak;

    const bool  mono      = mono_.load(std::memory_order_relaxed);
    const float musicGain = musicGain_.load();
    auto        prepare   = [&](std::vector<int16_t>& buf, eq::Equalizer& eqz) {
        if (mono)
            for (int i = 0; i < frames; ++i) buf[2 * i] = buf[2 * i + 1] = static_cast<int16_t>((buf[2 * i] + buf[2 * i + 1]) / 2);
        eqz.Process(buf.data(), frames);
    };
    if (appMusic) prepare(scratch_, equalizer_);
    if (fileMusic) prepare(fileScratch_, fileEq_);
    for (size_t i = 0; i < need; ++i) {
        const float sum = (appMusic ? static_cast<float>(scratch_[i]) : 0.0f) + (fileMusic ? static_cast<float>(fileScratch_[i]) : 0.0f);
        gained_[i]      = mixer::SoftClip(sum * musicGain);
    }
    *music = appMusic || fileMusic;

    const bool selfHear = !monitor_.load(std::memory_order_relaxed);
    bool       localAny = false;
    if (selfHear && fileMusic && fileMonitor_.load(std::memory_order_relaxed)) {
        for (size_t i = 0; i < need; ++i) localMix_[i] = mixer::SoftClip(fileScratch_[i] * musicGain);
        localAny = true;
    }
    // Soundboard: laufende Clips kommen ueber die Musik (nach EQ und Musik-Lautstaerke, vor Optimierer/Begrenzer, mit eigener Lautstaerke). Erst allein rendern,
    // dann addieren: so lassen sie sich auch ohne Musik auf die lokale Ausgabe legen.
    bool mixedVoices = false;
    if (sounds_.Active()) {
        std::fill(soundScratch_.begin(), soundScratch_.begin() + static_cast<std::ptrdiff_t>(need), static_cast<int16_t>(0));
        mixedVoices = sounds_.Mix(soundScratch_.data(), frames, soundGain_.load(std::memory_order_relaxed));
        if (mixedVoices) {
            for (size_t i = 0; i < need; ++i) gained_[i] = static_cast<int16_t>(std::clamp(static_cast<int>(gained_[i]) + soundScratch_[i], -32768, 32767));
            if (selfHear && soundMonitor_.load(std::memory_order_relaxed)) {
                if (!localAny) std::fill(localMix_.begin(), localMix_.begin() + static_cast<std::ptrdiff_t>(need), static_cast<int16_t>(0));
                for (size_t i = 0; i < need; ++i) localMix_[i] = static_cast<int16_t>(std::clamp(static_cast<int>(localMix_[i]) + soundScratch_[i], -32768, 32767));
                localAny = true;
            }
        }
    }
    *voices = mixedVoices;
    if (localAny) localOut_.Write(localMix_.data(), static_cast<size_t>(frames));
}

// Ersatztakt: TS3 ruft den Mikrofon-Callback nicht auf, solange du stumm bist, keine Sprechberechtigung hast oder nicht verbunden bist. Ohne diesen Takt
// wuerden Clips und Dateien stehen bleiben und spaeter alle auf einmal starten (wie eine Warteschlange). Hier laufen sie nach der Uhr weiter und sind lokal
// zu hoeren ("Selbst hoeren" bzw. Mithoeren); gesendet wird in dieser Zeit nichts (das ginge ohnehin nicht). Sobald TS3 wieder ruft, uebernimmt Mix().
void Engine::PumpMain()
{
    constexpr int       kChunk    = 960;  // 20 ms
    constexpr ULONGLONG kSilentMs = 150;  // so lange ohne Mikrofon-Callback = TS3 mischt gerade nicht
    LARGE_INTEGER       freq{}, last{};
    QueryPerformanceFrequency(&freq);
    bool   running = false;
    double owed    = 0; // noch zu erzeugende Frames
    while (!pumpQuit_.load()) {
        Sleep(10);
        const bool tsIdle = GetTickCount64() - lastMixTick_.load() > kSilentMs;
        const bool work   = sounds_.Active() || (fileActive_.load() && !filePaused_.load());
        if (!tsIdle || !work) {
            running = false;
            continue;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (!running) { // gleich den ersten Block erzeugen, damit der Klick sofort hoerbar wird
            running = true;
            owed    = kChunk;
        } else {
            owed += static_cast<double>(now.QuadPart - last.QuadPart) * 48000.0 / static_cast<double>(freq.QuadPart);
        }
        last = now;
        owed = std::min(owed, 4800.0); // nach einem Haenger nicht nachholen (hoechstens 100 ms)
        while (owed >= kChunk && !pumpQuit_.load()) {
            std::lock_guard<std::mutex> lk(renderMutex_);
            if (GetTickCount64() - lastMixTick_.load() <= kSilentMs) break; // TS3 ruft wieder
            ApplyEqIfChanged();
            bool music = false, voices = false, audible = false;
            RenderSources(kChunk, false, &music, &voices, &audible);
            if (monitor_.load(std::memory_order_relaxed)) monitorRing_.Write(gained_.data(), kChunk); // Mithoeren: fortlaufend, wie in Mix()
            owed -= kChunk;
        }
    }
}

void Engine::Mix(uint64_t schid, short* samples, int frames, int channels, int* edited)
{
    lastSchid_.store(schid, std::memory_order_relaxed);
    lastMixTick_.store(GetTickCount64(), std::memory_order_relaxed);
    if (frames <= 0 || channels <= 0) return;
    const bool tsSends = (*edited & 2) != 0; // laut Plugin-API: TS3 sendet diesen Block (gemeldet, bevor wir etwas aendern)
    ManageMusicMode(schid, frames);
    if (micMuted_.load(std::memory_order_relaxed)) { // Mikrofon stumm: gilt auch ohne Musikquelle, die Musik wird danach noch eingemischt
        std::memset(samples, 0, static_cast<size_t>(frames) * static_cast<size_t>(channels) * sizeof(short));
        *edited |= 1;
    }
    if (!AppOn() && !fileActive_.load(std::memory_order_relaxed) && !sounds_.Active()) {
        holdFrames_ = std::max<int64_t>(0, holdFrames_ - frames);
        if (micGain_.load() != 1.0f) { // Mikrofon-Lautstaerke gilt auch ohne Musik
            mixer::MixInto(reinterpret_cast<int16_t*>(samples), frames, channels, nullptr, 0, micGain_.load(), 1.0f);
            *edited |= 1;
        }
        return;
    }
    std::unique_lock<std::mutex> render(renderMutex_); // die Pump haelt ihn hoechstens fuer einen 20-ms-Block
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    lastBlock_.store(frames, std::memory_order_relaxed);
    ApplyEqIfChanged();

    const size_t need = static_cast<size_t>(frames) * 2;
    bool music = false, mixedVoices = false, audible = false;
    RenderSources(frames, true, &music, &mixedVoices, &audible);

    // Sende-Bit nachhalten: sobald eine Quelle hoerbar ist, kSendHoldFrames lang "senden", unabhaengig vom Pegel einzelner Bloecke.
    if (audible || mixedVoices) holdFrames_ = kSendHoldFrames;
    else holdFrames_ = std::max<int64_t>(0, holdFrames_ - frames);
    const bool audio    = music || mixedVoices;
    const bool sendBit  = alwaysSend_.load() && holdFrames_ > 0;
    if (audio) { // Diagnose: wie oft gibt TS3 den Block zum Senden frei?
        musicBlocks_.fetch_add(1, std::memory_order_relaxed);
        if (tsSends) openBlocks_.fetch_add(1, std::memory_order_relaxed);
        const float e = sendEma_.load(std::memory_order_relaxed);
        sendEma_.store(e + ((tsSends ? 1.0f : 0.0f) - e) * 0.05f, std::memory_order_relaxed);
    }
    if (audio && optimize_.load(std::memory_order_relaxed)) {
        // Mono, wenn das Mikrofon mono ist, "Mono" gewaehlt wurde oder der Kanal Opus Voice nutzt (TS3 mischt dann ohnehin zu Mono)
        optimizer_.Process(gained_.data(), frames, channels == 1 || mono_.load(std::memory_order_relaxed) || codecVoice_.load(std::memory_order_relaxed));
        optimizerActive_ = true;
    } else if (!audio && optimizerActive_) { // Pause: Filterzustand verwerfen, sonst knackt der Hochpass beim Wiedereinsatz
        optimizer_       = VoiceOptimizer();
        optimizerActive_ = false;
    }
    int outL = 0, outR = 0;
    mixer::PeakStereo(gained_.data(), frames, &outL, &outR);
    levelL_.store(outL / 32768.0f, std::memory_order_relaxed);
    levelR_.store(outR / 32768.0f, std::memory_order_relaxed);
    analysis_.Push(gained_.data(), frames); // Spektrum + Tonspur in der Oberflaeche

    if (monitor_.load(std::memory_order_relaxed)) { // fortlaufend (auch Stille), damit die Verzoegerung stimmt
        if (channels == 1) { // Mono-Mikrofon: gesendet wird die Mono-Summe, also genau das auch beim Mithoeren/Anhoeren (sonst klingt es breiter/anders)
            if (monBlock_.size() < need) monBlock_.resize(need);
            for (int i = 0; i < frames; ++i) monBlock_[2 * i] = monBlock_[2 * i + 1] = static_cast<int16_t>((gained_[2 * i] + gained_[2 * i + 1]) / 2);
            monitorRing_.Write(monBlock_.data(), frames);
        } else {
            monitorRing_.Write(gained_.data(), frames);
        }
    }

    if (gateOn_.load(std::memory_order_relaxed)) { // Sprachaktivierung umgangen: eigenes Gate haelt Tastatur-/Raumgeraeusche vom Mikro fern
        gate_.Configure(gateDb_.load(std::memory_order_relaxed));
        gate_.Process(reinterpret_cast<int16_t*>(samples), frames, channels);
    }

    const float micGain = micGain_.load();
    if (audio || micGain != 1.0f)
        mixer::MixInto(reinterpret_cast<int16_t*>(samples), frames, channels, audio ? gained_.data() : nullptr, audio ? frames : 0, micGain, 1.0f);
    if (audio || micGain != 1.0f) *edited |= 1;
    if (sendBit) *edited |= 3;
}


// =============================================================================================================================================
// Datei-Player
// =============================================================================================================================================

// ---- Soundboard ---------------------------------------------------------------------------------------------------------------------------

namespace {
std::wstring FileTitle(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name  = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot   = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return presets::Sanitize(name);
}
} // namespace

void Engine::SoundSetSlot(int slot, const std::wstring& path)
{
    if (slot < 0 || slot >= kSoundSlots) return;
    sounds_.Load(slot, path);
    std::lock_guard<std::mutex> lk(mutex_);
    SoundSlot& b = settings_.sounds[slot];
    b.path       = path;
    b.name       = path.empty() ? L"" : FileTitle(path);
    if (path.empty()) {
        b.color  = 0;
        b.volPct = 100;
        sounds_.SetVolume(slot, 100);
    }
    SaveLocked();
}

void Engine::SoundConfigure(int slot, const std::wstring& path, const std::wstring& name, uint32_t color, int volPct)
{
    if (slot < 0 || slot >= kSoundSlots) return;
    volPct = std::min(200, std::max(0, volPct));
    bool reload;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        SoundSlot&         b = settings_.sounds[slot];
        const std::wstring n = presets::Sanitize(name);
        reload               = b.path != path;
        b.path               = path;
        b.name               = path.empty() ? L"" : (n.empty() ? FileTitle(path) : n);
        b.color              = path.empty() ? 0 : (color & 0xFFFFFF);
        b.volPct             = path.empty() ? 100 : volPct;
        SaveLocked();
    }
    sounds_.SetVolume(slot, path.empty() ? 100 : volPct);
    if (reload) sounds_.Load(slot, path);
}

void Engine::SoundReload(int slot)
{
    if (slot < 0 || slot >= kSoundSlots) return;
    std::wstring path;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        path = settings_.sounds[slot].path;
    }
    if (!path.empty()) sounds_.Load(slot, path);
}

void Engine::SoundSwap(int a, int b)
{
    if (a < 0 || b < 0 || a >= kSoundSlots || b >= kSoundSlots || a == b) return;
    SoundSlot sa, sb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        std::swap(settings_.sounds[a], settings_.sounds[b]);
        sa = settings_.sounds[a];
        sb = settings_.sounds[b];
        SaveLocked();
    }
    sounds_.Load(a, sa.path);
    sounds_.Load(b, sb.path);
    sounds_.SetVolume(a, sa.volPct);
    sounds_.SetVolume(b, sb.volPct);
}

void Engine::SoundRename(int slot, const std::wstring& name)
{
    if (slot < 0 || slot >= kSoundSlots) return;
    std::lock_guard<std::mutex> lk(mutex_);
    if (settings_.sounds[slot].path.empty()) return;
    const std::wstring n = presets::Sanitize(name);
    settings_.sounds[slot].name = n.empty() ? FileTitle(settings_.sounds[slot].path) : n;
    SaveLocked();
}

void Engine::SoundPlay(int slot)
{
    if (slot < 0 || slot >= kSoundSlots) return;
    const Soundboard::State st = sounds_.GetState(slot);
    if (st != Soundboard::State::Ready && st != Soundboard::State::Unloaded && st != Soundboard::State::Loading) return;
    // Bewusst ohne "Musik einspielen": Clips werden im Mikrofon-Callback (bzw. im Ersatztakt) gemischt und brauchen den Schalter nicht. Er bleibt, wie er ist
    // (er wuerde sonst auch die gewaehlte App ins Mikrofon holen).
    sounds_.Play(slot);
    if (wake_) SetEvent(wake_);
}

void Engine::SoundStop() { sounds_.StopAll(); }

void Engine::SetSoundVolume(int pct)
{
    pct         = std::min(200, std::max(0, pct));
    soundGain_  = pct / 100.0f;
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.soundVolPct = pct;
    dirty_                = true;
}

void Engine::SetSoundGrid(int rows, int cols)
{
    rows = std::min(kSoundMaxRows, std::max(1, rows));
    cols = std::min(kSoundMaxCols, std::max(1, cols));
    while (rows * cols > kSoundSlots) (rows > cols ? rows : cols)--; // nie mehr Felder als Knoepfe
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.soundRows = rows;
    settings_.soundCols = cols;
    dirty_              = true;
}

void Engine::SetSoundShowKeys(bool on)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.soundShowKeys = on;
    dirty_                  = true;
}

void Engine::SetSoundMonitor(bool on)
{
    soundMonitor_ = on;
    if (!on && !fileMonitor_.load()) localOut_.Clear(); // Rest verwerfen, wenn gar nichts mehr lokal laufen soll
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.soundMonitor = on;
    SaveLocked();
}

void Engine::SetFileMonitor(bool on)
{
    fileMonitor_ = on;
    if (!on && !soundMonitor_.load()) localOut_.Clear();
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.fileMonitor = on;
    SaveLocked();
}

void Engine::SetSoundPinned(bool on)
{
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.soundPinned = on;
    dirty_                = true;
}

Engine::SoundInfo Engine::GetSound(int slot)
{
    SoundInfo out;
    if (slot < 0 || slot >= kSoundSlots) return out;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const SoundSlot& b = settings_.sounds[slot];
        out.name           = b.name;
        out.path           = b.path;
        out.color          = b.color;
        out.volPct         = b.volPct;
    }
    out.state    = sounds_.GetState(slot);
    out.seconds  = sounds_.Seconds(slot);
    out.position = sounds_.Position(slot);
    return out;
}

std::vector<Engine::SoundInfo> Engine::GetSounds()
{
    std::vector<SoundInfo> out(kSoundSlots);
    for (int i = 0; i < kSoundSlots; ++i) out[i] = GetSound(i);
    return out;
}

void Engine::SetPausedLocked(bool paused)
{
    playerPaused_ = paused;
    filePaused_   = paused;
    if (player_) player_->SetPaused(paused);
}

// Startet Titel `index`. gapless = der Puffer des vorigen Titels spielt weiter, der neue schliesst nahtlos an (Titelende).
void Engine::StartTrackLocked(int index, double startSec, bool gapless)
{
    if (index < 0 || index >= static_cast<int>(playlist_.items.size())) {
        StopPlayerLocked(false);
        return;
    }
    playlist_.current = index;
    player_.reset(); // alten Decoder beenden
    if (!gapless) {
        fileRing_.Clear();
        fileResetRequested_ = true;
    }
    player_ = std::make_unique<FilePlayer>(fileRing_);
    player_->SetOnEnd([this] {
        if (wake_) SetEvent(wake_);
    });
    player_->Start(playlist_.items[index].path, startSec);
    playerPaused_ = false;
    filePaused_   = false;
    playerMsg_.clear();
    drainUntil_ = 0;
    fileActive_ = true;
}

// drain = nach dem letzten Titel noch ~1 s weiterspielen, damit das Ende nicht abgeschnitten wird
void Engine::StopPlayerLocked(bool drain)
{
    player_.reset();
    playerPaused_ = false;
    filePaused_   = false;
    if (drain && fileActive_.load()) {
        drainUntil_ = GetTickCount64() + 1000;
    } else {
        drainUntil_ = 0;
        fileActive_ = false;
        fileRing_.Clear();
        fileResetRequested_ = true;
    }
}

void Engine::HandlePlayerEvents()
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    if (player_ && player_->Ended()) {
        const bool failed = player_->Failed();
        const int  cur    = playlist_.current;
        if (logger_) {
            char line[200];
            std::snprintf(line, sizeof(line), "Datei-Player: Titel %d beendet (Fehler %d, Position %.2f s von %.2f s)", cur, failed ? 1 : 0, player_->Position(), player_->Duration());
            logger_(line);
        }
        if (failed) {
            ++failStreak_;
            playerMsg_ = L"Nicht abspielbar: " + (cur >= 0 && cur < static_cast<int>(playlist_.items.size()) ? playlist_.items[cur].title : std::wstring());
            if (logger_ && cur >= 0 && cur < static_cast<int>(playlist_.items.size())) logger_("Datei konnte nicht abgespielt werden");
        } else {
            failStreak_ = 0;
        }
        int next = playlist_.AfterEnd();
        if (failed && failStreak_ >= playlist_.items.size()) next = -1; // alles kaputt: nicht endlos weiterschalten
        if (next >= 0) {
            const std::wstring keep = playerMsg_;
            StartTrackLocked(next, 0, !failed);
            playerMsg_ = keep;
        } else {
            const std::wstring keep = playerMsg_;
            StopPlayerLocked(!failed);
            playerMsg_ = keep;
        }
    }
    if (!player_ && fileActive_.load() && drainUntil_ && GetTickCount64() > drainUntil_) {
        drainUntil_ = 0;
        fileActive_ = false;
        fileRing_.Clear();
        fileResetRequested_ = true;
    }
}

void Engine::StartProber()
{
    if (proberRunning_.exchange(true)) return; // laeuft schon und holt sich auch die neuen Eintraege
    if (prober_.joinable()) prober_.join();
    prober_ = std::thread([this] {
        for (;;) {
            std::wstring path;
            {
                std::lock_guard<std::mutex> lk(playerMutex_);
                for (auto& t : playlist_.items)
                    if (t.duration == -1) { // -1 = noch nicht versucht, -2 = laeuft, -3 = nicht ermittelbar
                        t.duration = -2;
                        path       = t.path;
                        break;
                    }
            }
            if (path.empty()) {
                proberRunning_ = false;
                bool more      = false; // Rennen: zwischen Suche und Abmelden kam evtl. ein neuer Eintrag dazu
                {
                    std::lock_guard<std::mutex> lk(playerMutex_);
                    for (const auto& t : playlist_.items) more = more || t.duration == -1;
                }
                if (!more || proberRunning_.exchange(true)) return;
                continue;
            }
            const double d = FilePlayer::ProbeDuration(path);
            std::lock_guard<std::mutex> lk(playerMutex_);
            for (auto& t : playlist_.items)
                if (t.path == path && t.duration == -2) t.duration = d >= 0 ? d : -3;
        }
    });
}

int Engine::PlayerAdd(const std::vector<std::wstring>& paths)
{
    int added = 0;
    {
        std::lock_guard<std::mutex> lk(playerMutex_);
        for (const auto& p : paths) {
            const bool dup = std::any_of(playlist_.items.begin(), playlist_.items.end(), [&](const Track& t) { return playlist::ToLowerCopy(t.path) == playlist::ToLowerCopy(p); });
            if (!dup && playlist_.Add(p) >= 0) ++added; // Duplikate nicht doppelt aufnehmen
        }
        if (added) RememberPlaylistLocked();
    }
    if (added) StartProber();
    return added;
}

// Datei-Player braucht "Musik einspielen" nicht (der Schalter gilt nur fuer die App): Play schaltet ihn bewusst nicht ein.
void Engine::PlayerPlay(int index)
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    const int n = static_cast<int>(playlist_.items.size());
    if (n == 0) return;
    if (index < 0) {
        if (player_ && player_->Running()) SetPausedLocked(false); // laeuft schon bzw. ist pausiert -> fortsetzen
        else StartTrackLocked(playlist_.current >= 0 && playlist_.current < n ? playlist_.current : 0, 0, false);
    } else {
        StartTrackLocked(index, 0, false);
    }
}

void Engine::PlayerTogglePause()
{
    {
        std::lock_guard<std::mutex> lk(playerMutex_);
        if (player_ && player_->Running()) {
            SetPausedLocked(!playerPaused_);
            return;
        }
    }
    PlayerPlay(-1);
}

void Engine::PlayerStop()
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    StopPlayerLocked(false);
}

void Engine::PlayerNext()
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    if (!fileActive_.load()) return;
    StartTrackLocked(playlist_.Next(), 0, false);
}

void Engine::PlayerPrev()
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    if (!fileActive_.load()) return;
    if (player_ && player_->Running() && player_->Position() > 3.0) { // nach 3 s: erst an den Anfang des Titels
        player_->Seek(0);
        fileResetRequested_ = true;
        return;
    }
    StartTrackLocked(playlist_.Prev(), 0, false);
}

void Engine::PlayerSeek(double seconds)
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    if (playlist_.current < 0 || playlist_.current >= static_cast<int>(playlist_.items.size())) return;
    seconds = std::max(0.0, seconds);
    if (player_ && player_->Running()) {
        player_->Seek(seconds);
        fileResetRequested_ = true;
    } else { // Datei war schon zu Ende (oder gestoppt): ab der Stelle neu starten
        StartTrackLocked(playlist_.current, seconds, false);
    }
}

void Engine::PlayerRemove(int index)
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    const bool wasCurrent = index == playlist_.current && fileActive_.load();
    const bool wasPaused  = playerPaused_;
    if (!playlist_.Remove(index)) return;
    RememberPlaylistLocked();
    if (wasCurrent) {
        StartTrackLocked(index < static_cast<int>(playlist_.items.size()) ? index : -1, 0, false); // der naechste rueckt nach
        if (wasPaused && player_) SetPausedLocked(true); // pausiert bleibt pausiert
    }
}

void Engine::PlayerClear()
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    playlist_.Clear();
    StopPlayerLocked(false);
    RememberPlaylistLocked();
}

void Engine::PlayerSetRepeat(RepeatMode mode)
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    playlist_.repeat = mode;
    RememberPlaylistLocked();
}

void Engine::RememberPlaylistLocked()
{
    std::vector<std::wstring> paths;
    paths.reserve(playlist_.items.size());
    for (const auto& t : playlist_.items) paths.push_back(t.path);
    std::lock_guard<std::mutex> lk(mutex_);
    settings_.lastPlaylist = std::move(paths);
    settings_.lastRepeat   = static_cast<int>(playlist_.repeat);
    dirty_                 = true; // der Watcher speichert (nicht bei jedem Klick auf die Platte)
}

PresetResult Engine::PlaylistSave(const std::wstring& name)
{
    std::vector<std::wstring> paths;
    {
        std::lock_guard<std::mutex> lk(playerMutex_);
        for (const auto& t : playlist_.items) paths.push_back(t.path);
    }
    std::lock_guard<std::mutex> lk(mutex_);
    const PresetResult r = settings_.playlists.Save(name, paths);
    if (r == PresetResult::Ok || r == PresetResult::Updated) {
        settings_.playlistName = presets::Sanitize(name);
        SaveLocked();
    }
    return r;
}

PresetResult Engine::PlaylistRename(const std::wstring& from, const std::wstring& to)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const PresetResult r = settings_.playlists.Rename(from, to);
    if (r == PresetResult::Ok) {
        if (presets::EqualNoCase(settings_.playlistName, from)) settings_.playlistName = presets::Sanitize(to);
        SaveLocked();
    }
    return r;
}

bool Engine::PlaylistDelete(const std::wstring& name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const bool ok = settings_.playlists.Delete(name);
    if (ok) {
        if (presets::EqualNoCase(settings_.playlistName, name)) settings_.playlistName.clear();
        SaveLocked();
    }
    return ok;
}

bool Engine::PlaylistLoad(const std::wstring& name)
{
    SavedPlaylist pl;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const SavedPlaylist* f = settings_.playlists.Find(name);
        if (!f) return false;
        pl                     = *f;
        settings_.playlistName = pl.name;
    }
    {
        std::lock_guard<std::mutex> lk(playerMutex_);
        StopPlayerLocked(false);
        playlist_.Clear();
        playerMsg_.clear();
        for (const auto& p : pl.paths) playlist_.Add(p);
        RememberPlaylistLocked();
    }
    StartProber();
    return true;
}

std::vector<std::wstring> Engine::PlaylistNames()
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::wstring>   names;
    for (const auto& p : settings_.playlists.items) names.push_back(p.name);
    return names;
}

PlayerSnapshot Engine::GetPlayer()
{
    std::lock_guard<std::mutex> lk(playerMutex_);
    PlayerSnapshot snap;
    snap.items   = playlist_.items;
    snap.current = playlist_.current;
    snap.repeat  = playlist_.repeat;
    snap.message = playerMsg_;
    snap.active  = player_ && player_->Running();
    snap.paused  = snap.active && playerPaused_;
    if (player_ && snap.current >= 0 && snap.current < static_cast<int>(snap.items.size())) {
        snap.position = std::max(0.0, player_->Position() - static_cast<double>(fileReader_.buffered.load()) / 48000.0); // Vorrat im Reader abziehen
        snap.duration = snap.items[snap.current].duration > 0 ? snap.items[snap.current].duration : player_->Duration();
    } else if (snap.current >= 0 && snap.current < static_cast<int>(snap.items.size())) {
        snap.duration = snap.items[snap.current].duration;
    }
    return snap;
}

std::wstring Engine::SourceLabel()
{
    const std::wstring exe = GetSettings().targetExe;
    return exe.empty() ? std::wstring(Tr(L"keine App gewählt")) : PrettyExeName(exe);
}
