#include "config_dialog.h"
#include "remote.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <atomic>
#include <cmath>
#include <cwchar>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app_discovery.h"
#include "engine.h"
#include "equalizer.h"
#include "fft.h"
#include "ui_theme.h"

namespace {

using namespace ui;

constexpr UINT     WM_APPS_READY = WM_APP + 1;
constexpr UINT_PTR kTimerId      = 1;
constexpr int      kTimerMs      = 33;
constexpr int      kRefreshTicks = 30; // ~1 s: App-Liste ist live
constexpr float    kSide = 88;           // Breite der Seitenleiste
constexpr float    kW = 908, kH = 624;
constexpr float    kPad = 20; // Innenabstand der Karten
constexpr float    kGap = 12; // Abstand zwischen Karten und Kacheln
constexpr int      kMaxDelayMs = 2000;
constexpr int      kFftSize    = 8192;
constexpr int      kGridCols   = 6;

enum class Hit {
    None, TabMix, TabEq, TabFiles,
    // Seite "Dateien"
    FilePrev, FilePlay, FileStop, FileNext, FileRepeat, FileClear, FileOpen, FileSeek, FileRow, FileRemove,
    // Seite "Quelle & Mix"
    MainToggle, MenuItem, MenuBody, MicMute, Tile, Grid, SliderMusic, SliderMic, MonitorToggle, SliderDelay, AlwaysToggle,
    // Seite "Equalizer"
    EqToggle, EqBandsBtn, PresetField, PresetItem, PresetPopup, PresetSave, PresetRename, PresetDelete, EqPlot, Listen, ListenToggle, QualityBtn, MonoToggle, MusicModeToggle, SliderGate, OptimizeToggle, OpusToggle,
    // Profile, Titel melden, Kopfzeile, Soundboard
    TabSounds, TabSettings, SettingsGear, SettingsPanel, SettingsBackdrop, LangAuto, AdvToggle, ProfPrev, ProfNext, ProfField, ProfSave, ProfRename, ProfDelete, NowChat, NowChannel, NowMeta, HotkeyBtn, LangDe, LangEn, SoundTile, SoundVol, SoundStop,
    SoundEditOk, SoundEditCancel
};
struct HitInfo {
    Hit hit = Hit::None;
    int idx = -1;
    bool operator==(const HitInfo& o) const { return hit == o.hit && idx == o.idx; }
};

enum Anim { A_MAIN, A_ALWAYS, A_MONITOR, A_EQ, A_MONO, A_MUSIC, A_LISTEN, A_OPT, A_OPUS, A_NOWCHAT, A_NOWCHANNEL, A_NOWMETA, A_COUNT };

struct Layout {
    float s = 1;
    RectF header, tab[4];
    // Seite 2 (Dateien)
    RectF filesNow, fileSeek, btnPrev, btnPlay, btnStop, btnNext, btnRepeat, btnClear, btnOpen, fileCard, fileArea;
    // Seite 0
    RectF micMute, mainToggleRow, mainToggle, gridCard, gridArea, mixCard, meterL, meterR, sliderMusic, sliderMic, monitorCard, monitorRow, monitorToggle, sliderDelay;
    // Seite 1
    RectF eqCard, bandBtn[3], eqToggle, presetField, presetSave, presetRename, presetDelete, plot, listenBtn, listenToggle, tonCard, tonPlot, qualBtn[3], monoToggle, musicModeToggle, sliderGate, optimizeToggle, opusToggle;
    RectF diag, codec;
    // Seite 0 (Profile) / Seite 4 (Einstellungen: Allgemein, Senden, Titel melden, Info, Erweitert)
    RectF profCard, profPrev, profNext, profField, profSave, profRename, profDelete;
    RectF setGeneral, langBtn[3], hotkeyBtn, nowCard, nowToggle[3], sendCard, optToggle, infoCard, advCard, advHeader;
    RectF logo; // Logo oben in der Seitenleiste
    RectF gearBtn, settingsPanel; // Zahnrad neben dem Status-Schild, Panel des Einstellungs-Popups
    RectF soundCard, soundTile[kSoundSlots], soundVol, soundStop, soundEditField, soundEditOk, soundEditCancel;
    float tileW = 0, tileH = 0, gap = 0;
};

Layout MakeLayout(float s)
{
    Layout l;
    l.s = s;
    // Seitenleiste (x 0-88, volle Hoehe) mit Logo und 4 Navigationspunkten, rechts davon die Kopfleiste (Seitentitel, Zahnrad, Status-Schild).
    // Die Seiten sind im alten Koordinatensystem entworfen (x ab 16, y ab 112, enden bei y 628) und werden hier um die Seitenleiste nach rechts
    // und um 40 px nach oben geschoben: Inhalt ab y 72, Fusszeile y 600.
    auto A = [s](float x, float y, float w, float h) { return RectF(x * s, y * s, w * s, h * s); };
    auto R = [s](float x, float y, float w, float h) { return RectF((x + kSide) * s, (y - 40) * s, w * s, h * s); };
    l.header = A(kSide + 16, 12, 788, 48);
    l.logo   = A(22, 16, 44, 44);
    for (int i = 0; i < 4; ++i) l.tab[i] = A(8, 92 + i * 76, kSide - 16, 68); // Quelle | EQ | Dateien | Sounds
    l.gearBtn       = A(kSide + 16 + 788 - 14 - 92 - 10 - 32, 20, 32, 32); // links neben dem Status-Schild
    l.settingsPanel = R(8, 104, 804, 532);

    // Seite 2 "Dateien"
    l.filesNow  = R(16, 112, 788, 150);
    l.fileSeek  = R(36, 112 + 62, 748, 22);
    l.btnPrev   = R(36, 216, 46, 34);
    l.btnPlay   = R(90, 216, 70, 34);
    l.btnStop   = R(168, 216, 46, 34);
    l.btnNext   = R(222, 216, 46, 34);
    l.btnRepeat = R(286, 216, 156, 34);
    l.btnClear  = R(450, 216, 120, 34);
    l.btnOpen   = R(614, 216, 170, 34);
    l.fileCard  = R(16, 274, 788, 354);
    l.fileArea  = R(24, 274 + 40, 772, 354 - 48);

    // Seite 0 "Quelle & Mix": Karte "Audio-Quelle" mit Hauptschalter in der Titelleiste, darunter zwei gleich hohe Spalten (232)
    l.gridCard      = R(16, 112, 788, 272);
    l.mainToggleRow = R(552, 112 + 4, 236, 36);
    l.mainToggle    = R(16 + 788 - kPad - 48, 112 + 9, 48, 26);
    l.gridArea      = R(32, 112 + 44, 756, 212);
    l.gap           = kGap * s;
    l.tileH         = 100 * s;
    l.tileW         = (l.gridArea.Width - (kGridCols - 1) * l.gap) / kGridCols;
    l.mixCard     = R(16, 396, 388, 232);
    l.meterL      = R(36, 396 + 46, 348, 12);
    l.meterR      = R(36, 396 + 64, 348, 12);
    l.sliderMusic = R(36, 396 + 116, 348, 22); // Klickbereich, die Schiene liegt mittig darin
    l.sliderMic   = R(36, 396 + 176, 348, 22);
    l.micMute     = R(36 + 348 - 80 - 36, 396 + 176 - 27, 30, 24); // Stumm-Knopf links neben dem Wert der Mikrofon-Lautstaerke
    l.monitorCard   = R(416, 396, 388, 110);
    l.monitorRow    = R(416, 396, 388, 56);
    l.monitorToggle = R(416 + 388 - kPad - 48, 396 + 14, 48, 26);
    l.sliderDelay   = R(436, 396 + 76, 348, 22);
    l.profCard   = R(416, 518, 388, 110);
    l.profPrev   = R(436, 518 + 36, 30, 26);
    l.profField  = R(472, 518 + 36, 276, 26);
    l.profNext   = R(754, 518 + 36, 30, 26);
    l.profSave   = R(436, 518 + 70, 140, 26);
    l.profRename = R(584, 518 + 70, 96, 26);
    l.profDelete = R(688, 518 + 70, 96, 26);

    // Seite 1 "Equalizer": nur Klangformung (EQ, Anhoeren, Tonspur)
    l.eqCard = R(16, 112, 788, 380);
    for (int i = 0; i < 3; ++i) l.bandBtn[i] = R(150 + i * 56, 112 + 10, 52, 26);
    l.eqToggle = R(16 + 788 - kPad - 48, 112 + 10, 48, 26);
    l.presetField  = R(36, 112 + 46, 296, 28);
    l.presetSave   = R(342, 112 + 46, 128, 28);
    l.presetRename = R(478, 112 + 46, 104, 28);
    l.presetDelete = R(590, 112 + 46, 124, 28);
    l.plot      = R(72, 112 + 84, 712, 220);
    l.listenBtn = R(16 + 788 - kPad - 220, 112 + 380 - 44, 220, 30);
    l.listenToggle = R(16 + 788 - kPad - 220 - 14 - 48, 112 + 380 - 44 + 2, 48, 26);
    l.tonCard   = R(16, 504, 788, 124);
    l.tonPlot   = R(36, 504 + 34, 748, 76);
    l.diag      = R(20, 640, 290, 16);
    l.codec     = R(310, 640, 474, 16);

    // Seite 3 "Sounds": 4 x 2 Kacheln (178 x 150, Abstand 12), darunter Lautstaerke und "Alle stoppen"
    l.soundCard = R(16, 112, 788, 516);
    for (int i = 0; i < kSoundSlots; ++i) l.soundTile[i] = R(36 + (i % 4) * 190, 182 + (i / 4) * 162, 178, 150);
    l.soundVol  = R(36, 552, 420, 22);
    l.soundStop = R(616, 540, 168, 34);
    l.soundEditField  = R(36, 112 + 44, 420, 28);
    l.soundEditOk     = R(464, 112 + 44, 110, 28);
    l.soundEditCancel = R(582, 112 + 44, 110, 28);

    // Seite 4 "Einstellungen": striktes Grid. Reihe 1 (h 204): Allgemein | Senden; Reihe 2 (h 128): Titel melden | Info; Reihe 3: Erweitert
    l.setGeneral = R(16, 112, 388, 204);
    for (int i = 0; i < 3; ++i) l.langBtn[i] = R(36 + i * 120, 112 + 66, 108, 28);
    l.hotkeyBtn = R(36, 112 + 148, 348, 30);
    l.sendCard   = R(416, 112, 388, 204);
    l.optToggle  = R(416 + 388 - kPad - 48, 112 + 36, 48, 26);  // Musik immer senden
    l.monoToggle = R(416 + 388 - kPad - 48, 112 + 86, 48, 26);  // Mono einspielen
    for (int i = 0; i < 3; ++i) l.qualBtn[i] = R(436 + i * 120, 112 + 160, 108, 28);
    l.nowCard   = R(16, 328, 388, 128);
    for (int i = 0; i < 3; ++i) l.nowToggle[i] = R(16 + 388 - kPad - 48, 328 + 34 + i * 28, 48, 26);
    l.infoCard  = R(416, 328, 388, 128);
    l.advHeader = R(16, 468, 788, 44);
    l.advCard   = R(16, 468, 788, 150); // aufgeklappte Groesse
    l.musicModeToggle = R(16 + 388 - kPad - 48, 468 + 54, 48, 26);
    l.sliderGate      = R(136, 468 + 106, 248, 22);
    l.optimizeToggle  = R(416 + 388 - kPad - 48, 468 + 54, 48, 26);
    l.opusToggle      = R(416 + 388 - kPad - 48, 468 + 104, 48, 26);
    return l;
}

bool In(const RectF& r, int x, int y) { return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height; }

// 0..1 linear -> 0..1 Anzeige (dB-Skala -54..0)
float LevelToDisplay(float v)
{
    if (v <= 0.0005f) return 0;
    float db = 20.0f * std::log10(v);
    return std::clamp((db + 54.0f) / 54.0f, 0.0f, 1.0f);
}

std::wstring FormatFreq(double f)
{
    wchar_t b[32];
    if (f >= 1000) {
        swprintf(b, 32, L"%.1f kHz", f / 1000.0);
        std::wstring s = b;
        size_t       p = s.find(L".0");
        if (p != std::wstring::npos) s.erase(p, 2);
        return s;
    }
    swprintf(b, 32, L"%.0f Hz", f);
    return b;
}

std::wstring FormatDb(float db)
{
    wchar_t b[32];
    swprintf(b, 32, L"%+.1f dB", db);
    return b;
}

// Eine Kachel der Quellen-Auswahl. Die Reihenfolge bleibt stabil (neue Apps kommen hinten dazu), Positionen gleiten weich in ihren Platz.
struct TileState {
    AppInfo info;
    float   x = 0, y = 0; // animierte Position in Raster-Einheiten (Spalte, Zeile)
    float   amp = 0;      // Pegel-Animation 0..1, folgt info.playing
    float   phase = 0;    // Versatz, damit nicht alle Kacheln im Gleichtakt tanzen
    bool    alive = true;
};

struct MenuEntry {
    std::wstring label;
    int          action; // 1 = App ausblenden, 2 = alle Apps einblenden
    bool         enabled;
    std::wstring arg;
};
struct MenuState {
    bool                  open = false;
    float                 x = 0, y = 0;
    std::vector<MenuEntry> items;
};

struct Ctx {
    HWND                          hwnd = nullptr;
    Layout                        lay;
    int                           page = 0;
    float                         pageT = 1;    // Seitenwechsel: 0 = gerade gewechselt, 1 = fertig (gleitet ein)
    float                         popupT = 0;   // Einstellungs-Popup: 0 = zu, 1 = offen (blendet weich ein/aus)
    float                         navPos = 0; // gleitende Markierung der Seitenleiste (Seitenindex, weich nachgefuehrt)
    bool                          settingsOpen = false; // Einstellungs-Popup (Zahnrad) offen
    std::vector<TileState>        tiles;
    bool                          haveApps = false;
    MenuState                     menu;
    std::map<std::wstring, HICON> icons; // pro Exe-Pfad, Besitz hier
    HitInfo                       hover;
    HitInfo                       drag;
    int                           lastBand = -1;
    float                         scroll = 0;
    float                         fileScroll = 0;      // Wiedergabeliste (Seite Dateien)
    bool                          seekDrag = false;    // Suchleiste wird gezogen (erst beim Loslassen springen)
    float                         seekFrac = 0;
    float                         anim[A_COUNT] = {};
    float                         dispL = 0, dispR = 0, holdL = 0, holdR = 0;
    std::vector<float>            spec; // Spektrum je EQ-Band, 0..1 (mit Abklingen)
    int                           tick = 0;
    std::thread                   refresher;
    std::atomic<bool>             refreshing{false};
    std::unique_ptr<Font>         fTitle, fHead, fBody, fSmall, fTiny, fSign; // fSign: Schreibschrift fuer die Namen der Autoren
    std::unique_ptr<Bitmap>       appIcon;              // APP-ICON.png aus den Ressourcen der DLL
    HICON                         iconBig = nullptr, iconSmall = nullptr;

    // Preset-Auswahl (Dropdown), Namenseingabe, Meldungen
    bool         presetOpen   = false;
    float        presetScroll = 0;
    struct {
        bool         active = false, rename = false, selectAll = false;
        std::wstring text, original;
        int          kind = 0; // 0 = EQ-Filter, 1 = Profil, 2 = Sound-Name
        int          slot = -1; // Sound-Slot bei kind 2
    } edit;
    int          deleteArm  = 0; // verbleibende Ticks, in denen "Wirklich loeschen?" gilt
    std::wstring toast;
    bool         toastBad    = false;
    int          toastTicks  = 0;
    float        soundFlash[kSoundSlots] = {}; // Aufleuchten einer Sound-Kachel nach dem Klick
};

// APP-ICON.png aus den Ressourcen der DLL (nicht der .exe von TeamSpeak!) laden
std::unique_ptr<Bitmap> LoadAppIcon()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&LoadAppIcon), &self)) return nullptr;
    HRSRC   res  = FindResourceW(self, MAKEINTRESOURCEW(101), RT_RCDATA);
    HGLOBAL mem  = res ? LoadResource(self, res) : nullptr;
    const void* data = mem ? LockResource(mem) : nullptr;
    if (!data) return nullptr;
    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), SizeofResource(self, res));
    if (!stream) return nullptr;
    auto bmp = std::unique_ptr<Bitmap>(Bitmap::FromStream(stream));
    stream->Release();
    if (!bmp || bmp->GetLastStatus() != Ok) return nullptr;
    return bmp;
}

// Fenster-/Taskleisten-Icon in der gewuenschten Groesse
HICON MakeWindowIcon(Bitmap& src, int px)
{
    Bitmap   scaled(px, px, PixelFormat32bppARGB);
    Graphics gg(&scaled);
    gg.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    gg.Clear(Color(0, 0, 0, 0));
    gg.DrawImage(&src, Rect(0, 0, px, px));
    HICON icon = nullptr;
    scaled.GetHICON(&icon);
    return icon;
}

std::mutex        g_mutex;
std::thread       g_thread;
std::atomic<bool> g_running{false};
std::atomic<HWND> g_hwnd{nullptr};

float GetScale()
{
    HDC   dc  = GetDC(nullptr);
    float dpi = static_cast<float>(GetDeviceCaps(dc, LOGPIXELSX));
    ReleaseDC(nullptr, dc);
    float scale = dpi / 96.0f;
    // Kleine Bildschirme (z.B. 1366x768): das Fenster (inkl. Titelleiste) muss in den Arbeitsbereich passen
    RECT work{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const float fitH = (static_cast<float>(work.bottom - work.top) - 48.0f) / kH, fitW = (static_cast<float>(work.right - work.left) - 24.0f) / kW;
        scale = std::min(scale, std::min(fitH, fitW));
    }
    return std::max(0.5f, scale);
}

HICON IconFor(Ctx* c, const std::wstring& path)
{
    if (path.empty()) return nullptr;
    auto it = c->icons.find(path);
    if (it != c->icons.end()) return it->second;
    SHFILEINFOW info{};
    HICON       icon = nullptr;
    if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON)) icon = info.hIcon;
    c->icons[path] = icon;
    return icon;
}

void StartRefresh(Ctx* c)
{
    if (c->refreshing.exchange(true)) return;
    if (c->refresher.joinable()) c->refresher.join();
    HWND hwnd    = c->hwnd;
    c->refresher = std::thread([c, hwnd] {
        auto* apps = new std::vector<AppInfo>(DiscoverApps());
        if (!PostMessageW(hwnd, WM_APPS_READY, 0, reinterpret_cast<LPARAM>(apps))) delete apps;
        c->refreshing = false;
    });
}

int AliveCount(const Ctx* c)
{
    int n = 0;
    for (const auto& t : c->tiles) n += t.alive ? 1 : 0;
    return n;
}

float MaxScroll(Ctx* c)
{
    const int rows = (AliveCount(c) + kGridCols - 1) / kGridCols;
    return std::max(0.0f, rows * (c->lay.tileH + c->lay.gap) - c->lay.gap - c->lay.gridArea.Height);
}

RectF TileRect(const Ctx* c, const TileState& t)
{
    const Layout& l = c->lay;
    return RectF(l.gridArea.X + t.x * (l.tileW + l.gap), l.gridArea.Y + t.y * (l.tileH + l.gap) - c->scroll, l.tileW, l.tileH);
}

