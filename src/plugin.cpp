// TeamSpeak-3-Plugin "VoiceTune" (Your Sound. Your Voice. Your Way. Your Tune!): mischt den Ton einer ausgewaehlten Windows-App (Spotify, Browser/YouTube, ...)
// oder eigener Audio-Dateien
// in das Mikrofonsignal.
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "app_discovery.h"
#include "commands.h"
#include "config_dialog.h"
#include "soundboard_view.h"
#include "soundboard_window.h"
#include "engine.h"
#include "remote.h"
#include "plugin_definitions.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_rare_definitions.h"
#include "teamspeak/public_errors.h"
#include "ts3_functions.h"

#define PLUGIN_API_VERSION 26

#define EXPORT extern "C" __declspec(dllexport)

namespace {

struct TS3Functions ts3Functions;
char*               g_pluginId = nullptr;

enum { MENU_ID_TOGGLE = 1, MENU_ID_CONFIG = 2, MENU_ID_SOUNDBOARD = 3 };

void Log(const char* msg) { ts3Functions.logMessage(msg, LogLevel_INFO, "VoiceTune", 0); }


std::wstring Utf8ToWide(const char* s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return L"";
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}

// Icons liegen als PNG in <TS3>\plugins\VoiceTune_win64\ (Ordnername = DLL-Name ohne .dll).
std::string WideToUtf8(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// Chat-Meldung (TS3-BBCode) bei jedem Ein-/Ausschalten, egal ob per Menue oder Fenster: ein Banner mit Linie oben und unten.
// An = gruen, Aus = dunkelrot; beide Meldungen sind gleich aufgebaut.
void OnStateChanged(bool on)
{
    const Settings    s     = Engine::Get().GetSettings();
    const std::string app   = WideToUtf8(Engine::Get().SourceLabel());
    const std::string color = on ? "#2ECC71" : "#B0171F";

    const std::string bar  = "[color=" + color + "]━━━━━━━━━━━━━━━━━━━━━━━━━━[/color]";
    const std::string line = "[b][color=" + color + "]  " + Tr8(on ? "♪  MUSIK AN" : "■  MUSIK AUS") + "[/color][/b]  [color=#8A90A0]·[/color]  [b]" + app + "[/b]";
    const std::string msg  = bar + "\n" + line + "\n" + bar;
    ts3Functions.printMessageToCurrentTab(msg.c_str());
}

// Stream-Deck-Schnittstelle konnte nicht starten (meist: Port belegt, weil schon ein zweites TeamSpeak mit VoiceTune laeuft): Hinweis im TS3-Client.
// Beim Plugin-Start gibt es evtl. noch keinen sichtbaren Tab, deshalb wird er beim ersten Verbinden mit einem Server noch einmal gezeigt.
bool           g_remoteDown = false, g_remoteNoticePending = false;
unsigned short g_remotePort = 0;

void PrintRemoteNotice()
{
    std::string text = Tr8("Die Stream-Deck-Schnittstelle ist aktuell nicht verfügbar. Der Port %s ist belegt – vermutlich läuft schon ein zweites TeamSpeak mit VoiceTune. Das Stream Deck steuert nur die erste Instanz. Alles andere im Plugin funktioniert normal.");
    const size_t pos = text.find("%s");
    if (pos != std::string::npos) text.replace(pos, 2, std::to_string(g_remotePort));
    const std::string msg = "[color=#E67E22][b]VoiceTune:[/b] " + text + "[/color]";
    ts3Functions.printMessageToCurrentTab(msg.c_str());
}

void OnRemoteUnavailable(unsigned short port)
{
    g_remoteDown          = true;
    g_remoteNoticePending = true;
    g_remotePort          = port;
    Log(("Stream-Deck-Schnittstelle nicht verfügbar (Port " + std::to_string(port) + " belegt)").c_str());
    PrintRemoteNotice();
}

// Chat-Zeile bei Befehlen per Hotkey/Stream Deck, die kein eigenes Banner haben (Stumm). Stil wie das An/Aus-Banner.
void OnCommandFeedback(const char* kind, bool on)
{
    if (std::strcmp(kind, "micmute") != 0) return;
    const std::string msg = on ? "[b][color=#B0171F]" + Tr8("■  MIKROFON STUMM") + "[/color][/b]" : "[b][color=#2ECC71]" + Tr8("●  MIKROFON AKTIV") + "[/color][/b]";
    ts3Functions.printMessageToCurrentTab(msg.c_str());
}

// ------------------------------------------------------------------------------------------------------------------------------------------------
// Now-Playing: Titelwechsel in den eigenen Chat, optional in den Kanal-Chat und/oder die Client-Metadaten (alles nur auf der aktuellen Verbindung).
std::string g_metaOriginal;           // Metadaten des Nutzers, bevor wir sie ueberschrieben haben
bool        g_metaSet = false;
uint64      g_metaSchid = 0;

// "[" im Titel wuerde TS3 als BBCode lesen
std::string EscapeBB(const std::string& in)
{
    std::string out;
    for (char c : in) {
        if (c == '[') out += '\\';
        out += c;
    }
    return out;
}

void RestoreMeta()
{
    if (!g_metaSet) return;
    g_metaSet = false;
    ts3Functions.setClientSelfVariableAsString(g_metaSchid, CLIENT_META_DATA, g_metaOriginal.c_str());
    ts3Functions.flushClientSelfUpdates(g_metaSchid, nullptr);
}

void OnNowPlaying(const std::wstring& title)
{
    if (title.empty()) { // Musik aus: Metadaten zuruecksetzen
        RestoreMeta();
        return;
    }
    const Settings    s     = Engine::Get().GetSettings();
    const std::string t     = WideToUtf8(title);
    const uint64      schid = ts3Functions.getCurrentServerConnectionHandlerID();

    if (s.nowPlayingChat) {
        const std::string msg = "[color=#4C8DFF]♪[/color] [b]" + Tr8("Spielt:") + "[/b] " + EscapeBB(t);
        ts3Functions.printMessageToCurrentTab(msg.c_str());
    }
    if (s.nowPlayingChannel) { // erreicht alle im Kanal: nur auf ausdruecklichen Wunsch
        anyID  me = 0;
        uint64 ch = 0;
        if (ts3Functions.getClientID(schid, &me) == ERROR_ok && ts3Functions.getChannelOfClient(schid, me, &ch) == ERROR_ok && ch != 0) {
            const std::string msg = Tr8("♪ Spielt: ") + EscapeBB(t);
            ts3Functions.requestSendChannelTextMsg(schid, msg.c_str(), ch, nullptr);
        }
    }
    if (s.nowPlayingMeta) {
        if (!g_metaSet) {
            char* cur = nullptr;
            g_metaOriginal.clear();
            if (ts3Functions.getClientSelfVariableAsString(schid, CLIENT_META_DATA, &cur) == ERROR_ok && cur) {
                g_metaOriginal = cur;
                ts3Functions.freeMemory(cur);
            }
        }
        const std::string meta = "♪ " + t;
        if (ts3Functions.setClientSelfVariableAsString(schid, CLIENT_META_DATA, meta.c_str()) == ERROR_ok) {
            ts3Functions.flushClientSelfUpdates(schid, nullptr);
            g_metaSet   = true;
            g_metaSchid = schid;
        }
    } else {
        RestoreMeta(); // Schalter wurde ausgeschaltet
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------
// Musikmodus: TS3-Vorverarbeitung (Speex-Preprocessor) umgehen.
// TS3 ruft unseren Mikrofon-Callback VOR seiner Vorverarbeitung auf: Rauschfilter ("denoise"), Auto-Pegel ("agc"), Echo-Reduktion und Sprachaktivierung
// ("vad") sind auf Sprache abgestimmt, daher klingt Musik dort schlechter und wird ohne Sprechen gar nicht gesendet. Solange Musik laeuft, schalten wir sie
// aus und stellen danach exakt die vorherigen Werte des Nutzers wieder her.
std::string SelfStateReport(uint64 schid); // weiter unten

struct PreState {
    uint64                             schid = 0;
    std::map<std::string, std::string> saved; // ident -> urspruenglicher Wert (nur fuer Eintraege, die wir behandelt haben)
};
PreState g_pre;
bool     g_inputSaved = false; // Eingangsstatus (CLIENT_INPUT_DEACTIVATED) wurde von uns geaendert
int      g_inputOrig  = 0;

unsigned ApplyPreprocess(uint64 schid, unsigned mask)
{
    struct Item { const char* ident; unsigned bit; };
    static const Item items[] = {{"vad", 2}, {"agc", 1}, {"denoise", 1}, {"echo_reduction", 1}};

    if (!g_pre.saved.empty() && g_pre.schid != schid) { // Verbindung gewechselt: erst bei der alten zuruecksetzen
        for (auto& kv : g_pre.saved)
            if (kv.second == "true") ts3Functions.setPreProcessorConfigValue(g_pre.schid, kv.first.c_str(), "true");
        g_pre.saved.clear();
        if (g_inputSaved) ts3Functions.setClientSelfVariableAsInt(g_pre.schid, CLIENT_INPUT_DEACTIVATED, g_inputOrig);
        if (g_inputSaved) ts3Functions.flushClientSelfUpdates(g_pre.schid, nullptr);
        g_inputSaved = false;
    }
    g_pre.schid = schid;

    unsigned    applied = 0;
    std::string report;
    for (const Item& it : items) {
        const bool engaged = g_pre.saved.count(it.ident) != 0;
        const bool want    = (mask & it.bit) != 0;
        if (want && !engaged) {
            char* cur = nullptr;
            if (ts3Functions.getPreProcessorConfigValue(schid, it.ident, &cur) != ERROR_ok || !cur) {
                report += std::string(" ") + it.ident + "=n/a";
                continue;
            }
            const std::string orig = cur;
            ts3Functions.freeMemory(cur);
            g_pre.saved[it.ident] = orig;
            if (orig == "true") {
                const unsigned err = ts3Functions.setPreProcessorConfigValue(schid, it.ident, "false");
                report += std::string(" ") + it.ident + (err == ERROR_ok ? " aus" : " FEHLER " + std::to_string(err));
                if (err == ERROR_ok) applied |= it.bit;
            } else {
                report += std::string(" ") + it.ident + " war schon aus";
            }
        } else if (!want && engaged) {
            if (g_pre.saved[it.ident] == "true") {
                const unsigned err = ts3Functions.setPreProcessorConfigValue(g_pre.schid, it.ident, "true");
                report += std::string(" ") + it.ident + (err == ERROR_ok ? " wieder an" : " FEHLER " + std::to_string(err));
            }
            g_pre.saved.erase(it.ident);
        }
    }
    // Dauersenden: Eingang aktiv schalten (im Push-to-Talk-Modus steht er sonst auf "deaktiviert"), danach Aenderungen an TS3 uebergeben
    bool inputChanged = false;
    if ((mask & 2u) && !g_inputSaved) {
        int cur = -1;
        if (ts3Functions.getClientSelfVariableAsInt(schid, CLIENT_INPUT_DEACTIVATED, &cur) == ERROR_ok) {
            g_inputSaved = true;
            g_inputOrig  = cur;
            if (cur != INPUT_ACTIVE) {
                const unsigned err = ts3Functions.setClientSelfVariableAsInt(schid, CLIENT_INPUT_DEACTIVATED, INPUT_ACTIVE);
                report += err == ERROR_ok ? " Eingang aktiviert" : " Eingang FEHLER " + std::to_string(err);
                if (err == ERROR_ok) { applied |= 2u; inputChanged = true; }
            }
        }
    } else if (!(mask & 2u) && g_inputSaved) {
        if (g_inputOrig != INPUT_ACTIVE) {
            const unsigned err = ts3Functions.setClientSelfVariableAsInt(g_pre.schid, CLIENT_INPUT_DEACTIVATED, g_inputOrig);
            report += err == ERROR_ok ? " Eingang wiederhergestellt" : " Eingang FEHLER " + std::to_string(err);
            inputChanged = true;
        }
        g_inputSaved = false;
    }
    if (applied || inputChanged || !g_pre.saved.empty() || !report.empty()) ts3Functions.flushClientSelfUpdates(schid, nullptr);

    if (!report.empty()) Log(((mask ? "Musikmodus AN:" : "Musikmodus AUS:") + report + "  [" + SelfStateReport(schid) + "]").c_str());
    return applied;
}

// Zustand der Verbindung fuer die Diagnose im Log: warum sendet TS3 nicht? (vad, Schwelle, Eingang, stumm, spricht, Talk-Power, noetige Talk-Power)
std::string SelfStateReport(uint64 schid)
{
    std::string r = "schid " + std::to_string(schid);
    for (const char* ident : {"vad", "voiceactivation_level", "agc", "denoise"}) {
        char* v = nullptr;
        if (ts3Functions.getPreProcessorConfigValue(schid, ident, &v) == ERROR_ok && v) {
            r += std::string(" ") + ident + "=" + v;
            ts3Functions.freeMemory(v);
        }
    }
    struct F { const char* name; size_t flag; };
    for (const F& f : {F{"Eingang-deaktiviert", CLIENT_INPUT_DEACTIVATED}, F{"Mikro-stumm", CLIENT_INPUT_MUTED}, F{"spricht", CLIENT_FLAG_TALKING},
                        F{"Talk-Power", CLIENT_TALK_POWER}, F{"Talker", CLIENT_IS_TALKER}}) {
        int v = 0;
        r += std::string(" ") + f.name + "=" + (ts3Functions.getClientSelfVariableAsInt(schid, f.flag, &v) == ERROR_ok ? std::to_string(v) : "n/a");
    }
    anyID  me = 0;
    uint64 ch = 0;
    int    need = 0;
    if (ts3Functions.getClientID(schid, &me) == ERROR_ok && ts3Functions.getChannelOfClient(schid, me, &ch) == ERROR_ok &&
        ts3Functions.getChannelVariableAsInt(schid, ch, CHANNEL_NEEDED_TALK_POWER, &need) == ERROR_ok)
        r += " noetige-Talk-Power=" + std::to_string(need);
    return r;
}

// Kanal-Codec des aktuellen Kanals fuer den Hinweis im Fenster ("Opus Voice, Qualität 7").
std::wstring ChannelCodecInfo(uint64 schid)
{
    anyID  me = 0;
    uint64 ch = 0;
    int    codec = -1, quality = -1;
    if (ts3Functions.getClientID(schid, &me) != ERROR_ok || ts3Functions.getChannelOfClient(schid, me, &ch) != ERROR_ok) return L"";
    if (ts3Functions.getChannelVariableAsInt(schid, ch, CHANNEL_CODEC, &codec) != ERROR_ok) return L"";
    ts3Functions.getChannelVariableAsInt(schid, ch, CHANNEL_CODEC_QUALITY, &quality);
    const wchar_t* name = codec == CODEC_OPUS_VOICE ? L"Opus Voice" : codec == CODEC_OPUS_MUSIC ? L"Opus Music" : codec == CODEC_CELT_MONO ? L"CELT" : L"Speex";
    return std::wstring(name) + L", Qualität " + std::to_wstring(quality);
}

// Kodierung des aktuellen Kanals fuer die Opus-Vorschau: Opus Voice = mono, Opus Music = stereo. Die Bitrate liefert TS3 selbst
// (getEncodeConfigValue "bitrate"); ohne Angabe wird sie aus der Kanalqualitaet (0..10) geschaetzt.
bool ChannelEncodeInfo(uint64 schid, EncodeInfo* out)
{
    anyID  me = 0;
    uint64 ch = 0;
    int    codec = -1, quality = 5;
    if (ts3Functions.getClientID(schid, &me) != ERROR_ok || ts3Functions.getChannelOfClient(schid, me, &ch) != ERROR_ok) return false;
    if (ts3Functions.getChannelVariableAsInt(schid, ch, CHANNEL_CODEC, &codec) != ERROR_ok) return false;
    if (codec != CODEC_OPUS_VOICE && codec != CODEC_OPUS_MUSIC) return false;
    ts3Functions.getChannelVariableAsInt(schid, ch, CHANNEL_CODEC_QUALITY, &quality);
    quality = std::min(10, std::max(0, quality));

    out->channels   = codec == CODEC_OPUS_MUSIC ? 2 : 1;
    out->exact      = false;
    out->bitrateBps = codec == CODEC_OPUS_MUSIC ? 24000 + quality * 8000 : 6000 + quality * 4000;
    char* v = nullptr;
    if (ts3Functions.getEncodeConfigValue && ts3Functions.getEncodeConfigValue(schid, "bitrate", &v) == ERROR_ok && v) {
        double rate = std::atof(v);
        ts3Functions.freeMemory(v);
        if (rate > 0 && rate < 1000) rate *= 1000; // manche Versionen melden kbit/s statt bit/s
        if (rate >= 6000 && rate <= 512000) {
            out->bitrateBps = static_cast<int>(rate);
            out->exact      = true;
        }
    }
    return true;
}

PluginMenuItem* CreateMenuItem(PluginMenuType type, int id, const char* text, const char* icon)
{
    auto* item = static_cast<PluginMenuItem*>(std::malloc(sizeof(PluginMenuItem)));
    item->type = type;
    item->id   = id;
    std::strncpy(item->text, text, PLUGIN_MENU_BUFSZ - 1);
    item->text[PLUGIN_MENU_BUFSZ - 1] = '\0';
    std::strncpy(item->icon, icon, PLUGIN_MENU_BUFSZ - 1);
    item->icon[PLUGIN_MENU_BUFSZ - 1] = '\0';
    return item;
}

} // namespace

EXPORT const char* ts3plugin_name() { return "VoiceTune"; }
EXPORT const char* ts3plugin_version() { return "2.0"; }
EXPORT int         ts3plugin_apiVersion() { return PLUGIN_API_VERSION; }
EXPORT const char* ts3plugin_author() { return "Michael Schulz"; }
EXPORT const char* ts3plugin_description()
{
    return "VoiceTune – Your Sound. Your Voice. Your Way. Your Tune!\n\n"
           "Spielt den Ton einer beliebigen Windows-App (Spotify, YouTube im Browser, Discord …) oder eigener Audio-Dateien (mp3, wav, flac, m4a … per Drag & Drop) "
           "direkt in dein TeamSpeak-Mikrofon.\n\n"
           "• Pro-App-Aufnahme: nur die gewählte App wird gesendet – keine Stimmen anderer, kein Echo\n"
           "• Datei-Player mit Wiedergabeliste, Springen, Pause und Wiederholen\n"
           "• Equalizer mit 10, 31 oder 61 Bändern, Presets und eigenen Filtern, Live-Spektrum und Tonspur\n"
           "• Musikmodus: Musik kommt auch ohne Sprechen durch (umgeht Sprachaktivierung und Sprachfilter, mit Mikro-Gate)\n"
           "• Mithören und Anhören mit einstellbarer Verzögerung und Opus-Vorschau: höre, wie es bei den anderen ankommt\n"
           "• Klang-Optimierung für Opus, Lautstärken, Mikrofon-Stumm, Chat-Meldungen\n"
           "• Soundboard mit frei belegbaren Jingle-Knöpfen (Raster, Hotkeys, eigenes Fenster), Profile (Szenen), globale Hotkeys, Titelanzeige im Chat, Stream-Deck-Schnittstelle\n\n"
           "Plays the audio of any Windows app (Spotify, YouTube, Discord …) or your own audio files into your TeamSpeak microphone – with per-app capture, "
           "equalizer, monitoring, Opus preview, soundboard, profiles, hotkeys and a Stream Deck interface. The window is available in German and English.\n\n"
           "Your Sound. Your Voice. Your Way. Your Tune!";
}

EXPORT void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) { ts3Functions = funcs; }

EXPORT int ts3plugin_init()
{
    char configPath[512] = {};
    ts3Functions.getConfigPath(configPath, sizeof(configPath));
    Engine::Get().SetLogger(Log);
    Engine::Get().SetStateCallback(OnStateChanged);
    Engine::Get().SetPreprocessCallback(ApplyPreprocess);
    Engine::Get().SetCodecCallback(ChannelCodecInfo);
    Engine::Get().SetEncodeCallback(ChannelEncodeInfo);
    Engine::Get().SetDiagCallback(SelfStateReport);
    Engine::Get().Init(Utf8ToWide(configPath));
    SetHotkeySetupHandler([] {
        if (ts3Functions.showHotkeySetup) ts3Functions.showHotkeySetup();
    });
    sbv::SetHotkeyHandlers(
        [](int slot) { // Tastenkuerzel eines Soundboard-Knopfes: Aufnahme-Dialog von TeamSpeak
            if (!ts3Functions.requestHotkeyInputDialog || !g_pluginId) return;
            char keyword[24];
            std::snprintf(keyword, sizeof(keyword), "sound%d", slot + 1);
            ts3Functions.requestHotkeyInputDialog(g_pluginId, keyword, 1, nullptr);
        },
        [](int slot) -> std::string { // gebundenes Kuerzel (leer = keins)
            if (!ts3Functions.getHotkeyFromKeyword || !g_pluginId) return std::string();
            char        keyword[24];
            std::snprintf(keyword, sizeof(keyword), "sound%d", slot + 1);
            const char* keywords[1] = {keyword};
            char        buf[PLUGIN_HOTKEY_BUFSZ] = {};
            char*       outs[1]                  = {buf};
            if (ts3Functions.getHotkeyFromKeyword(g_pluginId, keywords, outs, 1, sizeof(buf)) != 0) return std::string();
            return buf;
        },
        [] {
            if (ts3Functions.showHotkeySetup) ts3Functions.showHotkeySetup();
        });
    SetCommandNotifier(OnCommandFeedback);
    RemoteSetUnavailableCallback(OnRemoteUnavailable);
    RemoteStart();
    Log("VoiceTune geladen");
    return 0;
}

EXPORT void ts3plugin_shutdown()
{
    RemoteStop();
    CloseSoundboardWindow();
    CloseConfigDialog();
    RestoreMeta();
    Engine::Get().Shutdown();
    if (g_pluginId) {
        std::free(g_pluginId);
        g_pluginId = nullptr;
    }
}

EXPORT void ts3plugin_freeMemory(void* data) { std::free(data); }

EXPORT int ts3plugin_requestAutoload() { return 1; } // nach der Installation von selbst aktiv (der Nutzer kann es in den Plugin-Einstellungen abschalten)

// Erste erfolgreiche Verbindung: Hinweis zur Stream-Deck-Schnittstelle (falls sie nicht starten konnte) dort zeigen, wo der Nutzer gerade ist
EXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber)
{
    if (g_remoteDown && g_remoteNoticePending && newStatus == STATUS_CONNECTION_ESTABLISHED) {
        g_remoteNoticePending = false;
        PrintRemoteNotice();
    }
}

EXPORT int ts3plugin_offersConfigure() { return PLUGIN_OFFERS_CONFIGURE_NEW_THREAD; }

// Das Fenster laeuft in einem eigenen Thread; die Funktion kehrt sofort zurueck.
EXPORT void ts3plugin_configure(void* handle, void* qParentWidget) { OpenConfigDialog(); }

EXPORT void ts3plugin_registerPluginID(const char* id)
{
    std::free(g_pluginId);
    g_pluginId = _strdup(id);
}

EXPORT void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon)
{
    *menuItems    = static_cast<PluginMenuItem**>(std::malloc(sizeof(PluginMenuItem*) * 4));
    (*menuItems)[0] = CreateMenuItem(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_TOGGLE, Tr8("Musik einspielen: Ein/Aus").c_str(), "toggle.png");
    (*menuItems)[1] = CreateMenuItem(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_CONFIG, Tr8("Steuerung öffnen").c_str(), "settings.png");
    (*menuItems)[2] = CreateMenuItem(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_SOUNDBOARD, Tr8("Soundboard öffnen").c_str(), "plugin.png");
    (*menuItems)[3] = nullptr;
    *menuIcon       = static_cast<char*>(std::malloc(PLUGIN_MENU_BUFSZ));
    std::strcpy(*menuIcon, "plugin.png");
}