// Neue Liste aus der Hintergrund-Abfrage uebernehmen: vorhandene Kacheln aktualisieren, verschwundene entfernen, neue hinten anhaengen.
// Ausgeblendete Apps fehlen; nur die aktive Quelle bleibt immer sichtbar (auch wenn sie gerade nicht laeuft).
void MergeApps(Ctx* c, std::vector<AppInfo>&& apps)
{
    const Settings st = Engine::Get().GetSettings();
    auto hidden = [&](const std::wstring& exe) {
        return exe != st.targetExe && std::find(st.hiddenApps.begin(), st.hiddenApps.end(), exe) != st.hiddenApps.end();
    };
    if (!st.targetExe.empty() && !FindByExe(apps, st.targetExe)) { // gespeicherte, aber nicht laufende Quelle
        AppInfo missing;
        missing.exe = st.targetExe;
        apps.push_back(missing);
    }

    std::vector<bool> used(apps.size(), false);
    for (auto& t : c->tiles) {
        t.alive = false;
        for (size_t i = 0; i < apps.size(); ++i)
            if (apps[i].exe == t.info.exe && !hidden(apps[i].exe)) {
                t.info  = apps[i];
                t.alive = true;
                used[i] = true;
                break;
            }
    }
    c->tiles.erase(std::remove_if(c->tiles.begin(), c->tiles.end(), [](const TileState& t) { return !t.alive; }), c->tiles.end());

    for (size_t i = 0; i < apps.size(); ++i) {
        if (used[i] || hidden(apps[i].exe)) continue;
        TileState t;
        t.info  = apps[i];
        t.x     = static_cast<float>(c->tiles.size() % kGridCols);
        t.y     = static_cast<float>(c->tiles.size() / kGridCols);
        t.phase = static_cast<float>(std::hash<std::wstring>()(apps[i].exe) % 628) / 100.0f;
        c->tiles.push_back(std::move(t));
    }
    c->haveApps = true;
    c->scroll   = std::clamp(c->scroll, 0.0f, MaxScroll(c));
}

// Pro Takt: Kacheln gleiten auf ihren Platz, die Pegelanimation blendet sanft ein/aus.
void AnimateTiles(Ctx* c)
{
    for (size_t k = 0; k < c->tiles.size(); ++k) {
        TileState& t   = c->tiles[k];
        const float tx = static_cast<float>(k % kGridCols), ty = static_cast<float>(k / kGridCols);
        t.x += (tx - t.x) * 0.28f;
        t.y += (ty - t.y) * 0.28f;
        if (std::fabs(tx - t.x) < 0.005f) t.x = tx;
        if (std::fabs(ty - t.y) < 0.005f) t.y = ty;
        const float target = t.info.playing ? 1.0f : 0.0f;
        t.amp += (target - t.amp) * 0.12f;
        if (std::fabs(target - t.amp) < 0.01f) t.amp = target;
    }
    c->scroll = std::clamp(c->scroll, 0.0f, MaxScroll(c));
}

// Rechtsklick-Menue (eigene Zeichnung im Fenster-Stil)
RectF MenuRect(const Layout& l, const MenuState& m)
{
    const float w = 260 * l.s, h = (m.items.size() * 32 + 12) * l.s;
    return RectF(std::min(m.x, kW * l.s - w - 8 * l.s), std::min(m.y, kH * l.s - h - 8 * l.s), w, h);
}

RectF MenuItemRect(const Layout& l, const MenuState& m, size_t i)
{
    const RectF r = MenuRect(l, m);
    return RectF(r.X + 6 * l.s, r.Y + 6 * l.s + i * 32 * l.s, r.Width - 12 * l.s, 32 * l.s);
}

// Kleine "Audio-Spur": vier abgerundete Balken, die weich auf und ab laufen (zwei ueberlagerte Sinuswellen je Balken; nicht die echte Spur der App).
void DrawActivityBars(Graphics& g, float x, float yBase, float s, float amp, double time, float phase)
{
    static const double w1[4] = {6.1, 7.7, 5.3, 8.9}, w2[4] = {3.3, 4.1, 2.7, 3.9};
    const float         bw = 3 * s, gap = 2 * s, maxH = 14 * s;
    for (int i = 0; i < 4; ++i) {
        const double a = 0.5 + 0.5 * std::sin(w1[i] * time + phase * (i + 1) * 0.7);
        const double b = 0.5 + 0.5 * std::sin(w2[i] * time + phase + i * 1.9);
        const float  h = std::max(2.0f * s, static_cast<float>((0.22 + 0.78 * (0.6 * a + 0.4 * b)) * amp) * maxH);
        FillRound(g, RectF(x + i * (bw + gap), yBase - h, bw, h), bw / 2, C(col::ok, static_cast<BYTE>(235 * std::min(1.0f, amp * 1.6f))));
    }
}

int PlotBand(const Layout& l, int bands, int x)
{
    const float colW = l.plot.Width / bands;
    return std::clamp(static_cast<int>((x - l.plot.X) / colW), 0, bands - 1);
}

float PlotGain(const Layout& l, int y)
{
    float db = eq::kMaxDb - (y - l.plot.Y) / l.plot.Height * 2 * eq::kMaxDb;
    db       = std::clamp(db, -eq::kMaxDb, eq::kMaxDb);
    db       = std::round(db * 2.0f) / 2.0f;
    return std::fabs(db) < 0.4f ? 0.0f : db;
}

// ---- Preset-Auswahl -------------------------------------------------------------------------------------------------------------------------

struct PRow {
    int          type; // 0 Ueberschrift, 1 Eintrag, 2 Hinweis
    std::wstring label;
    int          id; // 0..kPresetCount-1 = mitgeliefert, 100+i = eigener Filter i
};

std::vector<PRow> PopupRows(const Settings& st)
{
    std::vector<PRow> r;
    r.push_back({0, Tr(L"STANDARD"), -1});
    for (int i = 0; i < eq::kPresetCount; ++i) r.push_back({1, eq::PresetName(i), i});
    r.push_back({0, Tr(L"EIGENE FILTER"), -1});
    if (st.userPresets.items.empty()) r.push_back({2, Tr(L"Noch keiner – „Speichern als …“ legt einen an"), -1});
    for (size_t u = 0; u < st.userPresets.items.size(); ++u) r.push_back({1, st.userPresets.items[u].name, 100 + static_cast<int>(u)});
    return r;
}

float RowHeight(int type, float s) { return (type == 0 ? 24.0f : 28.0f) * s; }

float PopupContent(const Layout& l, const std::vector<PRow>& rows)
{
    float h = 8 * l.s;
    for (const auto& r : rows) h += RowHeight(r.type, l.s);
    return h;
}

RectF PopupRect(const Layout& l, const std::vector<PRow>& rows)
{
    return RectF(l.presetField.X, l.presetField.Y + l.presetField.Height + 4 * l.s, l.presetField.Width, std::min(PopupContent(l, rows), 330 * l.s));
}

RectF PopupRowRect(const Layout& l, const std::vector<PRow>& rows, size_t index, float scroll)
{
    const RectF pr = PopupRect(l, rows);
    float       y  = pr.Y + 4 * l.s - scroll;
    for (size_t i = 0; i < index; ++i) y += RowHeight(rows[i].type, l.s);
    return RectF(pr.X + 4 * l.s, y, pr.Width - 8 * l.s, RowHeight(rows[index].type, l.s));
}

// Welches Preset ist gerade gewaehlt? Ein gewaehlter Name zaehlt auch dann, wenn die Kurve danach veraendert wurde ("geaendert").
struct CurPreset {
    std::wstring label = Tr(L"Eigene Kurve");
    bool         user = false, builtin = false, modified = false;
};

CurPreset CurrentPreset(const Settings& st)
{
    const int          N = st.eqBands;
    std::vector<float> gains = st.eqGains;
    gains.resize(N, 0.0f);
    auto builtinMatches = [&](int i) { return presets::CurvesMatch(eq::PresetGains(i, N), gains, 0.01f); };

    CurPreset cur;
    if (!st.eqPresetName.empty()) {
        if (const EqPreset* u = st.userPresets.Find(st.eqPresetName)) {
            cur.label    = u->name;
            cur.user     = true;
            cur.modified = !presets::CurvesMatch(presets::CurveFor(*u, N), gains, 0.75f);
            return cur;
        }
        for (int i = 0; i < eq::kPresetCount; ++i)
            if (presets::EqualNoCase(st.eqPresetName, eq::PresetName(i))) {
                cur.label    = eq::PresetName(i);
                cur.builtin  = true;
                cur.modified = !builtinMatches(i);
                return cur;
            }
    }
    for (int i = 0; i < eq::kPresetCount; ++i) // nichts gewaehlt: passt die Kurve zufaellig zu einem Preset?
        if (builtinMatches(i)) {
            cur.label   = eq::PresetName(i);
            cur.builtin = true;
            return cur;
        }
    for (const auto& u : st.userPresets.items)
        if (presets::CurvesMatch(presets::CurveFor(u, N), gains, 0.75f)) {
            cur.label = u.name;
            cur.user  = true;
            return cur;
        }
    return cur;
}

// Treffer im Einstellungs-Popup: alles ausserhalb des Panels ist Hintergrund (Klick schliesst das Popup)
HitInfo HitSettings(Ctx* c, int x, int y)
{
    const Layout& l = c->lay;
        auto rowOf = [&](const RectF& toggle, float colX, float colW) { return RectF(colX + 6 * l.s, toggle.Y - 4 * l.s, colW - 12 * l.s, toggle.Height + 8 * l.s); };
        for (int i = 0; i < 3; ++i)
            if (In(l.langBtn[i], x, y)) return {i == 0 ? Hit::LangAuto : (i == 1 ? Hit::LangDe : Hit::LangEn), i};
        if (In(l.hotkeyBtn, x, y)) return {Hit::HotkeyBtn, -1};
        for (int i = 0; i < 3; ++i)
            if (In(rowOf(l.nowToggle[i], l.nowCard.X, l.nowCard.Width), x, y)) return {i == 0 ? Hit::NowChat : (i == 1 ? Hit::NowChannel : Hit::NowMeta), i};
        if (In(rowOf(l.optToggle, l.sendCard.X, l.sendCard.Width), x, y)) return {Hit::AlwaysToggle, -1};
        if (In(rowOf(l.monoToggle, l.sendCard.X, l.sendCard.Width), x, y)) return {Hit::MonoToggle, -1};
        for (int i = 0; i < 3; ++i)
            if (In(l.qualBtn[i], x, y)) return {Hit::QualityBtn, i};
        if (In(l.advHeader, x, y)) return {Hit::AdvToggle, -1};
        if (Engine::Get().GetSettings().advancedOpen) { // Inhalt von "Erweitert" nur bei aufgeklapptem Bereich
            const float lx = l.advCard.X, rx = l.advCard.X + 400 * l.s, cw = 388 * l.s;
            if (In(rowOf(l.musicModeToggle, lx, cw), x, y)) return {Hit::MusicModeToggle, -1};
            if (In(l.sliderGate, x, y)) return {Hit::SliderGate, -1};
            if (In(rowOf(l.optimizeToggle, rx, cw), x, y)) return {Hit::OptimizeToggle, -1};
            if (In(rowOf(l.opusToggle, rx, cw), x, y)) return {Hit::OpusToggle, -1};
        }
    if (In(l.settingsPanel, x, y)) return {Hit::SettingsPanel, -1};
    return {Hit::SettingsBackdrop, -1};
}

HitInfo HitTest(Ctx* c, int x, int y)
{
    const Layout& l = c->lay;
    if ((c->page == 0 || c->page == 3) && c->menu.open) { // Rechtsklick-Menue liegt ueber allem
        for (size_t i = 0; i < c->menu.items.size(); ++i)
            if (In(MenuItemRect(l, c->menu, i), x, y)) return {Hit::MenuItem, static_cast<int>(i)};
        if (In(MenuRect(l, c->menu), x, y)) return {Hit::MenuBody, -1};
    }
    if (In(l.gearBtn, x, y)) return {Hit::SettingsGear, -1};
    if (c->settingsOpen) return HitSettings(c, x, y); // das Popup faengt alle Klicks ab
    for (int i = 0; i < 4; ++i)
        if (In(l.tab[i], x, y)) return {i == 0 ? Hit::TabMix : (i == 1 ? Hit::TabEq : (i == 2 ? Hit::TabFiles : Hit::TabSounds)), i};

    if (c->page == 0) {
        if (In(l.profPrev, x, y)) return {Hit::ProfPrev, -1};
        if (In(l.profNext, x, y)) return {Hit::ProfNext, -1};
        if (In(l.profField, x, y)) return {Hit::ProfField, -1};
        if (In(l.profSave, x, y)) return {Hit::ProfSave, -1};
        if (In(l.profRename, x, y)) return {Hit::ProfRename, -1};
        if (!(c->edit.active && c->edit.kind == 1) && In(l.profDelete, x, y)) return {Hit::ProfDelete, -1};
        if (In(l.mainToggleRow, x, y)) return {Hit::MainToggle, -1};
        if (In(l.micMute, x, y)) return {Hit::MicMute, -1};
        if (In(l.sliderMusic, x, y)) return {Hit::SliderMusic, -1};
        if (In(l.sliderMic, x, y)) return {Hit::SliderMic, -1};
        if (In(l.sliderDelay, x, y)) return {Hit::SliderDelay, -1};
        if (In(l.monitorRow, x, y)) return {Hit::MonitorToggle, -1};
        if (In(l.gridArea, x, y)) {
            for (size_t i = 0; i < c->tiles.size(); ++i)
                if (In(TileRect(c, c->tiles[i]), x, y)) return {Hit::Tile, static_cast<int>(i)};
            return {Hit::Grid, -1};
        }
    } else if (c->page == 2) {
        if (In(l.btnPrev, x, y)) return {Hit::FilePrev, -1};
        if (In(l.btnPlay, x, y)) return {Hit::FilePlay, -1};
        if (In(l.btnStop, x, y)) return {Hit::FileStop, -1};
        if (In(l.btnNext, x, y)) return {Hit::FileNext, -1};
        if (In(l.btnRepeat, x, y)) return {Hit::FileRepeat, -1};
        if (In(l.btnClear, x, y)) return {Hit::FileClear, -1};
        if (In(l.btnOpen, x, y)) return {Hit::FileOpen, -1};
        if (In(l.fileSeek, x, y)) return {Hit::FileSeek, -1};
        if (In(l.fileArea, x, y)) {
            const int count = static_cast<int>(Engine::Get().GetPlayer().items.size());
            const int i     = static_cast<int>((y - l.fileArea.Y + c->fileScroll) / (40 * l.s));
            if (i >= 0 && i < count) return {x >= l.fileArea.X + l.fileArea.Width - 44 * l.s ? Hit::FileRemove : Hit::FileRow, i};
        }
    } else if (c->page == 3) {
        if (c->edit.active && c->edit.kind == 2) {
            if (In(l.soundEditOk, x, y)) return {Hit::SoundEditOk, -1};
            if (In(l.soundEditCancel, x, y)) return {Hit::SoundEditCancel, -1};
        }
        if (In(l.soundVol, x, y)) return {Hit::SoundVol, -1};
        if (In(l.soundStop, x, y)) return {Hit::SoundStop, -1};
        for (int i = 0; i < kSoundSlots; ++i)
            if (In(l.soundTile[i], x, y)) return {Hit::SoundTile, i};
    } else {
        for (int i = 0; i < 3; ++i)
            if (In(l.bandBtn[i], x, y)) return {Hit::EqBandsBtn, i};
        if (In(l.eqToggle, x, y)) return {Hit::EqToggle, -1};
        if (c->presetOpen) { // Dropdown-Liste liegt ueber allem
            const Settings st   = Engine::Get().GetSettings();
            const auto     rows = PopupRows(st);
            const RectF    pr   = PopupRect(l, rows);
            if (In(pr, x, y)) {
                for (size_t i = 0; i < rows.size(); ++i)
                    if (rows[i].type == 1 && In(PopupRowRect(l, rows, i, c->presetScroll), x, y)) return {Hit::PresetItem, rows[i].id};
                return {Hit::PresetPopup, -1};
            }
        }
        if (In(l.presetField, x, y)) return {Hit::PresetField, -1};
        if (In(l.presetSave, x, y)) return {Hit::PresetSave, -1};
        if (In(l.presetRename, x, y)) return {Hit::PresetRename, -1};
        if (!c->edit.active && In(l.presetDelete, x, y)) return {Hit::PresetDelete, -1};
        if (In(l.listenToggle, x, y)) return {Hit::ListenToggle, -1};
        if (In(l.listenBtn, x, y)) return {Hit::Listen, -1};
        if (x >= l.plot.X && x < l.plot.X + l.plot.Width && y >= l.plot.Y - 4 * l.s && y < l.plot.Y + l.plot.Height + 4 * l.s)
            return {Hit::EqPlot, PlotBand(l, Engine::Get().GetSettings().eqBands, x)};
    }
    return {};
}

// Wert 0..maxV aus der Mausposition; Lautstaerke (max 200) rastet bei 100 % ein, sonst auf `step` gerundet.
int SliderValue(const RectF& r, int x, int maxV, int step = 10)
{
    const float pad = 8 * (r.Height / 22.0f);
    float       t   = (x - (r.X + pad)) / (r.Width - 2 * pad);
    int         v   = static_cast<int>(std::lround(std::clamp(t, 0.0f, 1.0f) * maxV));
    if (maxV == 200) return std::abs(v - 100) <= 3 ? 100 : v;
    return (v + step / 2) / step * step;
}

// ---------------------------------------------------------------- Zeichnen ---

void DrawToggle(Graphics& g, const RectF& r, float anim)
{
    const float rad = r.Height / 2;
    Color       off = C(col::track), on = C(col::accent);
    Color       mix(255, static_cast<BYTE>(off.GetR() + (on.GetR() - off.GetR()) * anim), static_cast<BYTE>(off.GetG() + (on.GetG() - off.GetG()) * anim),
                    static_cast<BYTE>(off.GetB() + (on.GetB() - off.GetB()) * anim));
    FillRound(g, r, rad, mix);
    const float knobR = rad - 3 * (r.Height / 26.0f);
    const float cx    = r.X + rad + (r.Width - 2 * rad) * anim;
    FillCircle(g, cx, r.Y + rad + 1, knobR, C(0x000000, 60)); // weicher Schatten
    FillCircle(g, cx, r.Y + rad, knobR, C(0xFFFFFF));
}

void DrawSlider(Graphics& g, const RectF& r, float frac, bool active, bool markMiddle, bool dim = false)
{
    const float u   = r.Height / 22.0f;
    const float pad = 8 * u, h = 7 * u, cy = r.Y + r.Height / 2;
    RectF       track(r.X + pad, cy - h / 2, r.Width - 2 * pad, h);
    FillRound(g, track, h / 2, C(col::track));
    const float x = track.X + track.Width * frac;
    RectF       fill(track.X, track.Y, x - track.X, h);
    if (fill.Width > 0) {
        if (dim) {
            FillRound(g, fill, h / 2, C(col::sub));
        } else {
            GraphicsPath gp;
            RoundPath(gp, fill, h / 2);
            LinearGradientBrush gb(PointF(fill.X - 0.5f, 0), PointF(fill.X + std::max(fill.Width, 1.0f), 0), C(col::accent), C(0x7DB2FF));
            g.FillPath(&gb, &gp);
        }
    }
    if (markMiddle) { // 100 %
        const float mark = track.X + track.Width * 0.5f;
        Pen         tick(C(col::sub, 120), std::max(1.0f, u));
        g.DrawLine(&tick, mark, cy - 8 * u, mark, cy - 5 * u);
    }
    const float kr = (active ? 9.0f : 8.0f) * u;
    FillCircle(g, x, cy + 1, kr, C(0x000000, 70));
    FillCircle(g, x, cy, kr, C(0xFFFFFF));
    Pen ring(dim ? C(col::sub, 190) : C(col::accent, active ? 255 : 200), std::max(1.6f, 2.2f * u));
    g.DrawEllipse(&ring, x - kr + 1.2f * u, cy - kr + 1.2f * u, 2 * (kr - 1.2f * u), 2 * (kr - 1.2f * u));
}

void DrawMeter(Graphics& g, const RectF& r, float level, float hold)
{
    FillRound(g, r, r.Height / 2, C(col::track));
    if (level > 0.003f) {
        RectF               fill(r.X, r.Y, std::max(r.Height, r.Width * level), r.Height);
        LinearGradientBrush br(PointF(r.X, 0), PointF(r.X + r.Width, 0), C(col::ok), C(col::bad));
        const Color         cols[3] = {C(col::ok), C(col::warn), C(col::bad)};
        const REAL          pos[3]  = {0.0f, 0.75f, 1.0f};
        br.SetInterpolationColors(cols, pos, 3);
        GraphicsPath p;
        RoundPath(p, fill, r.Height / 2);
        g.FillPath(&br, &p);
    }
    if (hold > 0.02f) {
        Pen   pen(C(col::text, 200), std::max(1.5f, r.Height / 6));
        float x = r.X + r.Width * hold;
        g.DrawLine(&pen, x, r.Y, x, r.Y + r.Height);
    }
}

// Knopf: Segment/Pille. selected = Akzentfarbe, hover = heller.
void DrawButton(Graphics& g, const RectF& r, const std::wstring& label, const Font& f, bool selected, bool hover, float s, uint32_t selColor = col::accent, bool enabled = true)
{
    if (!enabled) {
        FillRound(g, r, r.Height / 2, C(col::track, 60));
        Text(g, label, f, r, C(col::sub, 120), StringAlignmentCenter);
        return;
    }
    if (selected) FillRound(g, r, r.Height / 2, C(selColor));
    else FillRound(g, r, r.Height / 2, hover ? C(col::cardHover) : C(col::track, 140));
    Text(g, label, f, r, selected ? C(0xFFFFFF) : C(col::text), StringAlignmentCenter);
}

// ---- Anzeige-Uebersetzung fuer Texte, die die Engine/das Plugin auf Deutsch liefert (die Oberflaeche wertet sie deutsch aus) -------------------------------
std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to)
{
    if (from.empty()) return s;
    for (size_t p = 0; (p = s.find(from, p)) != std::wstring::npos; p += to.size()) s.replace(p, from.size(), to);
    return s;
}

bool StartsWith(const std::wstring& s, const wchar_t* prefix) { return s.rfind(prefix, 0) == 0; }

// Status der Engine: "Aus", "Aktiv: spotify.exe (PID 12)", "Warte auf X (App starten ...)", "X: Capture-Start fehlgeschlagen (0x...)"
std::wstring TrStatus(const std::wstring& st)
{
    if (i18n::Current() == i18n::Lang::German) return st;
    const std::wstring whole = Tr(st);
    if (whole != st) return whole;
    if (StartsWith(st, L"Aktiv: ")) return std::wstring(Tr(L"Aktiv: ")) + st.substr(7);
    if (StartsWith(st, L"Warte auf ")) {
        const std::wstring tail = L" (App starten und Ton abspielen)";
        if (st.size() > 10 + tail.size() && st.compare(st.size() - tail.size(), tail.size(), tail) == 0) {
            std::wstring pattern = Tr(L"Warte auf %s (App starten und Ton abspielen)");
            const size_t pos     = pattern.find(L"%s");
            if (pos != std::wstring::npos) pattern.replace(pos, 2, st.substr(10, st.size() - 10 - tail.size()));
            return pattern;
        }
    }
    return ReplaceAll(st, L"Capture-Start fehlgeschlagen", Tr(L"Capture-Start fehlgeschlagen"));
}

// Diagnosezeile: "Puffer 20-30 ms  |  Aussetzer 0  |  Takt +0.00 %  |  TS3 sendet 100 %" bzw. "Keine Aufnahme aktiv"
std::wstring TrDiag(const std::wstring& d)
{
    if (i18n::Current() == i18n::Lang::German) return d;
    const std::wstring whole = Tr(d);
    if (whole != d) return whole;
    std::wstring out = ReplaceAll(d, L"Puffer ", Tr(L"Puffer "));
    out = ReplaceAll(out, L"Aussetzer", Tr(L"Aussetzer"));
    out = ReplaceAll(out, L"Takt", Tr(L"Takt"));
    return ReplaceAll(out, L"TS3 sendet", Tr(L"TS3 sendet"));
}

std::wstring TrCodec(const std::wstring& c) { return ReplaceAll(c, L", Qualität ", Tr(L", Qualität ")); }
std::wstring TrOpus(const std::wstring& o) { return ReplaceAll(o, L" (geschätzt)", Tr(L" (geschätzt)")); }
std::wstring TrMsg(const std::wstring& m) { return StartsWith(m, L"Nicht abspielbar: ") ? std::wstring(Tr(L"Nicht abspielbar: ")) + m.substr(18) : m; }

// Namenseingabe (Text mit blinkendem Cursor, optional komplett markiert) im Feld f
void DrawEditField(Graphics& g, Ctx* c, const RectF& f, float s)
{
    FillRound(g, f, 8 * s, C(col::bg));
    StrokeRound(g, f, 8 * s, C(col::accent, 230), 1.5f);
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetFormatFlags(fmt.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
    const float         tx = f.X + 12 * s, ty = f.Y + (f.Height - 16 * s) / 2;
    const std::wstring& t  = c->edit.text;
    RectF               box(0, 0, 0, 0);
    g.MeasureString(t.c_str(), static_cast<INT>(t.size()), c->fBody.get(), PointF(tx, ty), &fmt, &box);
    if (c->edit.selectAll && !t.empty()) FillRound(g, RectF(tx - 2 * s, ty - 1 * s, box.Width + 4 * s, 18 * s), 3 * s, C(col::accent, 120));
    if (t.empty()) {
        SolidBrush         b(C(col::sub));
        const std::wstring ph = Tr(L"Name eingeben …");
        g.DrawString(ph.c_str(), -1, c->fBody.get(), PointF(tx, ty), &fmt, &b);
    } else {
        SolidBrush b(C(col::text));
        g.DrawString(t.c_str(), static_cast<INT>(t.size()), c->fBody.get(), PointF(tx, ty), &fmt, &b);
    }
    if ((c->tick / 14) % 2 == 0) {
        Pen caret(C(col::text), std::max(1.2f, s));
        g.DrawLine(&caret, tx + box.Width + 1 * s, ty, tx + box.Width + 1 * s, ty + 16 * s);
    }
}

// Kleiner Pfeil-Knopf (links/rechts), z.B. zum Blaettern durch Profile
void DrawArrowBtn(Graphics& g, const RectF& r, bool left, bool hover, bool enabled, float s)
{
    FillRound(g, r, r.Height / 2, enabled ? (hover ? C(col::cardHover) : C(col::track, 140)) : C(col::track, 60));
    const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, d = left ? -1.0f : 1.0f;
    PointF      tri[3] = {PointF(cx + d * 4 * s, cy), PointF(cx - d * 3 * s, cy - 6 * s), PointF(cx - d * 3 * s, cy + 6 * s)};
    SolidBrush  b(enabled ? C(col::text) : C(col::sub, 120));
    g.FillPolygon(&b, tri, 3);
}

// Rundes Rechteck mit senkrechtem Farbverlauf (Karten: oben eine Spur heller)
void FillRoundGrad(Graphics& g, const RectF& r, float rad, const Color& top, const Color& bottom)
{
    if (r.Height < 2) return;
    GraphicsPath p;
    RoundPath(p, r, rad);
    LinearGradientBrush br(PointF(r.X, r.Y), PointF(r.X, r.Y + r.Height), top, bottom);
    g.FillPath(&br, &p);
}

// Kleine, selbst gezeichnete Symbole (24 x 24 Raster, Linienstil) fuer Seitenleiste und Kartentitel
enum class Icon { Source, Speaker, Headphones, Person, Eq, Wave, Note, Pads, Chat, Info, Sliders, Send, Gear };

void DrawIcon(Graphics& g, Icon ic, const RectF& r, const Color& col, float s)
{
    (void)s;
    const float u = r.Width / 24.0f, X = r.X, Y = r.Y;
    auto        P = [&](float x, float y) { return PointF(X + x * u, Y + y * u); };
    Pen         pen(col, std::max(1.4f, 1.8f * u));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    SolidBrush br(col);
    switch (ic) {
    case Icon::Source: { // Aktivitaets-Balken
        const float hs[4] = {8, 16, 11, 14};
        for (int i = 0; i < 4; ++i) FillRound(g, RectF(X + (3 + i * 5) * u, Y + (20 - hs[i]) * u, 3.2f * u, hs[i] * u), 1.6f * u, col);
        break;
    }
    case Icon::Speaker: {
        PointF poly[6] = {P(3, 9), P(7, 9), P(12, 5), P(12, 19), P(7, 15), P(3, 15)};
        g.FillPolygon(&br, poly, 6);
        g.DrawArc(&pen, X + 10 * u, Y + 8 * u, 7 * u, 8 * u, -55, 110);
        g.DrawArc(&pen, X + 8 * u, Y + 4 * u, 13 * u, 16 * u, -55, 110);
        break;
    }
    case Icon::Headphones:
        g.DrawArc(&pen, X + 4 * u, Y + 3 * u, 16 * u, 16 * u, 180, 180);
        FillRound(g, RectF(X + 3 * u, Y + 12 * u, 5 * u, 8 * u), 2 * u, col);
        FillRound(g, RectF(X + 16 * u, Y + 12 * u, 5 * u, 8 * u), 2 * u, col);
        break;
    case Icon::Person:
        g.DrawEllipse(&pen, X + 8 * u, Y + 3 * u, 8 * u, 8 * u);
        g.DrawArc(&pen, X + 4 * u, Y + 13 * u, 16 * u, 14 * u, 180, 180);
        break;
    case Icon::Eq: {
        const float xs[3] = {6, 12, 18}, ks[3] = {14, 7, 16};
        for (int i = 0; i < 3; ++i) {
            g.DrawLine(&pen, P(xs[i], 4), P(xs[i], 20));
            FillCircle(g, X + xs[i] * u, Y + ks[i] * u, 2.8f * u, col);
        }
        break;
    }
    case Icon::Wave: {
        PointF pts[9];
        const float hs[9] = {0, -4, 5, -7, 3, -6, 4, -3, 0};
        for (int i = 0; i < 9; ++i) pts[i] = P(2 + i * 2.5f, 12 + hs[i]);
        g.DrawCurve(&pen, pts, 9);
        break;
    }
    case Icon::Note:
        g.FillEllipse(&br, X + 4 * u, Y + 14 * u, 7 * u, 5.5f * u);
        g.DrawLine(&pen, P(10.5f, 16), P(10.5f, 4));
        g.DrawBezier(&pen, P(10.5f, 4), P(15, 5), P(18, 7), P(18, 12));
        break;
    case Icon::Pads:
        for (int i = 0; i < 4; ++i) StrokeRound(g, RectF(X + (3 + (i % 2) * 10) * u, Y + (3 + (i / 2) * 10) * u, 8 * u, 8 * u), 2.2f * u, col, std::max(1.4f, 1.8f * u));
        break;
    case Icon::Chat: {
        StrokeRound(g, RectF(X + 3 * u, Y + 4 * u, 18 * u, 12 * u), 3 * u, col, std::max(1.4f, 1.8f * u));
        PointF tail[3] = {P(8, 16), P(8, 21), P(13, 16)};
        g.DrawLines(&pen, tail, 3);
        break;
    }
    case Icon::Info:
        g.DrawEllipse(&pen, X + 3 * u, Y + 3 * u, 18 * u, 18 * u);
        g.DrawLine(&pen, P(12, 11), P(12, 17));
        FillCircle(g, X + 12 * u, Y + 7.5f * u, 1.4f * u, col);
        break;
    case Icon::Sliders: {
        const float ys[3] = {6, 12, 18}, ks[3] = {9, 15, 8};
        for (int i = 0; i < 3; ++i) {
            g.DrawLine(&pen, P(4, ys[i]), P(20, ys[i]));
            FillCircle(g, X + ks[i] * u, Y + ys[i] * u, 2.6f * u, col);
        }
        break;
    }
    case Icon::Send: {
        g.DrawLine(&pen, P(5, 19), P(19, 5));
        PointF head[3] = {P(10, 5), P(19, 5), P(19, 14)};
        g.DrawLines(&pen, head, 3);
        break;
    }
    case Icon::Gear:
        g.DrawEllipse(&pen, X + 8 * u, Y + 8 * u, 8 * u, 8 * u);
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) * 0.7853982f;
            g.DrawLine(&pen, P(12 + std::cos(a) * 6.2f, 12 + std::sin(a) * 6.2f), P(12 + std::cos(a) * 9.5f, 12 + std::sin(a) * 9.5f));
        }
        break;
    }
}