// Nach requestHotkeyInputDialog meldet TeamSpeak das aufgenommene Kuerzel; es ist dann schon dem Schluesselwort zugeordnet - nichts zu tun
EXPORT void ts3plugin_onHotkeyRecordedEvent(const char* keyword, const char* key) {}

EXPORT void ts3plugin_onMenuItemEvent(uint64 serverConnectionHandlerID, enum PluginMenuType type, int menuItemID, uint64 selectedItemID)
{
    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;
    if (menuItemID == MENU_ID_TOGGLE) {
        Engine::Get().SetEnabled(!Engine::Get().GetSettings().enabled); // die Chat-Meldung kommt aus OnStateChanged
    } else if (menuItemID == MENU_ID_CONFIG) {
        OpenConfigDialog();
    } else if (menuItemID == MENU_ID_SOUNDBOARD) {
        OpenSoundboardWindow();
    }
}

// Mithoeren: gemischte Wiedergabe des Clients. Nur fuer den aktiven Server-Tab, sonst wuerde die Musik pro Tab doppelt eingemischt.
// Hotkeys: die Belegung macht der Nutzer in TS3 (Einstellungen -> Hotkeys -> Plugins -> VoiceTune); Array und Eintraege werden mit malloc angelegt
// und von TS3 ueber ts3plugin_freeMemory freigegeben (wie die Menue-Eintraege).
EXPORT void ts3plugin_initHotkeys(struct PluginHotkey*** hotkeys)
{
    const auto& list = HotkeyList();
    *hotkeys         = static_cast<PluginHotkey**>(std::malloc(sizeof(PluginHotkey*) * (list.size() + 1)));
    for (size_t i = 0; i < list.size(); ++i) {
        auto* h = static_cast<PluginHotkey*>(std::malloc(sizeof(PluginHotkey)));
        std::memset(h, 0, sizeof(*h));
        std::strncpy(h->keyword, list[i].keyword, PLUGIN_HOTKEY_BUFSZ - 1);
        if (list[i].number > 0) std::snprintf(h->description, PLUGIN_HOTKEY_BUFSZ, Tr8(list[i].description).c_str(), list[i].number);
        else std::strncpy(h->description, Tr8(list[i].description).c_str(), PLUGIN_HOTKEY_BUFSZ - 1);
        (*hotkeys)[i] = h;
    }
    (*hotkeys)[list.size()] = nullptr;
}

EXPORT void ts3plugin_onHotkeyEvent(const char* keyword)
{
    if (keyword) RunHotkey(keyword);
}

EXPORT void ts3plugin_onEditMixedPlaybackVoiceDataEvent(uint64 serverConnectionHandlerID, short* samples, int sampleCount, int channels,
                                                        const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    if (serverConnectionHandlerID != ts3Functions.getCurrentServerConnectionHandlerID()) return;
    Engine::Get().MixMonitor(samples, sampleCount, channels, channelSpeakerArray, channelFillMask);
}

// Wird fuer jeden Mikrofon-Block aufgerufen (interleaved int16, 48 kHz).
// edited: Bit 0 = Samples veraendert, Bit 1 = Block auch senden, wenn PTT/VAD nicht ausloest.
EXPORT void ts3plugin_onEditCapturedVoiceDataEvent(uint64 serverConnectionHandlerID, short* samples, int sampleCount, int channels, int* edited)
{
    Engine::Get().Mix(serverConnectionHandlerID, samples, sampleCount, channels, edited);
}