void Paint(Ctx* c, HDC dc, const RECT& rc)
{
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC       mem = CreateCompatibleDC(dc);
    HBITMAP   bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ   old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(C(col::bg));

        const Layout&      l  = c->lay;
        const float        s  = l.s;
        Engine&            en = Engine::Get();
        const Settings     st = en.GetSettings();
        const std::wstring status = en.Status();
        const HitInfo      hv = c->hover;

        auto card = [&](const RectF& r) { // flach mit Tiefe: feiner Verlauf, Lichtkante oben, Rand nur ein Hauch
            FillRoundGrad(g, r, 14 * s, C(0x21242C), C(col::card));
            StrokeRound(g, r, 14 * s, C(col::border, 60));
            Pen hl(C(0xFFFFFF, 12), 1.0f);
            g.DrawLine(&hl, r.X + 14 * s, r.Y + 1.0f, r.X + r.Width - 14 * s, r.Y + 1.0f);
        };
        auto pad = [&](const RectF& r, float x, float y, float ww, float hh) { return RectF(r.X + x * s, r.Y + y * s, ww * s, hh * s); };
        auto cardTitle = [&](const RectF& cardR, Icon ic, const std::wstring& text, float y) { // Kartentitel: kleines Akzent-Symbol + Text
            DrawIcon(g, ic, RectF(cardR.X + kPad * s, cardR.Y + (y + 3) * s, 20 * s, 20 * s), C(col::accent, 220), s);
            Text(g, text, *c->fHead, RectF(cardR.X + (kPad + 28) * s, cardR.Y + y * s, 300 * s, 26 * s), C(col::text));
        };
        auto labelRight = [&](const RectF& toggle, const std::wstring& text) {
            Text(g, text, *c->fSmall, RectF(toggle.X - 160 * s, toggle.Y, 150 * s, toggle.Height), C(col::sub), StringAlignmentFar);
        };

        // --- Seitenleiste: Logo und Navigation (Symbole mit kurzem Namen) -------------------------------
        {
            SolidBrush side(C(col::card));
            g.FillRectangle(&side, 0.0f, 0.0f, kSide * s, static_cast<float>(h));
            Pen sep(C(col::border, 70), 1.0f);
            g.DrawLine(&sep, kSide * s, 0.0f, kSide * s, static_cast<float>(h));
            if (c->appIcon) { // APP-ICON.png (hat transparenten Rand)
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.DrawImage(c->appIcon.get(), l.logo);
            } else {
                FillRound(g, l.logo, 10 * s, C(col::accent));
            }
            const wchar_t* labels[4] = {Tr(L"Quelle"), Tr(L"EQ"), Tr(L"Dateien"), Tr(L"Sounds")};
            const Icon     icons[4]  = {Icon::Source, Icon::Eq, Icon::Note, Icon::Pads};
            const RectF    mark(l.tab[0].X, l.tab[0].Y + c->navPos * 76 * s, l.tab[0].Width, l.tab[0].Height); // gleitende Markierung
            FillRound(g, mark, 14 * s, C(col::accent, 40));
            FillRound(g, RectF(0, mark.Y + 16 * s, 4 * s, mark.Height - 32 * s), 2 * s, C(col::accent));
            for (int i = 0; i < 4; ++i) {
                const Hit   tabHit = i == 0 ? Hit::TabMix : (i == 1 ? Hit::TabEq : (i == 2 ? Hit::TabFiles : Hit::TabSounds));
                const bool  active = c->page == i, hover = hv.hit == tabHit;
                const RectF r      = l.tab[i];
                if (hover && !active) FillRound(g, r, 14 * s, C(0xFFFFFF, 10));
                const Color color = active ? C(col::accent) : (hover ? C(col::text) : C(col::sub));
                DrawIcon(g, icons[i], RectF(r.X + (r.Width - 26 * s) / 2, r.Y + 10 * s, 26 * s, 26 * s), color, s);
                Text(g, labels[i], *c->fTiny, RectF(r.X, r.Y + 42 * s, r.Width, 18 * s), color, StringAlignmentCenter);
            }
        }

        // --- Kopfleiste: Seitentitel, Zahnrad (Einstellungs-Popup), Status-Schild -----------------------
        {
            const wchar_t* pageNames[4] = {Tr(L"Quelle & Mix"), Tr(L"Equalizer"), Tr(L"Dateien"), Tr(L"Sounds")};
            Text(g, pageNames[c->page], *c->fTitle, RectF(l.header.X + 4 * s, l.header.Y, 420 * s, l.header.Height), C(col::text));
            std::wstring label;
            uint32_t     color;
            if (status.rfind(L"Aktiv", 0) == 0) { label = Tr(L"AKTIV"); color = col::ok; }
            else if (status.find(L"fehlgeschlagen") != std::wstring::npos) { label = Tr(L"FEHLER"); color = col::bad; }
            else if (!st.enabled) { label = Tr(L"AUS"); color = col::sub; }
            else { label = Tr(L"WARTET"); color = col::warn; }
            RectF pill(l.header.X + l.header.Width - 14 * s - 92 * s, l.header.Y + 12 * s, 92 * s, 24 * s);
            FillRound(g, pill, 12 * s, C(color, 38));
            FillCircle(g, pill.X + 14 * s, pill.Y + 12 * s, 4 * s, C(color));
            Text(g, label, *c->fSmall, RectF(pill.X + 24 * s, pill.Y, pill.Width - 24 * s, pill.Height), C(color));
            const RectF& gb  = l.gearBtn;
            const bool   hot = hv.hit == Hit::SettingsGear, open = c->settingsOpen;
            if (open) FillRound(g, gb, 16 * s, C(col::accent, 60));
            else if (hot) FillRound(g, gb, 16 * s, C(col::cardHover));
            DrawIcon(g, Icon::Gear, RectF(gb.X + 5 * s, gb.Y + 5 * s, 22 * s, 22 * s), open ? C(col::accent) : (hot ? C(col::text) : C(col::sub)), s);
        }

        // Seitenwechsel: Inhalt gleitet von unten ein und blendet auf (Ausblendflaeche in Hintergrundfarbe)
        const float pageE = 1.0f - std::pow(1.0f - std::min(1.0f, c->pageT), 3.0f);
        if (pageE < 1.0f) g.TranslateTransform(0.0f, (1.0f - pageE) * 16 * s);

        if (c->page == 0) {
            // --- Audio-Quelle: Titelleiste mit Status und Hauptschalter "Musik einspielen" -----------------
            card(l.gridCard);
            cardTitle(l.gridCard, Icon::Source, Tr(L"Audio-Quelle"), 10);
            {
                const bool problem = st.enabled && st.musicMode && en.MusicModeEngaged() && en.SendShare() < 0.6f; // Musikmodus aktiv, TS3 sendet aber nicht
                const RectF sr     = pad(l.gridCard, kPad + 150, 10, 380, 28);
                if (problem) Text(g, Tr(L"TS3 sendet die Musik nicht – Hilfe: Einstellungen → Erweitert"), *c->fSmall, sr, C(col::warn));
                else Text(g, TrStatus(status), *c->fSmall, sr, C(col::sub));
            }
            if (hv.hit == Hit::MainToggle) FillRound(g, l.mainToggleRow, 8 * s, C(0xFFFFFF, 8));
            Text(g, Tr(L"Musik einspielen"), *c->fBody, RectF(l.mainToggleRow.X + 8 * s, l.mainToggleRow.Y, l.mainToggleRow.Width - 66 * s, l.mainToggleRow.Height), C(col::text),
                 StringAlignmentFar);
            DrawToggle(g, l.mainToggle, c->anim[A_MAIN]);
            g.SetClip(l.gridArea);
            if (c->tiles.empty())
                Text(g, c->haveApps ? Tr(L"Keine App mit Audio gefunden.\nStarte Spotify oder spiele ein Video ab.") : Tr(L"Suche Apps ..."), *c->fBody, l.gridArea, C(col::sub),
                     StringAlignmentCenter);
            const double animTime = c->tick * (kTimerMs / 1000.0);
            for (size_t i = 0; i < c->tiles.size(); ++i) {
                const TileState& ts = c->tiles[i];
                const AppInfo&   a  = ts.info;
                const RectF      t  = TileRect(c, ts);
                if (t.Y + t.Height < l.gridArea.Y || t.Y > l.gridArea.Y + l.gridArea.Height) continue;
                const bool selected = a.exe == st.targetExe;
                const bool hover    = hv.hit == Hit::Tile && hv.idx == static_cast<int>(i);
                if (selected) StrokeRound(g, RectF(t.X - 2 * s, t.Y - 2 * s, t.Width + 4 * s, t.Height + 4 * s), 14 * s, C(col::accent, 50), 3.0f); // Glimmen
                FillRound(g, t, 12 * s, selected ? C(col::accent, 46) : (hover ? C(col::cardHover) : C(0xFFFFFF, 10)));
                StrokeRound(g, t, 12 * s, selected ? C(col::accent, 170) : C(col::border, 60));
                if (a.path.empty()) { // kein Icon: Buchstaben-Platzhalter
                    FillCircle(g, t.X + t.Width / 2, t.Y + 28 * s, 16 * s, C(col::track));
                    Text(g, PrettyExeName(a.exe).substr(0, 1), *c->fHead, RectF(t.X, t.Y + 12 * s, t.Width, 32 * s), C(col::text), StringAlignmentCenter);
                }
                Text(g, PrettyExeName(a.exe), *c->fBody, RectF(t.X + 6 * s, t.Y + 50 * s, t.Width - 12 * s, 22 * s), C(col::text), StringAlignmentCenter);
                Text(g, a.pid == 0 ? Tr(L"nicht gestartet") : (a.title.empty() ? a.exe : a.title), *c->fTiny, RectF(t.X + 6 * s, t.Y + 72 * s, t.Width - 12 * s, 20 * s),
                     C(col::sub), StringAlignmentCenter);
                if (ts.amp > 0.02f) DrawActivityBars(g, t.X + t.Width - 28 * s, t.Y + 24 * s, s, ts.amp, animTime, ts.phase); // statt des gruenen Punkts
            }
            g.ResetClip();
            if (MaxScroll(c) > 0) { // schmaler Scrollbalken
                const float view = l.gridArea.Height, content = view + MaxScroll(c);
                const float th = std::max(24 * s, view * view / content);
                const float ty = l.gridArea.Y + (view - th) * (c->scroll / MaxScroll(c));
                FillRound(g, RectF(l.gridArea.X + l.gridArea.Width - 4 * s, ty, 3 * s, th), 1.5f * s, C(col::sub, 120));
            }

            // --- Pegel + Lautstaerke -----------------------------------------------------
            card(l.mixCard);
            cardTitle(l.mixCard, Icon::Speaker, Tr(L"Lautstärke"), 10);
            DrawMeter(g, l.meterL, c->dispL, c->holdL);
            DrawMeter(g, l.meterR, c->dispR, c->holdR);
            Text(g, L"L", *c->fSmall, RectF(l.meterL.X - 14 * s, l.meterL.Y - 3 * s, 12 * s, l.meterL.Height + 6 * s), C(col::sub));
            Text(g, L"R", *c->fSmall, RectF(l.meterR.X - 14 * s, l.meterR.Y - 3 * s, 12 * s, l.meterR.Height + 6 * s), C(col::sub));
            auto sliderRow = [&](const wchar_t* label, const RectF& area, const std::wstring& value, float frac, bool active, bool mark, bool dim, bool dimText) {
                Text(g, label, *c->fBody, RectF(area.X, area.Y - 24 * s, 250 * s, 22 * s), C(dimText ? col::sub : col::text));
                Text(g, value, *c->fBody, RectF(area.X + area.Width - 80 * s, area.Y - 24 * s, 80 * s, 22 * s), C(col::sub), StringAlignmentFar);
                DrawSlider(g, area, frac, active, mark, dim);
            };
            sliderRow(Tr(L"Musik-Lautstärke"), l.sliderMusic, std::to_wstring(st.musicVolPct) + L" %", st.musicVolPct / 200.0f,
                      c->drag.hit == Hit::SliderMusic || hv.hit == Hit::SliderMusic, true, false, false);
            const bool muted = en.IsMicMuted();
            sliderRow(Tr(L"Mikrofon-Lautstärke"), l.sliderMic, muted ? std::wstring(Tr(L"stumm")) : std::to_wstring(st.micVolPct) + L" %", st.micVolPct / 200.0f,
                      c->drag.hit == Hit::SliderMic || hv.hit == Hit::SliderMic, true, muted, muted);
            {   // Stumm-Knopf: Mikrofon-Symbol, bei "stumm" rot und durchgestrichen
                const RectF& b   = l.micMute;
                const bool   hov = hv.hit == Hit::MicMute;
                FillRound(g, b, 7 * s, muted ? C(col::bad, 70) : (hov ? C(col::cardHover) : C(col::track, 140)));
                const Color ic = muted ? C(col::bad) : C(col::text);
                const float cx = b.X + b.Width / 2, cy = b.Y + b.Height / 2;
                FillRound(g, RectF(cx - 3 * s, cy - 8 * s, 6 * s, 10 * s), 3 * s, ic);
                Pen pen(ic, std::max(1.3f, 1.4f * s));
                g.DrawArc(&pen, cx - 5.5f * s, cy - 6.5f * s, 11 * s, 11 * s, 10, 160);
                g.DrawLine(&pen, cx, cy + 5 * s, cx, cy + 8 * s);
                g.DrawLine(&pen, cx - 3 * s, cy + 8 * s, cx + 3 * s, cy + 8 * s);
                if (muted) {
                    Pen slash(C(col::bad), std::max(2.0f, 2.2f * s));
                    g.DrawLine(&slash, cx - 9 * s, cy - 9 * s, cx + 9 * s, cy + 9 * s);
                }
            }

            // --- Mithoeren ----------------------------------------------------------------
            card(l.monitorCard);
            if (hv.hit == Hit::MonitorToggle) FillRound(g, l.monitorRow, 10 * s, C(0xFFFFFF, 8));
            cardTitle(l.monitorCard, Icon::Headphones, Tr(L"Mithören"), 10);
            Text(g, st.monitor ? Tr(L"Du hörst die Musik verzögert in TeamSpeak") : Tr(L"Aus – prüfe, was die anderen hören"), *c->fSmall, pad(l.monitorCard, kPad, 34, 270, 18),
                 C(col::sub));
            DrawToggle(g, l.monitorToggle, c->anim[A_MONITOR]);
            sliderRow(Tr(L"Verzögerung"), l.sliderDelay, std::to_wstring(st.monitorDelayMs) + L" ms", st.monitorDelayMs / static_cast<float>(kMaxDelayMs),
                      c->drag.hit == Hit::SliderDelay || hv.hit == Hit::SliderDelay, false, !st.monitor, !st.monitor);

            // --- Profile: benannte Schnappschuesse der Einstellungen ---------------------------------
            {
                card(l.profCard);
                cardTitle(l.profCard, Icon::Person, Tr(L"Profile"), 10);
                const bool     editing  = c->edit.active && c->edit.kind == 1;
                const Profile* curP     = st.profiles.Find(st.profileName);
                const bool     modified = curP && !SameProfile(*curP, ProfileFromSettings(st));
                const bool     have     = !st.profiles.items.empty();
                DrawArrowBtn(g, l.profPrev, true, hv.hit == Hit::ProfPrev, have && !editing, s);
                DrawArrowBtn(g, l.profNext, false, hv.hit == Hit::ProfNext, have && !editing, s);
                const RectF& f = l.profField;
                if (editing) {
                    DrawEditField(g, c, f, s);
                } else {
                    FillRound(g, f, 8 * s, C(col::track, 140));
                    const std::wstring label = curP ? curP->name : std::wstring(Tr(L"Kein Profil gewählt"));
                    Text(g, label, *c->fBody, RectF(f.X + 12 * s, f.Y, f.Width - 24 * s, f.Height), curP ? C(col::text) : C(col::sub));
                    if (modified) {
                        StringFormat fmt(StringFormat::GenericTypographic());
                        RectF        m(0, 0, 0, 0);
                        g.MeasureString(label.c_str(), static_cast<INT>(label.size()), c->fBody.get(), PointF(0, 0), &fmt, &m);
                        const float x = f.X + 12 * s + std::min(m.Width + 8 * s, f.Width - 110 * s);
                        Text(g, Tr(L"(geändert)"), *c->fTiny, RectF(x, f.Y, 90 * s, f.Height), C(col::warn));
                    }
                }
                if (!editing) {
                    DrawButton(g, l.profSave, Tr(L"Speichern als …"), *c->fSmall, false, hv.hit == Hit::ProfSave, s);
                    DrawButton(g, l.profRename, Tr(L"Umbenennen"), *c->fSmall, false, hv.hit == Hit::ProfRename, s, col::accent, curP != nullptr);
                    const bool armed = c->deleteArm > 0;
                    DrawButton(g, l.profDelete, armed ? Tr(L"Wirklich löschen?") : Tr(L"Löschen"), *c->fSmall, armed, hv.hit == Hit::ProfDelete, s, col::bad, curP != nullptr);
                } else {
                    DrawButton(g, l.profSave, c->edit.rename ? Tr(L"Umbenennen") : Tr(L"Speichern"), *c->fSmall, true, hv.hit == Hit::ProfSave, s, col::ok);
                    DrawButton(g, l.profRename, Tr(L"Abbrechen"), *c->fSmall, false, hv.hit == Hit::ProfRename, s);
                }
            }
        } else if (c->page == 1) {
            const int   N     = st.eqBands;
            const float colW  = l.plot.Width / N;
            const auto  freqs = eq::CenterFrequencies(N);
            auto        yOf   = [&](float db) { return l.plot.Y + (eq::kMaxDb - db) / (2 * eq::kMaxDb) * l.plot.Height; };
            const float on    = st.eqEnabled ? 1.0f : 0.35f;
            const BYTE  A     = static_cast<BYTE>(255 * on);

            // --- Equalizer-Karte -----------------------------------------------------------
            card(l.eqCard);
            cardTitle(l.eqCard, Icon::Eq, Tr(L"Equalizer"), 10);
            for (int i = 0; i < 3; ++i) {
                const int counts[3] = {10, 31, 61};
                DrawButton(g, l.bandBtn[i], std::to_wstring(counts[i]), *c->fSmall, st.eqBands == counts[i], hv.hit == Hit::EqBandsBtn && hv.idx == i, s);
            }
            Text(g, Tr(L"Bänder"), *c->fSmall, RectF(l.bandBtn[2].X + l.bandBtn[2].Width + 8 * s, l.bandBtn[2].Y, 60 * s, l.bandBtn[2].Height), C(col::sub));
            labelRight(l.eqToggle, Tr(L"EQ aktiv"));
            DrawToggle(g, l.eqToggle, c->anim[A_EQ]);

            // Preset-Zeile: Dropdown (mitgelieferte + eigene Filter), Speichern als / Umbenennen / Loeschen, oder Namenseingabe
            const CurPreset cur = CurrentPreset(st);
            {
                const RectF& f       = l.presetField;
                const bool   editing = c->edit.active, open = c->presetOpen;
                FillRound(g, f, 8 * s, editing ? C(col::bg) : ((hv.hit == Hit::PresetField || open) ? C(col::cardHover) : C(col::track, 140)));
                if (editing) StrokeRound(g, f, 8 * s, C(col::accent, 230), 1.5f);
                if (editing) {
                    StringFormat fmt(StringFormat::GenericTypographic());
                    fmt.SetFormatFlags(fmt.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
                    const float         tx = f.X + 12 * s, ty = f.Y + (f.Height - 16 * s) / 2;
                    const std::wstring& t = c->edit.text;
                    RectF               box(0, 0, 0, 0);
                    g.MeasureString(t.c_str(), static_cast<INT>(t.size()), c->fBody.get(), PointF(tx, ty), &fmt, &box);
                    if (c->edit.selectAll && !t.empty()) FillRound(g, RectF(tx - 2 * s, ty - 1 * s, box.Width + 4 * s, 18 * s), 3 * s, C(col::accent, 120));
                    if (t.empty()) {
                        SolidBrush b(C(col::sub));
                        g.DrawString(Tr(L"Name eingeben …"), -1, c->fBody.get(), PointF(tx, ty), &fmt, &b);
                    } else {
                        SolidBrush b(C(col::text));
                        g.DrawString(t.c_str(), static_cast<INT>(t.size()), c->fBody.get(), PointF(tx, ty), &fmt, &b);
                    }
                    if ((c->tick / 14) % 2 == 0) { // blinkender Cursor
                        Pen caret(C(col::text), std::max(1.2f, s));
                        g.DrawLine(&caret, tx + box.Width + 1 * s, ty, tx + box.Width + 1 * s, ty + 16 * s);
                    }
                } else {
                    // kleines Equalizer-Symbol, Name, Pfeil
                    SolidBrush  bar(C(col::accent));
                    const float ix = f.X + 12 * s, iy = f.Y + f.Height / 2;
                    const float hs[3] = {7, 12, 9};
                    for (int i = 0; i < 3; ++i) g.FillRectangle(&bar, ix + i * 5 * s, iy + 6 * s - hs[i] * s, 3 * s, hs[i] * s);
                    const std::wstring label = cur.user ? cur.label : Tr(cur.label);
                    Text(g, label, *c->fBody, RectF(f.X + 38 * s, f.Y, f.Width - 70 * s, f.Height), C(col::text));
                    if (cur.modified) {
                        StringFormat fmt(StringFormat::GenericTypographic());
                        RectF        m(0, 0, 0, 0);
                        g.MeasureString(label.c_str(), static_cast<INT>(label.size()), c->fBody.get(), PointF(0, 0), &fmt, &m);
                        const float x = f.X + 38 * s + std::min(m.Width + 8 * s, f.Width - 150 * s);
                        Text(g, Tr(L"(geändert)"), *c->fTiny, RectF(x, f.Y, 80 * s, f.Height), C(col::warn));
                    }
                    if (cur.user) FillCircle(g, f.X + f.Width - 44 * s, f.Y + f.Height / 2, 3 * s, C(col::accent)); // Markierung: eigener Filter
                    PointF tri[3] = {PointF(f.X + f.Width - 26 * s, f.Y + 11 * s), PointF(f.X + f.Width - 14 * s, f.Y + 11 * s), PointF(f.X + f.Width - 20 * s, f.Y + 18 * s)};
                    SolidBrush arrow(C(col::sub));
                    g.FillPolygon(&arrow, tri, 3);
                }
            }
            if (!c->edit.active) {
                DrawButton(g, l.presetSave, Tr(L"Speichern als …"), *c->fSmall, false, hv.hit == Hit::PresetSave, s);
                DrawButton(g, l.presetRename, Tr(L"Umbenennen"), *c->fSmall, false, hv.hit == Hit::PresetRename, s, col::accent, cur.user);
                const bool armed = c->deleteArm > 0;
                DrawButton(g, l.presetDelete, armed ? Tr(L"Wirklich löschen?") : Tr(L"Löschen"), *c->fSmall, armed, hv.hit == Hit::PresetDelete, s, col::bad, cur.user);
            } else {
                DrawButton(g, l.presetSave, c->edit.rename ? Tr(L"Umbenennen") : Tr(L"Speichern"), *c->fSmall, true, hv.hit == Hit::PresetSave, s, col::ok);
                DrawButton(g, l.presetRename, Tr(L"Abbrechen"), *c->fSmall, false, hv.hit == Hit::PresetRename, s);
            }

            // Plot: Hintergrund, Raster, dB-Beschriftung
            FillRound(g, RectF(l.plot.X - 4 * s, l.plot.Y - 6 * s, l.plot.Width + 8 * s, l.plot.Height + 12 * s), 8 * s, C(col::bg));
            StrokeRound(g, RectF(l.plot.X - 4 * s, l.plot.Y - 6 * s, l.plot.Width + 8 * s, l.plot.Height + 12 * s), 8 * s, C(col::border));
            for (int db = -12; db <= 12; db += 6) {
                Pen pen(db == 0 ? C(col::sub, 150) : C(col::border), std::max(1.0f, s * (db == 0 ? 1.2f : 1.0f)));
                g.DrawLine(&pen, l.plot.X, yOf(static_cast<float>(db)), l.plot.X + l.plot.Width, yOf(static_cast<float>(db)));
                wchar_t b[16];
                swprintf(b, 16, db == 0 ? L"0 dB" : L"%+d", db);
                Text(g, b, *c->fTiny, RectF(l.plot.X - 56 * s, yOf(static_cast<float>(db)) - 8 * s, 48 * s, 16 * s), C(col::sub), StringAlignmentFar);
            }

            // Live-Spektrum hinter den Reglern
            for (int i = 0; i < N && i < static_cast<int>(c->spec.size()); ++i) {
                const float v = c->spec[i];
                if (v < 0.01f) continue;
                const float bh = v * l.plot.Height;
                RectF       r(l.plot.X + i * colW + 0.5f, l.plot.Y + l.plot.Height - bh, std::max(1.0f, colW - 1.0f), bh);
                LinearGradientBrush br(PointF(0, r.Y + r.Height), PointF(0, l.plot.Y), C(col::ok, 90), C(col::accent, 130));
                const Color         cols[3] = {C(col::ok, 70), C(col::accent, 110), C(0xFF5A8A, 150)};
                const REAL          pos[3]  = {0.0f, 0.65f, 1.0f};
                br.SetInterpolationColors(cols, pos, 3);
                g.FillRectangle(&br, r);
            }

            // Regler: Balken von 0 dB zum Wert, Knopf, verbindende Kurve
            std::vector<PointF> pts;
            for (int i = 0; i < N; ++i) {
                const float gdb = i < static_cast<int>(st.eqGains.size()) ? st.eqGains[i] : 0.0f;
                const float cx = l.plot.X + (i + 0.5f) * colW, y0 = yOf(0), y1 = yOf(gdb);
                const bool  hot = (hv.hit == Hit::EqPlot && hv.idx == i);
                if (std::fabs(gdb) >= 0.25f) {
                    const uint32_t color = gdb > 0 ? col::accent : col::warn;
                    FillRound(g, RectF(cx - std::max(1.0f, colW * 0.5f - 1.0f), std::min(y0, y1), std::max(2.0f, colW - 2.0f), std::fabs(y1 - y0)), 1.5f * s, C(color, static_cast<BYTE>(150 * on)));
                }
                const float kw = std::max(4.0f * s, colW - 1.5f);
                FillRound(g, RectF(cx - kw / 2, y1 - 2.5f * s, kw, 5 * s), 2 * s, hot ? C(0xFFFFFF, 255) : C(0xE8EAF0, A));
                pts.push_back(PointF(cx, y1));
            }
            if (pts.size() > 1) {
                Pen curve(C(0xFFFFFF, static_cast<BYTE>(110 * on)), std::max(1.2f, 1.4f * s));
                g.DrawCurve(&curve, pts.data(), static_cast<INT>(pts.size()), 0.4f);
            }
            if (hv.hit == Hit::EqPlot && hv.idx >= 0 && hv.idx < N) { // Hover-Info
                const float cx = l.plot.X + (hv.idx + 0.5f) * colW;
                Pen         guide(C(col::text, 60), 1.0f);
                g.DrawLine(&guide, cx, l.plot.Y, cx, l.plot.Y + l.plot.Height);
                const float gdb = hv.idx < static_cast<int>(st.eqGains.size()) ? st.eqGains[hv.idx] : 0.0f;
                RectF       tip(l.plot.X + 8 * s, l.plot.Y + 4 * s, 170 * s, 20 * s);
                FillRound(g, tip, 6 * s, C(col::card, 230));
                Text(g, FormatFreq(freqs[hv.idx]) + L"   " + FormatDb(gdb), *c->fSmall, tip, C(col::text), StringAlignmentCenter);
            }
            // Frequenz-Beschriftung
            {
                const double marks[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
                for (double m : marks) {
                    int best = 0;
                    for (int i = 1; i < N; ++i)
                        if (std::fabs(std::log2(freqs[i] / m)) < std::fabs(std::log2(freqs[best] / m))) best = i;
                    if (std::fabs(std::log2(freqs[best] / m)) > 0.4) continue;
                    const float cx = l.plot.X + (best + 0.5f) * colW;
                    Text(g, m >= 1000 ? std::to_wstring(static_cast<int>(m / 1000)) + L"k" : std::to_wstring(static_cast<int>(m)), *c->fTiny,
                         RectF(cx - 24 * s, l.plot.Y + l.plot.Height + 8 * s, 48 * s, 16 * s), C(col::sub), StringAlignmentCenter);
                }
            }

            // Anhoeren-Knopf
            {
                const bool active = en.IsPreview();
                if (active) FillRound(g, l.listenBtn, 9 * s, C(col::ok));
                else FillRound(g, l.listenBtn, 9 * s, hv.hit == Hit::Listen ? C(col::accent) : C(col::accent, 200));
                // Lautsprecher-Symbol
                SolidBrush white(C(0xFFFFFF));
                const float ix = l.listenBtn.X + 16 * s, iy = l.listenBtn.Y + l.listenBtn.Height / 2;
                g.FillRectangle(&white, ix, iy - 3 * s, 4 * s, 6 * s);
                PointF cone[3] = {PointF(ix + 4 * s, iy - 3 * s), PointF(ix + 11 * s, iy - 8 * s), PointF(ix + 11 * s, iy + 8 * s)};
                PointF cone2[3] = {PointF(ix + 4 * s, iy + 3 * s), PointF(ix + 4 * s, iy - 3 * s), PointF(ix + 11 * s, iy + 8 * s)};
                g.FillPolygon(&white, cone, 3);
                g.FillPolygon(&white, cone2, 3);
                Pen wave(C(0xFFFFFF), std::max(1.5f, 1.6f * s));
                g.DrawArc(&wave, ix + 10 * s, iy - 6 * s, 8 * s, 12 * s, -50, 100);
                Text(g, active ? Tr(L"Du hörst mit …") : Tr(L"Einstellungen anhören"), *c->fBody, RectF(l.listenBtn.X + 34 * s, l.listenBtn.Y, l.listenBtn.Width - 40 * s, l.listenBtn.Height),
                     C(0xFFFFFF), StringAlignmentCenter);
                TextWrap(g, Tr(L"Hör die Musik mit EQ an (nur wenn verbunden): Knopf halten oder „Dauerhaft“.\nZiehen = Kurve zeichnen  ·  Rechtsklick = Band zurücksetzen"),
                         *c->fTiny, RectF(l.eqCard.X + kPad * s, l.listenBtn.Y - 6 * s, 392 * s, l.listenBtn.Height + 12 * s), C(col::sub));
                Text(g, Tr(L"Dauerhaft"), *c->fSmall, RectF(l.listenToggle.X - 76 * s, l.listenToggle.Y, 68 * s, l.listenToggle.Height), C(col::sub), StringAlignmentFar);
                DrawToggle(g, l.listenToggle, c->anim[A_LISTEN]);
            }

            // --- Tonspur ----------------------------------------------------------------------
            card(l.tonCard);
            cardTitle(l.tonCard, Icon::Wave, Tr(L"Tonspur – Echtzeit"), 10);
            Text(g, Tr(L"was gerade ins Mikrofon gemischt wird"), *c->fSmall, pad(l.tonCard, 218, 10, 400, 26), C(col::sub));
            FillRound(g, RectF(l.tonPlot.X - 4 * s, l.tonPlot.Y - 2 * s, l.tonPlot.Width + 8 * s, l.tonPlot.Height + 4 * s), 8 * s, C(col::bg));
            {
                const int          cols = static_cast<int>(l.tonPlot.Width / (2 * s));
                std::vector<float> mn(cols), mx(cols);
                en.GetEnvelope(mn.data(), mx.data(), cols);
                const float mid = l.tonPlot.Y + l.tonPlot.Height / 2, half = l.tonPlot.Height / 2;
                Pen         zero(C(col::border), 1.0f);
                g.DrawLine(&zero, l.tonPlot.X, mid, l.tonPlot.X + l.tonPlot.Width, mid);
                const float step = l.tonPlot.Width / cols;
                for (int i = 0; i < cols; ++i) {
                    const float top = std::clamp(mx[i] * 2.2f, 0.0f, 1.0f), bot = std::clamp(-mn[i] * 2.2f, 0.0f, 1.0f);
                    if (top < 0.004f && bot < 0.004f) continue;
                    const float amp = std::max(top, bot);
                    const BYTE  alpha = static_cast<BYTE>(110 + 145 * (static_cast<float>(i) / cols)); // neu = kraeftiger
                    Pen         p(amp > 0.85f ? C(col::bad, alpha) : (amp > 0.6f ? C(col::warn, alpha) : C(col::accent, alpha)), std::max(1.2f, step * 0.8f));
                    const float x = l.tonPlot.X + (i + 0.5f) * step;
                    g.DrawLine(&p, x, mid - top * half, x, mid + bot * half + 0.5f);
                }
            }
        } else if (c->page == 2) {
            // =========================================================== Seite "Dateien" ===========================================================
            const PlayerSnapshot ps = en.GetPlayer();
            const int  count   = static_cast<int>(ps.items.size());
            const bool haveCur = ps.current >= 0 && ps.current < count;
            const double dur   = ps.duration;
            const float  frac  = c->seekDrag ? c->seekFrac : (dur > 0 ? std::clamp(static_cast<float>(ps.position / dur), 0.0f, 1.0f) : 0.0f);

            card(l.filesNow);
            Text(g, haveCur ? ps.items[ps.current].title : std::wstring(Tr(L"Keine Datei ausgewählt")), *c->fTitle, pad(l.filesNow, kPad, 8, 756, 30), C(col::text));
            {
                std::wstring sub;
                uint32_t     color = col::sub;
                if (!ps.message.empty()) { sub = TrMsg(ps.message); color = col::warn; }
                else if (ps.active) { sub = ps.paused ? Tr(L"Pausiert") : Tr(L"Spielt – geht durch Equalizer, Lautstärke und Mikrofon"); color = ps.paused ? col::warn : col::ok; }
                else if (haveCur) sub = Tr(L"Gestoppt");
                else sub = Tr(L"Dateien hineinziehen oder „Dateien öffnen“ wählen");
                Text(g, sub, *c->fSmall, pad(l.filesNow, kPad, 38, 756, 18), C(color));
            }
            DrawSlider(g, l.fileSeek, frac, c->drag.hit == Hit::FileSeek || hv.hit == Hit::FileSeek, false, dur <= 0);
            Text(g, playlist::FormatTime(dur > 0 ? frac * dur : (ps.active ? ps.position : 0)), *c->fTiny, RectF(l.fileSeek.X + 6 * s, l.fileSeek.Y + 20 * s, 120 * s, 16 * s), C(col::sub));
            Text(g, playlist::FormatTime(dur), *c->fTiny, RectF(l.fileSeek.X + l.fileSeek.Width - 126 * s, l.fileSeek.Y + 20 * s, 120 * s, 16 * s), C(col::sub), StringAlignmentFar);

            // Wiedergabe-Knoepfe mit gezeichneten Symbolen
            auto glyphBtn = [&](const RectF& r, int kind, bool hover, bool accent) {
                FillRound(g, r, 9 * s, accent ? (hover ? C(0x5C9CFF) : C(col::accent)) : (hover ? C(col::cardHover) : C(col::track, 150)));
                SolidBrush  b(C(0xFFFFFF));
                const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, u = 7 * s;
                switch (kind) {
                case 0: { // zurueck
                    g.FillRectangle(&b, cx - u, cy - u, 2.2f * s, 2 * u);
                    PointF t[3] = {PointF(cx + u, cy - u), PointF(cx + u, cy + u), PointF(cx - u + 2.2f * s, cy)};
                    g.FillPolygon(&b, t, 3);
                    break;
                }
                case 1: { // abspielen
                    PointF t[3] = {PointF(cx - u * 0.7f, cy - u * 1.1f), PointF(cx - u * 0.7f, cy + u * 1.1f), PointF(cx + u, cy)};
                    g.FillPolygon(&b, t, 3);
                    break;
                }
                case 2: // pause
                    g.FillRectangle(&b, cx - u * 0.8f, cy - u, 4 * s, 2 * u);
                    g.FillRectangle(&b, cx + u * 0.8f - 4 * s, cy - u, 4 * s, 2 * u);
                    break;
                case 3: g.FillRectangle(&b, cx - u * 0.8f, cy - u * 0.8f, 1.6f * u, 1.6f * u); break; // stopp
                default: { // weiter
                    g.FillRectangle(&b, cx + u - 2.2f * s, cy - u, 2.2f * s, 2 * u);
                    PointF t[3] = {PointF(cx - u, cy - u), PointF(cx - u, cy + u), PointF(cx + u - 2.2f * s, cy)};
                    g.FillPolygon(&b, t, 3);
                    break;
                }
                }
            };
            glyphBtn(l.btnPrev, 0, hv.hit == Hit::FilePrev, false);
            glyphBtn(l.btnPlay, ps.active && !ps.paused ? 2 : 1, hv.hit == Hit::FilePlay, true);
            glyphBtn(l.btnStop, 3, hv.hit == Hit::FileStop, false);
            glyphBtn(l.btnNext, 4, hv.hit == Hit::FileNext, false);
            const wchar_t* rep[3] = {Tr(L"Wiederholen: Aus"), Tr(L"Wiederholen: Alle"), Tr(L"Wiederholen: Eins")};
            DrawButton(g, l.btnRepeat, rep[static_cast<int>(ps.repeat)], *c->fSmall, ps.repeat != RepeatMode::Off, hv.hit == Hit::FileRepeat, s);
            DrawButton(g, l.btnClear, Tr(L"Liste leeren"), *c->fSmall, false, hv.hit == Hit::FileClear, s, col::accent, count > 0);
            DrawButton(g, l.btnOpen, Tr(L"Dateien öffnen …"), *c->fSmall, false, hv.hit == Hit::FileOpen, s);

            // Wiedergabeliste bzw. Ablageflaeche
            card(l.fileCard);
            cardTitle(l.fileCard, Icon::Note, Tr(L"Wiedergabeliste"), 10);
            Text(g, count ? std::to_wstring(count) + (count == 1 ? Tr(L" Titel") : Tr(L" Titel "))  /* zweiter Schluessel mit Leerzeichen: Mehrzahl */ : std::wstring(Tr(L"leer")), *c->fSmall, pad(l.fileCard, 178, 8, 200, 28), C(col::sub));
            g.SetClip(l.fileArea);
            if (count == 0) {
                RectF zone(l.fileArea.X + 24 * s, l.fileArea.Y + 24 * s, l.fileArea.Width - 48 * s, l.fileArea.Height - 48 * s);
                Pen   dash(C(col::accent, 150), std::max(1.5f, 1.6f * s));
                dash.SetDashStyle(DashStyleDash);
                GraphicsPath zp;
                RoundPath(zp, zone, 16 * s);
                g.DrawPath(&dash, &zp);
                FillRound(g, RectF(zone.X + zone.Width / 2 - 28 * s, zone.Y + zone.Height / 2 - 70 * s, 56 * s, 56 * s), 14 * s, C(col::accent, 46));
                SolidBrush acc(C(col::accent));
                const float ax = zone.X + zone.Width / 2, ay = zone.Y + zone.Height / 2 - 42 * s; // Pfeil nach unten in die Ablage
                g.FillRectangle(&acc, ax - 3 * s, ay - 14 * s, 6 * s, 16 * s);
                PointF head[3] = {PointF(ax - 11 * s, ay), PointF(ax + 11 * s, ay), PointF(ax, ay + 12 * s)};
                g.FillPolygon(&acc, head, 3);
                Text(g, Tr(L"Audio-Dateien hierher ziehen"), *c->fHead, RectF(zone.X, zone.Y + zone.Height / 2 - 4 * s, zone.Width, 26 * s), C(col::text), StringAlignmentCenter);
                Text(g, Tr(L"Sie werden der Reihe nach in dein Mikrofon eingespielt – mit Equalizer, Lautstärke und Mithören."), *c->fSmall,
                     RectF(zone.X, zone.Y + zone.Height / 2 + 24 * s, zone.Width, 20 * s), C(col::sub), StringAlignmentCenter);
            }
            const double animTime = c->tick * (kTimerMs / 1000.0);
            for (int i = 0; i < count; ++i) {
                const RectF row(l.fileArea.X, l.fileArea.Y + i * 40 * s - c->fileScroll, l.fileArea.Width - 10 * s, 36 * s);
                if (row.Y + row.Height < l.fileArea.Y || row.Y > l.fileArea.Y + l.fileArea.Height) continue;
                const bool cur   = i == ps.current;
                const bool hover = (hv.hit == Hit::FileRow || hv.hit == Hit::FileRemove) && hv.idx == i;
                if (cur) {
                    FillRound(g, row, 8 * s, C(col::accent, 46));
                    StrokeRound(g, row, 8 * s, C(col::accent, 150));
                } else if (hover) {
                    FillRound(g, row, 8 * s, C(col::cardHover));
                }
                if (cur && ps.active && !ps.paused) DrawActivityBars(g, row.X + 12 * s, row.Y + 26 * s, s, 1.0f, animTime, 0.7f);
                else Text(g, cur ? L"▶" : std::to_wstring(i + 1), *c->fSmall, RectF(row.X + 8 * s, row.Y, 34 * s, row.Height), cur ? C(col::accent) : C(col::sub), StringAlignmentCenter);
                Text(g, ps.items[i].title, *c->fBody, RectF(row.X + 50 * s, row.Y, row.Width - 190 * s, row.Height), C(col::text));
                Text(g, playlist::FormatTime(ps.items[i].duration), *c->fSmall, RectF(row.X + row.Width - 130 * s, row.Y, 80 * s, row.Height), C(col::sub), StringAlignmentFar);
                const bool xHover = hv.hit == Hit::FileRemove && hv.idx == i;
                const RectF xr(row.X + row.Width - 36 * s, row.Y + 6 * s, 24 * s, 24 * s);
                if (xHover) FillCircle(g, xr.X + 12 * s, xr.Y + 12 * s, 12 * s, C(col::bad, 70));
                Pen xp(xHover ? C(col::bad) : C(col::sub, 160), std::max(1.5f, 1.6f * s));
                g.DrawLine(&xp, xr.X + 8 * s, xr.Y + 8 * s, xr.X + 16 * s, xr.Y + 16 * s);
                g.DrawLine(&xp, xr.X + 16 * s, xr.Y + 8 * s, xr.X + 8 * s, xr.Y + 16 * s);
            }
            g.ResetClip();
            {
                const float content = count * 40 * s, view = l.fileArea.Height;
                if (content > view) { // schmaler Scrollbalken
                    const float th = std::max(24 * s, view * view / content), ty = l.fileArea.Y + (view - th) * (c->fileScroll / (content - view));
                    FillRound(g, RectF(l.fileArea.X + l.fileArea.Width - 4 * s, ty, 3 * s, th), 1.5f * s, C(col::sub, 120));
                }
            }
            if (c->toastTicks > 0) { // Rueckmeldung zum Hinzufuegen
                const BYTE     a = static_cast<BYTE>(255 * std::min(1.0f, c->toastTicks / 12.0f));
                const RectF    t(l.fileArea.X + l.fileArea.Width / 2 - 200 * s, l.fileArea.Y + l.fileArea.Height - 40 * s, 400 * s, 28 * s);
                const uint32_t color = c->toastBad ? col::bad : col::ok;
                FillRound(g, t, 8 * s, C(col::card, a));
                StrokeRound(g, t, 8 * s, C(color, a));
                Text(g, c->toast, *c->fSmall, t, C(col::text, a), StringAlignmentCenter);
            }
        } else if (c->page == 3) {
            // =========================================================== Seite "Sounds" ===========================================================
            card(l.soundCard);
            cardTitle(l.soundCard, Icon::Pads, Tr(L"Soundboard"), 10);
            const bool editingSound = c->edit.active && c->edit.kind == 2;
            if (editingSound) {
                DrawEditField(g, c, l.soundEditField, s);
                DrawButton(g, l.soundEditOk, Tr(L"Umbenennen"), *c->fSmall, true, hv.hit == Hit::SoundEditOk, s, col::ok);
                DrawButton(g, l.soundEditCancel, Tr(L"Abbrechen"), *c->fSmall, false, hv.hit == Hit::SoundEditCancel, s);
            } else {
                Text(g, Tr(L"Datei auf eine Kachel ziehen oder leere Kachel anklicken  ·  Klick spielt  ·  Rechtsklick: Optionen"), *c->fSmall,
                     pad(l.soundCard, kPad, 38, 748, 22), C(col::sub));
            }
            const auto sounds = en.GetSounds();
            for (int i = 0; i < kSoundSlots; ++i) {
                const RectF&                   t     = l.soundTile[i];
                const Engine::SoundInfo&       si    = sounds[i];
                const bool                     hover = hv.hit == Hit::SoundTile && hv.idx == i;
                const bool                     ready = si.state == Soundboard::State::Ready;
                const float                    flash = c->soundFlash[i];
                FillRound(g, t, 12 * s, ready ? (hover ? C(col::cardHover) : C(0xFFFFFF, 12)) : (hover ? C(col::cardHover) : C(0xFFFFFF, 6)));
                if (flash > 0.01f) FillRound(g, t, 12 * s, C(col::accent, static_cast<BYTE>(110 * flash)));
                StrokeRound(g, t, 12 * s, ready ? C(col::accent, flash > 0.01f ? 220 : 90) : C(col::border, 70), 1.2f);
                FillCircle(g, t.X + 22 * s, t.Y + 22 * s, 12 * s, ready ? C(col::accent, 90) : C(col::track));
                Text(g, std::to_wstring(i + 1), *c->fSmall, RectF(t.X + 10 * s, t.Y + 10 * s, 24 * s, 24 * s), C(col::text), StringAlignmentCenter);
                if (ready) {
                    const float cx = t.X + t.Width / 2, cy = t.Y + 56 * s; // Play-Dreieck
                    PointF      tri[3] = {PointF(cx - 9 * s, cy - 13 * s), PointF(cx - 9 * s, cy + 13 * s), PointF(cx + 14 * s, cy)};
                    SolidBrush  b(C(col::accent));
                    g.FillPolygon(&b, tri, 3);
                    Text(g, si.name, *c->fBody, RectF(t.X + 8 * s, t.Y + 92 * s, t.Width - 16 * s, 24 * s), C(col::text), StringAlignmentCenter);
                    Text(g, playlist::FormatTime(si.seconds), *c->fTiny, RectF(t.X + 8 * s, t.Y + 118 * s, t.Width - 16 * s, 20 * s), C(col::sub), StringAlignmentCenter);
                } else if (si.state == Soundboard::State::Loading) {
                    Text(g, Tr(L"Lädt …"), *c->fBody, RectF(t.X, t.Y + 50 * s, t.Width, 30 * s), C(col::sub), StringAlignmentCenter);
                } else if (si.state == Soundboard::State::Failed) {
                    Text(g, Tr(L"Nicht lesbar"), *c->fBody, RectF(t.X, t.Y + 44 * s, t.Width, 24 * s), C(col::bad), StringAlignmentCenter);
                    Text(g, si.name, *c->fTiny, RectF(t.X + 8 * s, t.Y + 72 * s, t.Width - 16 * s, 20 * s), C(col::sub), StringAlignmentCenter);
                    Text(g, Tr(L"Klick: andere Datei wählen"), *c->fTiny, RectF(t.X + 8 * s, t.Y + 118 * s, t.Width - 16 * s, 20 * s), C(col::sub, 170), StringAlignmentCenter);
                } else { // leer: nur ein dezentes "+" (der Hinweis steht einmal ueber dem Raster)
                    const float cx = t.X + t.Width / 2, cy = t.Y + t.Height / 2;
                    Pen         pp(hover ? C(col::text, 200) : C(col::sub, 110), std::max(2.0f, 2.2f * s));
                    pp.SetStartCap(LineCapRound);
                    pp.SetEndCap(LineCapRound);
                    g.DrawLine(&pp, cx - 12 * s, cy, cx + 12 * s, cy);
                    g.DrawLine(&pp, cx, cy - 12 * s, cx, cy + 12 * s);
                }
            }
            Text(g, Tr(L"Sound-Lautstärke"), *c->fBody, RectF(l.soundVol.X, l.soundVol.Y - 24 * s, 250 * s, 22 * s), C(col::text));
            Text(g, std::to_wstring(st.soundVolPct) + L" %", *c->fBody, RectF(l.soundVol.X + l.soundVol.Width - 80 * s, l.soundVol.Y - 24 * s, 80 * s, 22 * s), C(col::sub), StringAlignmentFar);
            DrawSlider(g, l.soundVol, st.soundVolPct / 200.0f, c->drag.hit == Hit::SoundVol || hv.hit == Hit::SoundVol, true);
            DrawButton(g, l.soundStop, Tr(L"Alle stoppen"), *c->fSmall, false, hv.hit == Hit::SoundStop, s, col::bad);
        }

        g.ResetTransform();
        if (pageE < 1.0f) { // Einblend-Schleier ueber dem Inhaltsbereich (ohne Seitenleiste, Kopfleiste und Fusszeile)
            SolidBrush veil(C(col::bg, static_cast<BYTE>((1.0f - pageE) * 255)));
            g.FillRectangle(&veil, l.header.X - 12 * s, l.header.Y + l.header.Height + 6 * s, static_cast<float>(w) - (l.header.X - 12 * s), l.diag.Y - (l.header.Y + l.header.Height + 6 * s) - 2 * s);
        }

        // Einstellungs-Popup (Zahnrad): der Inhalt wird am Ende des Zeichnens ueber alles gelegt
        auto paintSettings = [&]() {
            // =========================================================== Seite "Einstellungen" ===========================================================
            // Zeile mit Schalter rechts: Titel, darunter eine graue Erklaerung (colX/colW = Spalte der Karte)
            auto toggleRow = [&](const RectF& toggle, float colX, float colW, float anim, const std::wstring& title, const std::wstring& sub, uint32_t subColor, bool hover, uint32_t titleColor = col::text) {
                if (hover) FillRound(g, RectF(colX + 6 * s, toggle.Y - 4 * s, colW - 12 * s, toggle.Height + 8 * s), 8 * s, C(0xFFFFFF, 8));
                Text(g, title, *c->fBody, RectF(colX + kPad * s, toggle.Y - 3 * s, colW - 90 * s, 18 * s), C(titleColor));
                if (!sub.empty()) Text(g, sub, *c->fTiny, RectF(colX + kPad * s, toggle.Y + 15 * s, colW - 90 * s, 16 * s), C(subColor));
                DrawToggle(g, toggle, anim);
            };

            // --- Allgemein: Sprache, Hotkeys ---------------------------------------------------------
            card(l.setGeneral);
            cardTitle(l.setGeneral, Icon::Gear, Tr(L"Allgemein"), 10);
            Text(g, Tr(L"Oberflächensprache"), *c->fBody, pad(l.setGeneral, kPad, 40, 300, 20), C(col::text));
            {
                const wchar_t* names[3] = {Tr(L"Automatisch"), L"Deutsch", L"English"};
                const Hit      hits[3]  = {Hit::LangAuto, Hit::LangDe, Hit::LangEn};
                for (int i = 0; i < 3; ++i) DrawButton(g, l.langBtn[i], names[i], *c->fSmall, st.language == i, hv.hit == hits[i], s);
            }
            Text(g, Tr(L"Tastenkürzel"), *c->fBody, pad(l.setGeneral, kPad, 114, 300, 24), C(col::text));
            DrawButton(g, l.hotkeyBtn, Tr(L"Hotkeys festlegen …"), *c->fSmall, false, hv.hit == Hit::HotkeyBtn, s);

            // --- Titel melden (Now-Playing) ----------------------------------------------------------
            card(l.nowCard);
            cardTitle(l.nowCard, Icon::Chat, Tr(L"Titel melden"), 10);
            {
                const wchar_t* labels[3] = {Tr(L"Im eigenen Chat"), Tr(L"Im Kanal-Chat (alle sehen es)"), Tr(L"In den Client-Metadaten")};
                for (int i = 0; i < 3; ++i) {
                    const bool hov = hv.hit == (i == 0 ? Hit::NowChat : (i == 1 ? Hit::NowChannel : Hit::NowMeta));
                    toggleRow(l.nowToggle[i], l.nowCard.X, l.nowCard.Width, c->anim[A_NOWCHAT + i], labels[i], L"", col::sub, hov, i == 1 ? col::warn : col::text);
                }
            }

            // --- Senden: immer senden, Mono, Qualitaet -----------------------------------------------
            card(l.sendCard);
            cardTitle(l.sendCard, Icon::Send, Tr(L"Senden"), 10);
            toggleRow(l.optToggle, l.sendCard.X, l.sendCard.Width, c->anim[A_ALWAYS], Tr(L"Musik immer senden"), Tr(L"Auch ohne Sprachaktivierung / Push-to-Talk"), col::sub, hv.hit == Hit::AlwaysToggle);
            toggleRow(l.monoToggle, l.sendCard.X, l.sendCard.Width, c->anim[A_MONO], Tr(L"Mono einspielen"), Tr(L"Stereo zu Mono mischen"), col::sub, hv.hit == Hit::MonoToggle);
            Text(g, Tr(L"Qualität"), *c->fBody, RectF(l.sendCard.X + kPad * s, l.qualBtn[0].Y - 26 * s, 300 * s, 20 * s), C(col::text));
            {
                const wchar_t* q[3] = {Tr(L"Niedrige Latenz"), Tr(L"Ausgewogen"), Tr(L"Stabil")};
                for (int i = 0; i < 3; ++i) DrawButton(g, l.qualBtn[i], q[i], *c->fSmall, st.quality == i, hv.hit == Hit::QualityBtn && hv.idx == i, s);
            }

            // --- Info: Stream-Deck-Schnittstelle, Version, Motto -------------------------------------------
            card(l.infoCard);
            cardTitle(l.infoCard, Icon::Info, Tr(L"Info"), 10);
            {
                const bool         run  = RemoteIsRunning();
                const std::wstring port = std::to_wstring(RemotePort());
                const std::wstring line = std::wstring(L"Stream Deck: ") + ReplaceAll(run ? Tr(L"aktiv · Port %s") : Tr(L"nicht verfügbar (Port %s belegt)"), L"%s", port);
                Text(g, line, *c->fSmall, pad(l.infoCard, kPad, 42, 348, 20), C(run ? col::ok : col::warn));
            }
            Text(g, L"VoiceTune 2.0", *c->fSmall, pad(l.infoCard, kPad, 64, 348, 18), C(col::sub));
            Text(g, L"Dave Swift × Michael Schulz", *c->fSign, pad(l.infoCard, kPad, 80, 348, 28), C(col::text, 200));
            Text(g, L"Your Sound. Your Voice. Your Way. Your Tune!", *c->fTiny, pad(l.infoCard, kPad, 108, 348, 16), C(col::sub, 150));

            // --- Erweitert (einklappbar): Musikmodus, Mikro-Gate, Opus --------------------------------------
            {
                const bool open = st.advancedOpen;
                RectF      adv  = l.advCard;
                if (!open) adv.Height = l.advHeader.Height;
                card(adv);
                if (hv.hit == Hit::AdvToggle) FillRound(g, l.advHeader, 10 * s, C(0xFFFFFF, 8));
                DrawIcon(g, Icon::Sliders, RectF(l.advHeader.X + kPad * s, l.advHeader.Y + 12 * s, 20 * s, 20 * s), C(col::accent, 220), s);
                Text(g, Tr(L"Erweitert"), *c->fHead, pad(l.advHeader, kPad + 28, 0, 140, 44), C(col::text));
                if (!open) Text(g, Tr(L"Musikmodus, Mikro-Gate, Klang für Opus"), *c->fSmall, pad(l.advHeader, 160, 0, 500, 44), C(col::sub));
                {   // Pfeil: zu = nach rechts, auf = nach unten
                    const float ax = l.advHeader.X + l.advHeader.Width - 28 * s, ay = l.advHeader.Y + l.advHeader.Height / 2;
                    PointF      closed[3] = {PointF(ax - 3 * s, ay - 6 * s), PointF(ax - 3 * s, ay + 6 * s), PointF(ax + 4 * s, ay)};
                    PointF      opened[3] = {PointF(ax - 6 * s, ay - 3 * s), PointF(ax + 6 * s, ay - 3 * s), PointF(ax, ay + 4 * s)};
                    SolidBrush  b(C(col::sub));
                    g.FillPolygon(&b, open ? opened : closed, 3);
                }
                if (open) {
                    const float lx = l.advCard.X, rx = l.advCard.X + 400 * s, cw = 388 * s;
                    // Musikmodus (links) mit Statuszeile
                    std::wstring mmText;
                    uint32_t     mmColor = col::sub;
                    if (!st.musicMode) {
                        mmText  = Tr(L"AUS: Musik kommt nur durch, solange du sprichst");
                        mmColor = col::warn;
                    } else if (en.MusicModeEngaged()) {
                        const bool sends = en.SendShare() >= 0.6f;
                        mmText           = sends ? Tr(L"aktiv: Dauersenden erzwungen, TS3 sendet die Musik") : Tr(L"aktiv, aber TS3 sendet nicht (Details im TS3-Log)");
                        mmColor          = sends ? col::ok : col::warn;
                    } else {
                        mmText = Tr(L"bereit: schaltet auf Dauersenden, sobald Musik läuft");
                    }
                    toggleRow(l.musicModeToggle, lx, cw, c->anim[A_MUSIC], Tr(L"Musikmodus"), mmText, mmColor, hv.hit == Hit::MusicModeToggle);
                    {   // Mikro-Gate
                        const bool   off = st.gateDb <= -70;
                        const RectF& a   = l.sliderGate;
                        Text(g, Tr(L"Mikro-Gate"), *c->fBody, RectF(a.X - 92 * s, a.Y, 88 * s, a.Height), C(st.musicMode ? col::text : col::sub), StringAlignmentFar);
                        Text(g, off ? std::wstring(Tr(L"aus")) : std::to_wstring(st.gateDb) + L" dB", *c->fSmall, RectF(a.X + a.Width - 56 * s, a.Y, 56 * s, a.Height), C(col::sub), StringAlignmentFar);
                        DrawSlider(g, RectF(a.X, a.Y, a.Width - 60 * s, a.Height), (st.gateDb + 70) / 45.0f, c->drag.hit == Hit::SliderGate || hv.hit == Hit::SliderGate, false, !st.musicMode);
                    }
                    // Opus (rechts)
                    toggleRow(l.optimizeToggle, rx, cw, c->anim[A_OPT], Tr(L"Klang für Opus optimieren"), Tr(L"Hochpass, phasensichere Mono-Summe, Begrenzer"), col::sub, hv.hit == Hit::OptimizeToggle);
                    std::wstring info;
                    uint32_t     infoColor = col::sub;
                    if (!OpusPreview::Available()) {
                        info      = Tr(L"in dieser Version nicht enthalten");
                        infoColor = col::warn;
                    } else if (!st.opusPreview) {
                        info = Tr(L"Mithören/Anhören klingt wie das Original");
                    } else {
                        info = TrOpus(en.OpusInfo());
                        if (info.empty()) info = Tr(L"wird eingerichtet …");
                    }
                    toggleRow(l.opusToggle, rx, cw, c->anim[A_OPUS], Tr(L"Vorschau wie Opus"), info, infoColor, hv.hit == Hit::OpusToggle);
                }
            }
        };

        // --- Diagnose ---------------------------------------------------------------------
        if ((c->page == 0 || c->page == 3) && c->toastTicks > 0) { // Rueckmeldung (Profile, Sounds) in der Fusszeile
            RectF d = l.diag;
            d.Width = (kW - kSide - 24) * s;
            const BYTE a = static_cast<BYTE>(255 * std::min(1.0f, c->toastTicks / 12.0f));
            Text(g, c->toast, *c->fSmall, d, C(c->toastBad ? col::bad : col::ok, a));
        } else {
            RectF d = l.diag;
            if (c->page != 1 && !(c->page == 0 && !st.hiddenApps.empty())) d.Width = (kW - kSide - 24) * s; // rechts ist nur auf der Equalizer-Seite (Kanal-Codec) und bei ausgeblendeten Apps (Seite 1) etwas
            Text(g, TrDiag(en.Diagnostics()), *c->fSmall, d, C(col::sub, 170));
        }
        if (c->page == 2 || c->page == 3 || (c->page == 0 && st.hiddenApps.empty())) // rechts frei: Namen der Autoren, unaufdringlich
            Text(g, L"Dave Swift × Michael Schulz", *c->fSign, l.codec, C(col::sub, 110), StringAlignmentFar);
        if (c->page == 0 && !st.hiddenApps.empty())
            Text(g, ReplaceAll(Tr(L"%s ausgeblendet · Rechtsklick ins Leere: alle einblenden"), L"%s", std::to_wstring(st.hiddenApps.size())), *c->fSmall, l.codec, C(col::sub, 200), StringAlignmentFar);
        if (c->page == 1) { // Kanal-Codec: "Opus Voice" ist mono und sprachoptimiert -> fuer Musik ungeeignet
            const std::wstring codec = en.CodecInfo();
            if (!codec.empty())
                Text(g, std::wstring(Tr(L"Kanal-Codec: ")) + TrCodec(codec) + (en.CodecIsVoice() ? Tr(L"  –  für Musik besser „Opus Music“") : L""), *c->fSmall, l.codec,
                     en.CodecIsVoice() ? C(col::warn) : C(col::sub, 200), StringAlignmentFar);
        }

        if ((c->page == 0 || c->page == 3) && c->menu.open) { // Rechtsklick-Menue
            const MenuState& m  = c->menu;
            const RectF      mr = MenuRect(l, m);
            FillRound(g, RectF(mr.X + 2 * s, mr.Y + 4 * s, mr.Width, mr.Height), 10 * s, C(0x000000, 90));
            FillRound(g, mr, 10 * s, C(col::card));
            StrokeRound(g, mr, 10 * s, C(col::border));
            for (size_t i = 0; i < m.items.size(); ++i) {
                const RectF ir = MenuItemRect(l, m, i);
                if (m.items[i].enabled && hv.hit == Hit::MenuItem && hv.idx == static_cast<int>(i)) FillRound(g, ir, 7 * s, C(col::cardHover));
                Text(g, m.items[i].label, *c->fBody, RectF(ir.X + 12 * s, ir.Y, ir.Width - 20 * s, ir.Height), m.items[i].enabled ? C(col::text) : C(col::sub, 140));
            }
        }

        if (c->page == 1) {
            if (c->toastTicks > 0) { // Rueckmeldung zu Speichern/Umbenennen/Loeschen
                const BYTE     a = static_cast<BYTE>(255 * std::min(1.0f, c->toastTicks / 12.0f));
                const RectF    t(l.plot.X + l.plot.Width / 2 - 170 * s, l.plot.Y + 8 * s, 340 * s, 26 * s);
                const uint32_t color = c->toastBad ? col::bad : col::ok;
                FillRound(g, t, 8 * s, C(col::card, a));
                StrokeRound(g, t, 8 * s, C(color, a));
                Text(g, c->toast, *c->fSmall, t, C(col::text, a), StringAlignmentCenter);
            }
            if (c->presetOpen) { // Dropdown-Liste
                const auto      rows = PopupRows(st);
                const RectF     pr   = PopupRect(l, rows);
                const CurPreset cur2 = CurrentPreset(st);
                FillRound(g, RectF(pr.X + 2 * s, pr.Y + 4 * s, pr.Width, pr.Height), 10 * s, C(0x000000, 90));
                FillRound(g, pr, 10 * s, C(col::card));
                StrokeRound(g, pr, 10 * s, C(col::border));
                g.SetClip(RectF(pr.X, pr.Y + 2 * s, pr.Width, pr.Height - 4 * s));
                for (size_t i = 0; i < rows.size(); ++i) {
                    const RectF rr = PopupRowRect(l, rows, i, c->presetScroll);
                    if (rr.Y + rr.Height < pr.Y || rr.Y > pr.Y + pr.Height) continue;
                    if (rows[i].type == 0) {
                        Text(g, rows[i].label, *c->fTiny, RectF(rr.X + 8 * s, rr.Y, rr.Width, rr.Height), C(col::sub), StringAlignmentNear, StringAlignmentFar);
                    } else if (rows[i].type == 2) {
                        Text(g, rows[i].label, *c->fTiny, RectF(rr.X + 8 * s, rr.Y, rr.Width - 8 * s, rr.Height), C(col::sub, 190));
                    } else {
                        const bool user = rows[i].id >= 100;
                        const bool sel  = presets::EqualNoCase(rows[i].label, cur2.label) && (user ? cur2.user : cur2.builtin);
                        const bool hov  = hv.hit == Hit::PresetItem && hv.idx == rows[i].id;
                        if (sel) FillRound(g, rr, 7 * s, C(col::accent, 60));
                        else if (hov) FillRound(g, rr, 7 * s, C(col::cardHover));
                        if (user) FillCircle(g, rr.X + 12 * s, rr.Y + rr.Height / 2, 3 * s, C(col::accent));
                        Text(g, user ? rows[i].label : Tr(rows[i].label), *c->fBody, RectF(rr.X + 24 * s, rr.Y, rr.Width - 52 * s, rr.Height), C(col::text));
                        if (sel) { // Haken
                            Pen         ck(C(col::accent), std::max(1.6f, 1.8f * s));
                            const float cx = rr.X + rr.Width - 18 * s, cy = rr.Y + rr.Height / 2;
                            g.DrawLine(&ck, cx - 5 * s, cy, cx - 1 * s, cy + 4 * s);
                            g.DrawLine(&ck, cx - 1 * s, cy + 4 * s, cx + 6 * s, cy - 4 * s);
                        }
                    }
                }
                g.ResetClip();
                const float content = PopupContent(l, rows), view = pr.Height - 8 * s;
                if (content - 8 * s > view) { // Scrollbalken
                    const float th = std::max(20 * s, view * view / content), ty = pr.Y + 4 * s + (view - th) * (c->presetScroll / (content - pr.Height));
                    FillRound(g, RectF(pr.X + pr.Width - 5 * s, ty, 3 * s, th), 1.5f * s, C(col::sub, 120));
                }
            }
        }
        if (c->popupT > 0.01f) { // Popup: Hintergrund abdunkeln (Kopfleiste bleibt), Panel gleitet ein und blendet auf
            const float  e  = 1.0f - std::pow(1.0f - c->popupT, 3.0f);
            const float  W = static_cast<float>(w), H = static_cast<float>(h);
            const RectF& pn = l.settingsPanel;
            SolidBrush   dim(C(0x000000, static_cast<BYTE>(150 * e)));
            g.FillRectangle(&dim, 0.0f, l.header.Y + l.header.Height + 6 * s, W, H);
            g.TranslateTransform(0.0f, (1.0f - e) * 18 * s);
            FillRound(g, RectF(pn.X, pn.Y + 6 * s, pn.Width, pn.Height), 16 * s, C(0x000000, static_cast<BYTE>(110 * e)));
            FillRound(g, pn, 16 * s, C(0x181B21));
            StrokeRound(g, pn, 16 * s, C(col::border, 120));
            paintSettings();
            if (e < 1.0f) { // Schleier: das Panel blendet auf
                SolidBrush veil(C(0x181B21, static_cast<BYTE>((1.0f - e) * 255)));
                g.FillRectangle(&veil, pn.X - 1 * s, pn.Y - 1 * s, pn.Width + 2 * s, pn.Height + 8 * s);
            }
            g.ResetTransform();
        }
    }

    // Icons (GDI, mit Clip auf das Raster) nach dem GDI+-Pass; bei offenem Popup entfallen sie (sie wuerden ueber der Abdunkelung liegen)
    if (c->page == 0 && c->popupT < 0.01f && c->pageT > 0.7f) {
        const Layout& l  = c->lay;
        const int     sz = static_cast<int>(32 * l.s);
        SaveDC(mem);
        IntersectClipRect(mem, static_cast<int>(l.gridArea.X), static_cast<int>(l.gridArea.Y), static_cast<int>(l.gridArea.X + l.gridArea.Width),
                          static_cast<int>(l.gridArea.Y + l.gridArea.Height));
        for (const auto& ts : c->tiles) {
            if (ts.info.path.empty()) continue;
            const RectF t = TileRect(c, ts);
            const float x = t.X + (t.Width - sz) / 2, y = t.Y + 12 * l.s;
            if (y + sz < l.gridArea.Y || y > l.gridArea.Y + l.gridArea.Height) continue;
            if (HICON ic = IconFor(c, ts.info.path)) DrawIconEx(mem, static_cast<int>(x), static_cast<int>(y), ic, sz, sz, 0, nullptr, DI_NORMAL);
        }
        RestoreDC(mem, -1);
    }

    BitBlt(dc, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// -------------------------------------------------------------------- Logik ---

void UpdateAnim(Ctx* c)
{
    const Settings st   = Engine::Get().GetSettings();
    auto           ease = [](float& v, float target) { v += (target - v) * 0.35f; if (std::fabs(target - v) < 0.01f) v = target; };
    if (c->pageT < 1.0f) c->pageT = std::min(1.0f, c->pageT + 0.12f); // ca. 0,3 s
    {
        const float target = c->settingsOpen ? 1.0f : 0.0f;
        c->popupT += (target - c->popupT) * 0.35f;
        if (std::fabs(target - c->popupT) < 0.02f) c->popupT = target;
    }
    c->navPos += (static_cast<float>(c->page) - c->navPos) * 0.3f; // gleitende Markierung der Seitenleiste
    if (std::fabs(static_cast<float>(c->page) - c->navPos) < 0.01f) c->navPos = static_cast<float>(c->page);
    ease(c->anim[A_MAIN], st.enabled ? 1.0f : 0.0f);
    ease(c->anim[A_ALWAYS], st.alwaysSend ? 1.0f : 0.0f);
    ease(c->anim[A_MONITOR], st.monitor ? 1.0f : 0.0f);
    ease(c->anim[A_EQ], st.eqEnabled ? 1.0f : 0.0f);
    ease(c->anim[A_MONO], st.mono ? 1.0f : 0.0f);
    ease(c->anim[A_MUSIC], st.musicMode ? 1.0f : 0.0f);
    ease(c->anim[A_OPT], st.optimizeForOpus ? 1.0f : 0.0f);
    ease(c->anim[A_OPUS], st.opusPreview ? 1.0f : 0.0f);
    ease(c->anim[A_NOWCHAT], st.nowPlayingChat ? 1.0f : 0.0f);
    ease(c->anim[A_NOWCHANNEL], st.nowPlayingChannel ? 1.0f : 0.0f);
    ease(c->anim[A_NOWMETA], st.nowPlayingMeta ? 1.0f : 0.0f);
    ease(c->anim[A_LISTEN], Engine::Get().IsPreviewLatched() ? 1.0f : 0.0f);

    float l = 0, r = 0;
    Engine::Get().GetLevels(&l, &r);
    float tl = LevelToDisplay(l), tr = LevelToDisplay(r);
    c->dispL = tl > c->dispL ? tl : c->dispL * 0.88f;
    c->dispR = tr > c->dispR ? tr : c->dispR * 0.88f;
    c->holdL = tl > c->holdL ? tl : std::max(0.0f, c->holdL - 0.006f);
    c->holdR = tr > c->holdR ? tr : std::max(0.0f, c->holdR - 0.006f);

    if (c->page == 1) { // Spektrum nur berechnen, solange es sichtbar ist
        std::vector<float> buf(kFftSize);
        Engine::Get().GetRecentSamples(buf.data(), kFftSize);
        const auto lv = fft::BandLevelsDb(fft::MagnitudeDb(buf.data(), kFftSize), 48000.0, st.eqBands);
        c->spec.resize(st.eqBands, 0.0f);
        for (int i = 0; i < st.eqBands; ++i) {
            const float v = std::clamp((lv[i] + 80.0f) / 75.0f, 0.0f, 1.0f);
            c->spec[i]    = v > c->spec[i] ? v : c->spec[i] * 0.86f;
        }
    }
}

// Beim Ziehen im Plot alle ueberstrichenen Baender setzen (auch bei schneller Mausbewegung).
void PaintEq(Ctx* c, int x, int y)
{
    const Settings st = Engine::Get().GetSettings();
    const int      b  = PlotBand(c->lay, st.eqBands, x);
    const float    db = PlotGain(c->lay, y);
    if (c->lastBand < 0 || c->lastBand == b) {
        Engine::Get().SetEqGain(b, db);
    } else {
        const int   from = c->lastBand, step = b > from ? 1 : -1;
        const float g0   = st.eqGains.size() > static_cast<size_t>(from) ? st.eqGains[from] : 0.0f;
        for (int i = from + step;; i += step) {
            const float t = static_cast<float>(i - from) / static_cast<float>(b - from);
            Engine::Get().SetEqGain(i, g0 + (db - g0) * t);
            if (i == b) break;
        }
    }
    c->lastBand = b;
}

void Dwm(HWND hwnd)
{
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto fn  = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
        if (fn) {
            BOOL     dark    = TRUE;
            int      corners = 2;          // DWMWCP_ROUND
            COLORREF caption = 0x001B1614; // #14161B (BGR)
            COLORREF text    = 0x00F0EAE8;
            fn(hwnd, 20, &dark, sizeof(dark));
            fn(hwnd, 33, &corners, sizeof(corners));
            fn(hwnd, 35, &caption, sizeof(caption));
            fn(hwnd, 36, &text, sizeof(text));
            fn(hwnd, 34, &caption, sizeof(caption)); // Rahmenfarbe
        }
        FreeLibrary(dwm);
    }
}

void (*g_hotkeySetup)() = nullptr; // vom Plugin: oeffnet die Hotkey-Einstellungen von TeamSpeak

// Ersetzt das erste "%s" im (uebersetzten) Text
std::wstring Fmt1(const std::wstring& pattern, const std::wstring& arg)
{
    std::wstring out        = pattern;
    const size_t pos        = out.find(L"%s");
    if (pos != std::wstring::npos) out.replace(pos, 2, arg);
    return out;
}

void ShowToast(Ctx* c, const std::wstring& text, bool bad)
{
    c->toast      = text;
    c->toastBad   = bad;
    c->toastTicks = bad ? 90 : 60; // ca. 3 bzw. 2 s
}

// Namenseingabe starten (Speichern oder Umbenennen). Vorbelegung ist komplett markiert: erstes Zeichen ersetzt sie.
void StartEdit(Ctx* c, bool rename, const std::wstring& initial, int kind = 0, int slot = -1)
{
    c->edit.active    = true;
    c->edit.rename    = rename;
    c->edit.selectAll = true;
    c->edit.text      = initial;
    c->edit.original  = initial;
    c->edit.kind      = kind;
    c->edit.slot      = slot;
    c->presetOpen     = false;
    c->deleteArm      = 0;
}

void CancelEdit(Ctx* c) { c->edit = {}; }

void CommitEdit(Ctx* c)
{
    Engine&            en   = Engine::Get();
    if (c->edit.kind == 2) { // Name eines Sounds
        const std::wstring name = presets::Sanitize(c->edit.text);
        if (name.empty()) {
            ShowToast(c, Tr(L"Bitte einen Namen eingeben"), true);
            return;
        }
        en.SoundRename(c->edit.slot, name);
        ShowToast(c, Fmt1(Tr(L"Umbenannt in „%s“"), name), false);
        CancelEdit(c);
        return;
    }
    if (c->edit.kind == 1) { // Profil speichern bzw. umbenennen
        const std::wstring name     = presets::Sanitize(c->edit.text);
        const std::wstring was      = c->edit.original;
        const bool         isRename = c->edit.rename;
        const bool         existed  = !isRename && en.GetSettings().profiles.Find(name) != nullptr;
        const PresetResult r        = isRename ? en.RenameProfile(was, name) : en.SaveProfile(name);
        switch (r) {
        case PresetResult::Ok:
        case PresetResult::Updated:
            ShowToast(c, isRename ? Fmt1(Tr(L"Umbenannt in „%s“"), name) : Fmt1(existed ? Tr(L"Profil „%s“ aktualisiert") : Tr(L"Profil „%s“ gespeichert"), name), false);
            CancelEdit(c);
            break;
        case PresetResult::Empty: ShowToast(c, Tr(L"Bitte einen Namen eingeben"), true); break;
        case PresetResult::Exists: ShowToast(c, Fmt1(Tr(L"Ein Profil „%s“ gibt es schon"), name), true); break;
        case PresetResult::Full: ShowToast(c, Tr(L"Maximal 30 Profile"), true); break;
        default: ShowToast(c, Tr(L"Das hat nicht geklappt"), true); break;
        }
        return;
    }
    const std::wstring name = presets::Sanitize(c->edit.text);
    const std::wstring was  = c->edit.original;
    const bool         isRename = c->edit.rename;
    const bool         existed  = !isRename && en.GetSettings().userPresets.Find(name) != nullptr;
    const PresetResult r = isRename ? en.RenameEqPreset(was, name) : en.SaveEqPreset(name);
    switch (r) {
    case PresetResult::Ok:
    case PresetResult::Updated:
        ShowToast(c, isRename ? Fmt1(Tr(L"Umbenannt in „%s“"), name) : Fmt1(existed ? Tr(L"Filter „%s“ aktualisiert") : Tr(L"Filter „%s“ gespeichert"), name), false);
        CancelEdit(c);
        break;
    case PresetResult::Empty: ShowToast(c, Tr(L"Bitte einen Namen eingeben"), true); break;
    case PresetResult::Reserved: ShowToast(c, Fmt1(Tr(L"„%s“ ist ein mitgelieferter Name"), name), true); break;
    case PresetResult::Exists: ShowToast(c, Fmt1(Tr(L"Einen Filter „%s“ gibt es schon"), name), true); break;
    case PresetResult::Full: ShowToast(c, Tr(L"Maximal 50 eigene Filter"), true); break;
    default: ShowToast(c, Tr(L"Das hat nicht geklappt"), true); break;
    }
}

std::wstring ClipboardLine(HWND hwnd)
{
    std::wstring text;
    if (OpenClipboard(hwnd)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* p = static_cast<const wchar_t*>(GlobalLock(data))) {
                text = p;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    const size_t nl = text.find_first_of(L"\r\n");
    return nl == std::wstring::npos ? text : text.substr(0, nl);
}

// Alle Audio-Dateien eines Ordners (nach Namen sortiert, Unterordner bis Tiefe 3)
void EnumAudioFiles(const std::wstring& dir, int depth, std::vector<std::wstring>& out)
{
    WIN32_FIND_DATAW fd;
    HANDLE           h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> files, dirs;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 3) dirs.push_back(full);
        } else if (playlist::IsAudioFile(full)) {
            files.push_back(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    auto byName = [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; };
    std::sort(files.begin(), files.end(), byName);
    std::sort(dirs.begin(), dirs.end(), byName);
    for (const auto& f : files) out.push_back(f);
    for (const auto& d : dirs)
        if (out.size() < playlist::Playlist::kMax) EnumAudioFiles(d, depth + 1, out);
}

// Dateien (aus Drag & Drop oder Dialog) an die Wiedergabeliste haengen; startet den ersten neuen Titel, wenn gerade nichts laeuft.
void AddPaths(Ctx* c, const std::vector<std::wstring>& paths)
{
    Engine&              en     = Engine::Get();
    const PlayerSnapshot before = en.GetPlayer();
    const int            first  = static_cast<int>(before.items.size());
    const int            added  = en.PlayerAdd(paths);
    c->presetOpen = false;
    c->menu.open  = false;
    if (added > 0) {
        c->page = 2;
        if (!before.active) en.PlayerPlay(first);
        ShowToast(c, std::to_wstring(added) + (added == 1 ? Tr(L" Datei hinzugefügt") : Tr(L" Dateien hinzugefügt")), false);
    } else {
        c->page = 2;
        ShowToast(c, paths.empty() ? Tr(L"Keine Datei erkannt") : (static_cast<int>(before.items.size()) >= static_cast<int>(playlist::Playlist::kMax) ? Tr(L"Die Liste ist voll") : Tr(L"Keine Audio-Datei erkannt (mp3, wav, flac, m4a, ogg …)")), true);
    }
}

// Filter fuer die Datei-Dialoge ("Audio-Dateien\0*.mp3;...\0Alle Dateien\0*.*\0\0")
std::wstring AudioFilter()
{
    std::wstring f = Tr(L"Audio-Dateien");
    f.push_back(L'\0');
    f += L"*.mp3;*.wav;*.flac;*.m4a;*.aac;*.wma;*.ogg;*.oga;*.opus;*.mp4;*.mka;*.aiff;*.aif;*.webm;*.mp2;*.wv;*.ac3";
    f.push_back(L'\0');
    f += Tr(L"Alle Dateien");
    f.push_back(L'\0');
    f += L"*.*";
    f.push_back(L'\0');
    return f;
}

// Dateien den Sound-Slots zuweisen: die erste auf `preferredSlot` (falls >= 0), alle weiteren auf freie Plaetze
void AssignSounds(Ctx* c, const std::vector<std::wstring>& paths, int preferredSlot)
{
    Engine&           en     = Engine::Get();
    auto              sounds = en.GetSounds();
    std::vector<bool> used(kSoundSlots, false);
    int               assigned = 0;
    bool              noRoom   = false;
    for (const auto& path : paths) {
        if (!playlist::IsAudioFile(path)) continue;
        int target = -1;
        if (assigned == 0 && preferredSlot >= 0) {
            target = preferredSlot;
        } else {
            for (int i = 0; i < kSoundSlots; ++i)
                if (!used[i] && sounds[i].path.empty()) {
                    target = i;
                    break;
                }
        }
        if (target < 0) {
            noRoom = true;
            break;
        }
        en.SoundSetSlot(target, path);
        used[target]         = true;
        sounds[target].path = path;
        ++assigned;
    }
    c->presetOpen = false;
    c->menu.open  = false;
    c->page       = 3;
    if (assigned > 0)
        ShowToast(c, std::to_wstring(assigned) + (assigned == 1 ? Tr(L" Sound zugewiesen") : Tr(L" Sounds zugewiesen")) + (noRoom ? Tr(L" (alle Plätze belegt)") : L""), false);
    else
        ShowToast(c, noRoom ? Tr(L"Alle 8 Plätze sind belegt") : Tr(L"Keine Audio-Datei erkannt"), true);
}

// Eine Datei fuer einen Sound-Slot waehlen
void PickSoundFile(Ctx* c, int slot)
{
    std::vector<wchar_t> buf(4096, L'\0');
    const std::wstring   filter = AudioFilter();
    const std::wstring   title  = Tr(L"Sound-Datei auswählen");
    OPENFILENAMEW        ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = c->hwnd;
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle  = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    AssignSounds(c, {std::wstring(buf.data())}, slot);
}

void OnDropFiles(Ctx* c, HDROP drop)
{
    POINT pt{0, 0};
    DragQueryPoint(drop, &pt);
    std::vector<std::wstring> paths;
    const UINT                n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        wchar_t buf[MAX_PATH * 2];
        if (!DragQueryFileW(drop, i, buf, MAX_PATH * 2)) continue;
        const DWORD attr = GetFileAttributesW(buf);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) EnumAudioFiles(buf, 0, paths); // Ordner: alle Audio-Dateien darin
        else paths.push_back(buf);
    }
    DragFinish(drop);
    if (c->page == 3) { // Seite "Sounds": auf eine Kachel gezogen = dieser Slot, sonst freie Plaetze
        int slot = -1;
        for (int i = 0; i < kSoundSlots; ++i)
            if (In(c->lay.soundTile[i], pt.x, pt.y)) slot = i;
        AssignSounds(c, paths, slot);
        return;
    }
    AddPaths(c, paths);
}

void OpenFilesDialog(Ctx* c)
{
    std::vector<wchar_t> buf(32768, L'\0');
    OPENFILENAMEW        ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = c->hwnd;
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = static_cast<DWORD>(buf.size());
    const std::wstring title  = Tr(L"Audio-Dateien auswählen");
    const std::wstring filter = AudioFilter();
    ofn.lpstrTitle  = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    ofn.Flags       = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    std::vector<std::wstring> paths;
    const std::wstring        first = buf.data();
    const wchar_t*            next  = buf.data() + first.size() + 1;
    if (*next == L'\0') { // eine einzelne Datei
        paths.push_back(first);
    } else { // Ordner, dann die Dateinamen
        while (*next) {
            paths.push_back(first + L"\\" + next);
            next += wcslen(next) + 1;
        }
    }
    AddPaths(c, paths);
}

float SeekFrac(const RectF& r, int x)
{
    const float pad = 8 * (r.Height / 22.0f);
    return std::clamp((x - (r.X + pad)) / (r.Width - 2 * pad), 0.0f, 1.0f);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        c       = static_cast<Ctx*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        c->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(c));
        c->lay        = MakeLayout(GetScale());
        const float s = c->lay.s;
        auto        font = [s](float px, int style) { return std::make_unique<Font>(L"Segoe UI", px * s, style, UnitPixel); };
        c->fTitle = font(20, FontStyleBold);
        c->fHead  = font(14, FontStyleBold);
        c->fBody  = font(14, FontStyleRegular);
        c->fSmall = font(12, FontStyleRegular);
        c->fTiny  = font(11, FontStyleRegular);
        c->fSign  = std::make_unique<Font>(L"Segoe Script", 12 * s, FontStyleRegular, UnitPixel); // Unterschrift-Schrift (Windows 10/11)
        if (!c->fSign->IsAvailable()) c->fSign = std::make_unique<Font>(L"Segoe UI", 12 * s, FontStyleItalic, UnitPixel);
        c->appIcon = LoadAppIcon();
        if (c->appIcon) { // Icon im Fensterkopf und in der Taskleiste
            c->iconBig   = MakeWindowIcon(*c->appIcon, 32);
            c->iconSmall = MakeWindowIcon(*c->appIcon, 16);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(c->iconBig));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(c->iconSmall));
        }
        const Settings st = Engine::Get().GetSettings();
        c->anim[A_MAIN] = st.enabled, c->anim[A_ALWAYS] = st.alwaysSend, c->anim[A_MONITOR] = st.monitor, c->anim[A_EQ] = st.eqEnabled, c->anim[A_MONO] = st.mono, c->anim[A_MUSIC] = st.musicMode,
        c->anim[A_OPT] = st.optimizeForOpus, c->anim[A_OPUS] = st.opusPreview,
        c->anim[A_NOWCHAT] = st.nowPlayingChat, c->anim[A_NOWCHANNEL] = st.nowPlayingChannel, c->anim[A_NOWMETA] = st.nowPlayingMeta;
        Dwm(hwnd);
        DragAcceptFiles(hwnd, TRUE); // Audio-Dateien per Drag & Drop (auch wenn TeamSpeak mit anderen Rechten laeuft als der Explorer)
        if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
            using FilterFn = BOOL(WINAPI*)(HWND, UINT, DWORD, void*);
            if (auto filter = reinterpret_cast<FilterFn>(reinterpret_cast<void*>(GetProcAddress(user, "ChangeWindowMessageFilterEx")))) {
                filter(hwnd, WM_DROPFILES, 1 /*MSGFLT_ALLOW*/, nullptr);
                filter(hwnd, 0x0049 /*WM_COPYGLOBALDATA*/, 1, nullptr);
            }
        }
        StartRefresh(c);
        SetTimer(hwnd, kTimerId, kTimerMs, nullptr);
        return 0;
    }
    case WM_DROPFILES:
        if (c) OnDropFiles(c, reinterpret_cast<HDROP>(wp));
        return 0;
    case WM_APPS_READY: {
        std::unique_ptr<std::vector<AppInfo>> apps(reinterpret_cast<std::vector<AppInfo>*>(lp));
        if (c && apps) {
            MergeApps(c, std::move(*apps));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_TIMER:
        if (c) {
            UpdateAnim(c);
            AnimateTiles(c);
            if (c->deleteArm > 0) --c->deleteArm;
            if (c->toastTicks > 0) --c->toastTicks;
            for (float& f : c->soundFlash) f = std::max(0.0f, f - 0.12f);
            if (++c->tick % kRefreshTicks == 0) StartRefresh(c);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        GetClientRect(hwnd, &rc);
        if (c) Paint(c, dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!c) break;
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        Engine& en = Engine::Get();
        if (c->drag.hit == Hit::SliderMusic) en.SetMusicVolume(SliderValue(c->lay.sliderMusic, x, 200));
        else if (c->drag.hit == Hit::SliderMic) en.SetMicVolume(SliderValue(c->lay.sliderMic, x, 200));
        else if (c->drag.hit == Hit::SliderDelay) en.SetMonitorDelay(SliderValue(c->lay.sliderDelay, x, kMaxDelayMs));
        else if (c->drag.hit == Hit::SliderGate) en.SetGateDb(-70 + SliderValue(c->lay.sliderGate, x, 45, 1));
        else if (c->drag.hit == Hit::SoundVol) en.SetSoundVolume(SliderValue(c->lay.soundVol, x, 200));
        else if (c->drag.hit == Hit::EqPlot) PaintEq(c, x, y);
        else if (c->drag.hit == Hit::FileSeek) c->seekFrac = SeekFrac(c->lay.fileSeek, x);
        c->hover = HitTest(c, x, y);
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (c) c->hover = {};
        return 0;
    case WM_SETCURSOR:
        if (c && LOWORD(lp) == HTCLIENT) {
            const bool hand = (c->hover.hit != Hit::None && c->hover.hit != Hit::Grid && c->hover.hit != Hit::SettingsBackdrop && c->hover.hit != Hit::SettingsPanel) || c->drag.hit != Hit::None;
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (!c) break;
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        const HitInfo h  = HitTest(c, x, y);
        Engine&       en = Engine::Get();
        const Settings st = en.GetSettings();
        if (c->deleteArm > 0 && h.hit != Hit::PresetDelete && h.hit != Hit::ProfDelete) c->deleteArm = 0; // "Wirklich loeschen?" verfaellt bei jedem anderen Klick
        if (c->menu.open) { // Rechtsklick-Menue offen: Eintrag ausfuehren oder schliessen; der Klick geht nie "durch"
            const MenuState m = c->menu;
            c->menu.open      = false;
            if (h.hit == Hit::MenuItem && static_cast<size_t>(h.idx) < m.items.size() && m.items[h.idx].enabled) {
                const MenuEntry& e = m.items[h.idx];
                if (e.action == 1) {
                    en.HideApp(e.arg);
                    c->tiles.erase(std::remove_if(c->tiles.begin(), c->tiles.end(), [&](const TileState& t) { return t.info.exe == e.arg; }), c->tiles.end());
                } else if (e.action == 2) {
                    en.ShowAllApps();
                    StartRefresh(c); // sofort neu laden, damit die Apps gleich wieder auftauchen
                } else if (e.action == 3) { // Sound entfernen
                    en.SoundSetSlot(std::stoi(e.arg), L"");
                } else if (e.action == 4) { // Sound umbenennen
                    const int slot = std::stoi(e.arg);
                    StartEdit(c, true, en.GetSounds()[slot].name, 2, slot);
                } else if (e.action == 5) { // Datei waehlen bzw. ersetzen
                    PickSoundFile(c, std::stoi(e.arg));
                }
            }
            return 0;
        }
        if (c->presetOpen) { // Dropdown offen: Auswahl uebernehmen oder schliessen; der Klick geht nie "durch" auf Elemente darunter
            c->presetOpen = false;
            if (h.hit == Hit::PresetItem) {
                if (h.idx < 100) en.ApplyBuiltinPreset(h.idx);
                else if (static_cast<size_t>(h.idx - 100) < st.userPresets.items.size()) en.ApplyUserPreset(st.userPresets.items[h.idx - 100].name);
                return 0;
            }
            if (h.hit != Hit::TabMix && h.hit != Hit::TabEq && h.hit != Hit::TabFiles && h.hit != Hit::TabSounds) return 0;
        }
        switch (h.hit) {
        case Hit::MicMute: en.SetMicMuted(!en.IsMicMuted()); break;
        case Hit::FilePrev: en.PlayerPrev(); break;
        case Hit::FilePlay: en.PlayerTogglePause(); break;
        case Hit::FileStop: en.PlayerStop(); break;
        case Hit::FileNext: en.PlayerNext(); break;
        case Hit::FileRepeat: {
            const RepeatMode m = en.GetPlayer().repeat;
            en.PlayerSetRepeat(m == RepeatMode::Off ? RepeatMode::All : (m == RepeatMode::All ? RepeatMode::One : RepeatMode::Off));
            break;
        }
        case Hit::FileClear: en.PlayerClear(); break;
        case Hit::FileOpen: OpenFilesDialog(c); break;
        case Hit::FileRow: en.PlayerPlay(h.idx); break;
        case Hit::FileRemove: en.PlayerRemove(h.idx); break;
        case Hit::FileSeek:
            c->drag     = h;
            c->seekDrag = true;
            c->seekFrac = SeekFrac(c->lay.fileSeek, x);
            SetCapture(hwnd);
            break;
        case Hit::TabMix:
        case Hit::TabEq:
        case Hit::TabFiles:
        case Hit::TabSounds: {
            const int newPage = (h.hit == Hit::TabMix) ? 0 : (h.hit == Hit::TabEq ? 1 : (h.hit == Hit::TabFiles ? 2 : 3));
            if (newPage != c->page) {
                CancelEdit(c); // eine begonnene Namenseingabe gehoert zur Seite
                c->pageT = 0;  // neue Seite gleitet ein
            }
            c->page       = newPage;
            c->presetOpen = false;
            c->deleteArm  = 0;
            break;
        }
        case Hit::SettingsGear: // Zahnrad: Popup auf/zu
            c->settingsOpen = !c->settingsOpen;
            c->menu.open    = false;
            c->presetOpen   = false;
            CancelEdit(c);
            break;
        case Hit::SettingsBackdrop: c->settingsOpen = false; break; // Klick neben das Panel schliesst es
        case Hit::SettingsPanel: break;
        case Hit::LangAuto: en.SetLanguage(0); break;
        case Hit::AdvToggle: en.SetAdvancedOpen(!st.advancedOpen); break;
        case Hit::LangDe: en.SetLanguage(1); break;
        case Hit::LangEn: en.SetLanguage(2); break;
        case Hit::HotkeyBtn:
            if (g_hotkeySetup) g_hotkeySetup();
            else ShowToast(c, Tr(L"Hotkeys: TeamSpeak → Einstellungen → Hotkeys → Plugins"), false);
            break;
        case Hit::NowChat: en.SetNowPlayingChat(!st.nowPlayingChat); break;
        case Hit::NowChannel: en.SetNowPlayingChannel(!st.nowPlayingChannel); break;
        case Hit::NowMeta: en.SetNowPlayingMeta(!st.nowPlayingMeta); break;
        case Hit::ProfPrev:
        case Hit::ProfNext: {
            const auto names = en.ProfileNames();
            if (names.empty()) {
                ShowToast(c, Tr(L"Noch keine Profile – speichere zuerst eines mit „Speichern als …“"), true);
                break;
            }
            const int n = static_cast<int>(names.size());
            int       cur = -1;
            for (int i = 0; i < n; ++i)
                if (presets::EqualNoCase(names[i], st.profileName)) cur = i;
            const int next = h.hit == Hit::ProfNext ? (cur + 1) % n : (cur < 0 ? n - 1 : (cur + n - 1) % n);
            en.ApplyProfile(names[next]);
            break;
        }
        case Hit::ProfField: break;
        case Hit::ProfSave:
            if (c->edit.active && c->edit.kind == 1) CommitEdit(c);
            else StartEdit(c, false, st.profiles.Find(st.profileName) ? st.profileName : st.profiles.SuggestName(), 1); // ein gewaehltes Profil wird so aktualisiert
            break;
        case Hit::ProfRename:
            if (c->edit.active && c->edit.kind == 1) CancelEdit(c);
            else if (st.profiles.Find(st.profileName)) StartEdit(c, true, st.profileName, 1);
            break;
        case Hit::ProfDelete: {
            const Profile* pr = st.profiles.Find(st.profileName);
            if (!pr) break;
            if (c->deleteArm > 0) { // zweiter Klick: wirklich loeschen
                const std::wstring name = pr->name;
                en.DeleteProfile(name);
                ShowToast(c, Fmt1(Tr(L"Profil „%s“ gelöscht"), name), false);
                c->deleteArm = 0;
            } else {
                c->deleteArm = 90;
            }
            break;
        }
        case Hit::SoundTile: {
            if (h.idx < 0 || h.idx >= kSoundSlots || c->edit.active) break; // beim Umbenennen nicht abspielen
            const auto state = en.GetSounds()[h.idx].state;
            if (state == Soundboard::State::Ready) {
                en.SoundPlay(h.idx);
                c->soundFlash[h.idx] = 1.0f;
            } else if (state != Soundboard::State::Loading) {
                PickSoundFile(c, h.idx); // leer oder nicht lesbar: Datei waehlen
            }
            break;
        }
        case Hit::SoundVol:
            c->drag = h;
            SetCapture(hwnd);
            en.SetSoundVolume(SliderValue(c->lay.soundVol, x, 200));
            break;
        case Hit::SoundStop: en.SoundStop(); break;
        case Hit::SoundEditOk: CommitEdit(c); break;
        case Hit::SoundEditCancel: CancelEdit(c); break;
        case Hit::PresetField:
            if (!c->edit.active) {
                c->presetOpen   = true;
                c->presetScroll = 0;
            }
            break;
        case Hit::PresetSave:
            if (c->edit.active) CommitEdit(c);
            else {
                const CurPreset cur = CurrentPreset(st);
                StartEdit(c, false, cur.user ? cur.label : st.userPresets.SuggestName()); // ein gewaehlter eigener Filter wird so aktualisiert
            }
            break;
        case Hit::PresetRename:
            if (c->edit.active) CancelEdit(c);
            else {
                const CurPreset cur = CurrentPreset(st);
                if (cur.user) StartEdit(c, true, cur.label);
            }
            break;
        case Hit::PresetDelete: {
            const CurPreset cur = CurrentPreset(st);
            if (!cur.user) break;
            if (c->deleteArm > 0) { // zweiter Klick: wirklich loeschen
                en.DeleteEqPreset(cur.label);
                ShowToast(c, Fmt1(Tr(L"Filter „%s“ gelöscht"), cur.label), false);
                c->deleteArm = 0;
            } else {
                c->deleteArm = 90;
            }
            break;
        }
        case Hit::MainToggle: en.SetEnabled(!st.enabled); break;
        case Hit::AlwaysToggle: en.SetAlwaysSend(!st.alwaysSend); break;
        case Hit::MonitorToggle: en.SetMonitor(!st.monitor); break;
        case Hit::Tile: en.SetTarget(c->tiles[h.idx].info.exe); break;
        case Hit::ListenToggle: en.SetPreviewLatch(!en.IsPreviewLatched()); break;
        case Hit::EqToggle: en.SetEqEnabled(!st.eqEnabled); break;
        case Hit::MonoToggle: en.SetMono(!st.mono); break;
        case Hit::MusicModeToggle: en.SetMusicMode(!st.musicMode); break;
        case Hit::OptimizeToggle: en.SetOptimizeForOpus(!st.optimizeForOpus); break;
        case Hit::OpusToggle: en.SetOpusPreview(!st.opusPreview); break;
        case Hit::SliderGate:
            c->drag = h;
            SetCapture(hwnd);
            en.SetGateDb(-70 + SliderValue(c->lay.sliderGate, x, 45, 1));
            break;
        case Hit::QualityBtn: en.SetQuality(h.idx); break;
        case Hit::EqBandsBtn: en.SetEqBands(h.idx == 0 ? 10 : h.idx == 1 ? 31 : 61); break;
        case Hit::SliderMusic:
            c->drag = h;
            SetCapture(hwnd);
            en.SetMusicVolume(SliderValue(c->lay.sliderMusic, x, 200));
            break;
        case Hit::SliderMic:
            c->drag = h;
            SetCapture(hwnd);
            en.SetMicVolume(SliderValue(c->lay.sliderMic, x, 200));
            break;
        case Hit::SliderDelay:
            c->drag = h;
            SetCapture(hwnd);
            en.SetMonitorDelay(SliderValue(c->lay.sliderDelay, x, kMaxDelayMs));
            break;
        case Hit::EqPlot:
            c->drag     = h;
            c->lastBand = -1;
            SetCapture(hwnd);
            PaintEq(c, x, y);
            break;
        case Hit::Listen: // "Einstellungen anhoeren": nur solange gehalten
            c->drag = h;
            SetCapture(hwnd);
            en.SetPreview(true);
            break;
        default: break;
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (c && c->drag.hit != Hit::None) {
            if (c->drag.hit == Hit::Listen) Engine::Get().SetPreview(false);
            if (c->drag.hit == Hit::FileSeek && msg == WM_LBUTTONUP) { // erst beim Loslassen springen
                const PlayerSnapshot ps = Engine::Get().GetPlayer();
                if (ps.duration > 0) Engine::Get().PlayerSeek(c->seekFrac * ps.duration);
            }
            c->seekDrag = false;
            c->drag = {};
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (c && c->settingsOpen) return 0;
        if (c && c->page == 1) { // Band auf 0 dB zuruecksetzen
            const HitInfo h = HitTest(c, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (h.hit == Hit::EqPlot) Engine::Get().SetEqGain(h.idx, 0.0f);
        } else if (c && c->page == 0) { // Rechtsklick: auf eine App = ausblenden, ins Leere = alle einblenden
            c->menu.open = false;
            const int      x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            const HitInfo  h = HitTest(c, x, y);
            const Settings st = Engine::Get().GetSettings();
            const std::wstring showAll = Fmt1(Tr(L"Alle Apps einblenden (%s)"), std::to_wstring(st.hiddenApps.size()));
            if (h.hit == Hit::Tile) {
                const AppInfo& a = c->tiles[h.idx].info;
                if (a.exe == st.targetExe) c->menu.items.assign(1, MenuEntry{Tr(L"Aktive Quelle – nicht ausblendbar"), 0, false, L""});
                else c->menu.items.assign(1, MenuEntry{Fmt1(Tr(L"„%s“ ausblenden"), PrettyExeName(a.exe)), 1, true, a.exe});
                if (!st.hiddenApps.empty()) c->menu.items.push_back(MenuEntry{showAll, 2, true, L""});
            } else if (h.hit == Hit::Grid) {
                c->menu.items.assign(1, MenuEntry{st.hiddenApps.empty() ? Tr(L"Keine Apps ausgeblendet") : showAll, 2, !st.hiddenApps.empty(), L""});
            } else {
                return 0;
            }
            c->menu.open = true;
            c->menu.x    = static_cast<float>(x);
            c->menu.y    = static_cast<float>(y);
        } else if (c && c->page == 3) { // Rechtsklick auf eine Sound-Kachel: Umbenennen / Ersetzen / Entfernen
            c->menu.open  = false;
            const int     x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            const HitInfo h = HitTest(c, x, y);
            if (h.hit != Hit::SoundTile || c->edit.active) return 0;
            const auto         info = Engine::Get().GetSounds()[h.idx];
            const std::wstring arg  = std::to_wstring(h.idx);
            c->menu.items.clear();
            if (info.path.empty()) {
                c->menu.items.push_back(MenuEntry{Tr(L"Datei auswählen …"), 5, true, arg});
            } else {
                c->menu.items.push_back(MenuEntry{Tr(L"Umbenennen"), 4, info.state != Soundboard::State::Loading, arg});
                c->menu.items.push_back(MenuEntry{Tr(L"Datei ersetzen …"), 5, true, arg});
                c->menu.items.push_back(MenuEntry{Tr(L"Entfernen"), 3, true, arg});
            }
            c->menu.open = true;
            c->menu.x    = static_cast<float>(x);
            c->menu.y    = static_cast<float>(y);
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (c && c->page == 1) {
            const HitInfo h = HitTest(c, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (h.hit == Hit::EqPlot) {
                Engine::Get().SetEqGain(h.idx, 0.0f);
                return 0;
            }
        }
        return WndProc(hwnd, WM_LBUTTONDOWN, wp, lp); // zweiter Klick eines Doppelklicks zaehlt wie ein normaler Klick
    case WM_MOUSEWHEEL:
        if (c && c->page == 1 && c->presetOpen) { // Dropdown-Liste scrollen
            const Settings st   = Engine::Get().GetSettings();
            const auto     rows = PopupRows(st);
            const float    maxScroll = std::max(0.0f, PopupContent(c->lay, rows) - PopupRect(c->lay, rows).Height);
            c->presetScroll = std::clamp(c->presetScroll - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 28 * c->lay.s, 0.0f, maxScroll);
            return 0;
        }
        if (c && c->page == 2) { // Wiedergabeliste scrollen
            const int   count     = static_cast<int>(Engine::Get().GetPlayer().items.size());
            const float maxScroll = std::max(0.0f, count * 40 * c->lay.s - c->lay.fileArea.Height);
            c->fileScroll         = std::clamp(c->fileScroll - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 40 * c->lay.s, 0.0f, maxScroll);
            return 0;
        }
        if (c && c->page == 0) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            if (In(c->lay.gridCard, p.x, p.y))
                c->scroll = std::clamp(c->scroll - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * (c->lay.tileH + c->lay.gap), 0.0f, MaxScroll(c));
        }
        return 0;
    case WM_KEYDOWN:
        if (c && wp == VK_ESCAPE) { // erst Eingabe/Dropdown schliessen, dann das Fenster
            if (c->menu.open) c->menu.open = false;
            else if (c->settingsOpen) c->settingsOpen = false;
            else if (c->edit.active) CancelEdit(c);
            else if (c->presetOpen) c->presetOpen = false;
            else DestroyWindow(hwnd);
        } else if (c && c->edit.active && wp == VK_DELETE && c->edit.selectAll) {
            c->edit.text.clear();
            c->edit.selectAll = false;
        }
        return 0;
    case WM_CHAR: // Namenseingabe fuer eigene EQ-Filter
        if (c && c->edit.active) {
            const wchar_t ch = static_cast<wchar_t>(wp);
            if (ch == L'\r') CommitEdit(c);
            else if (ch == 27) CancelEdit(c);
            else if (ch == L'\b') {
                if (c->edit.selectAll) c->edit.text.clear();
                else if (!c->edit.text.empty()) c->edit.text.pop_back();
                c->edit.selectAll = false;
            } else if (ch == 22) { // Strg+V
                const std::wstring paste = presets::Sanitize(ClipboardLine(hwnd));
                if (c->edit.selectAll) c->edit.text.clear();
                c->edit.selectAll = false;
                c->edit.text      = presets::Sanitize(c->edit.text + paste);
            } else if (ch >= 32 && ch != 127 && ch != L'"') {
                if (c->edit.selectAll) c->edit.text.clear();
                c->edit.selectAll = false;
                if (c->edit.text.size() < presets::kMaxName) c->edit.text += ch;
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        Engine::Get().SetPreview(false); // "Anhoeren" nie haengen lassen (auch nicht der Dauerhaft-Schalter)
        Engine::Get().SetPreviewLatch(false);
        KillTimer(hwnd, kTimerId);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void DialogThread()
{
    GdiplusStartupInput gsi;
    ULONG_PTR           token = 0;
    GdiplusStartup(&token, &gsi, nullptr);

    WNDCLASSW wc{};
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"VoiceTuneConfig";
    RegisterClassW(&wc);

    {
        Ctx   ctx;
        float s = GetScale();
        RECT  r{0, 0, static_cast<LONG>(kW * s), static_cast<LONG>(kH * s)};
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&r, style, FALSE);
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"VoiceTune", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                    wc.hInstance, &ctx);
        if (hwnd) {
            g_hwnd = hwnd;
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        g_hwnd = nullptr;
        if (ctx.refresher.joinable()) ctx.refresher.join();
        // Nachzuegler-Nachrichten der Aktualisierung freigeben
        MSG m;
        while (PeekMessageW(&m, nullptr, WM_APPS_READY, WM_APPS_READY, PM_REMOVE)) delete reinterpret_cast<std::vector<AppInfo>*>(m.lParam);
        for (auto& kv : ctx.icons)
            if (kv.second) DestroyIcon(kv.second);
        ctx.fTitle.reset();
        ctx.fHead.reset();
        ctx.fBody.reset();
        ctx.fSmall.reset();
        ctx.fTiny.reset();
        ctx.appIcon.reset(); // vor GdiplusShutdown freigeben
        if (ctx.iconBig) DestroyIcon(ctx.iconBig);
        if (ctx.iconSmall) DestroyIcon(ctx.iconSmall);
    }

    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    GdiplusShutdown(token);
    g_running = false;
}

} // namespace

void SetHotkeySetupHandler(void (*fn)()) { g_hotkeySetup = fn; }

void OpenConfigDialog()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_running) {
        if (HWND h = g_hwnd.load()) {
            ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
        }
        return;
    }
    if (g_thread.joinable()) g_thread.join();
    g_running = true;
    g_thread  = std::thread(DialogThread);
}

void CloseConfigDialog()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (HWND h = g_hwnd.load()) PostMessageW(h, WM_CLOSE, 0, 0);
    if (g_thread.joinable()) g_thread.join();
}
