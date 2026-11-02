// Minimaler "Fake-TeamSpeak": laedt die Plugin-DLL und ruft die Exports in der Reihenfolge des Clients auf.
// ts3_host_test.exe <pfad\zur\plugin.dll>
#include <windows.h>

#include <shellapi.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "plugin_definitions.h"
#include "teamspeak/public_definitions.h"
#include "ts3_functions.h"

// Nachgestellte TS3-Vorverarbeitung (Speex): Werte wie in einem Client mit Sprachaktivierung
using PreMap = std::map<std::string, std::string>;
static std::map<uint64, PreMap> g_prePer;
static std::map<uint64, int>    g_inputPer;
static bool                     g_ptt = false;
static PreMap& Pre(uint64 sch)
{
    auto it = g_prePer.find(sch);
    if (it == g_prePer.end()) it = g_prePer.emplace(sch, PreMap{{"vad", g_ptt ? "false" : "true"}, {"agc", "true"}, {"denoise", "true"}, {"echo_reduction", "false"}}).first;
    return it->second;
}
static int& Input(uint64 sch)
{
    auto it = g_inputPer.find(sch);
    if (it == g_inputPer.end()) it = g_inputPer.emplace(sch, g_ptt ? 1 : 0).first;
    return it->second;
}
static DWORD g_t0 = 0;
static unsigned FakeGetPre(uint64 sch, const char* ident, char** out)
{
    auto it = Pre(sch).find(ident);
    if (it == Pre(sch).end()) return 1;
    *out = _strdup(it->second.c_str());
    return 0;
}
static unsigned FakeSetPre(uint64 sch, const char* ident, const char* value)
{
    std::printf("  [%5lu ms] schid %llu Vorverarbeitung: %s = %s\n", timeGetTime() - g_t0, static_cast<unsigned long long>(sch), ident, value);
    Pre(sch)[ident] = value;
    return 0;
}
struct DropFilesHeader { DWORD pFiles; POINT pt; BOOL fNC; BOOL fWide; }; // wie DROPFILES aus shlobj.h
static unsigned FakeFree(void* p) { std::free(p); return 0; }
static unsigned FakeGetSelf(uint64 sch, size_t flag, int* out)
{
    if (flag != CLIENT_INPUT_DEACTIVATED) return 1;
    *out = Input(sch);
    return 0;
}
static unsigned FakeSetSelf(uint64 sch, size_t flag, int v)
{
    if (flag != CLIENT_INPUT_DEACTIVATED) return 1;
    std::printf("  [%5lu ms] schid %llu Eingang: %s\n", timeGetTime() - g_t0, static_cast<unsigned long long>(sch), v ? "deaktiviert" : "aktiv");
    Input(sch) = v;
    return 0;
}
static unsigned FakeFlush(uint64, const char*)
{
    std::printf("  [%5lu ms] flushClientSelfUpdates\n", timeGetTime() - g_t0);
    return 0;
}
static unsigned FakeClientId(uint64, anyID* id) { *id = 1; return 0; }
static unsigned FakeChannelOf(uint64, anyID, uint64* ch) { *ch = 5; return 0; }
static unsigned FakeChannelInt(uint64, uint64, size_t flag, int* out)
{
    *out = flag == CHANNEL_CODEC ? CODEC_OPUS_VOICE : 7; // Kanal mit "Opus Voice", Qualitaet 7
    return 0;
}

static unsigned FakeEncodeConfig(uint64, const char* ident, char** out)
{
    if (std::strcmp(ident, "bitrate") != 0) return 1;
    const char* v = std::getenv("AAM_BITRATE"); // z.B. 32000; ohne Angabe liefert die Nachbildung den Wert, den TS3 fuer Opus Voice Qualitaet 7 meldet
    *out = _strdup(v ? v : "34000");
    return 0;
}

static unsigned int FakeLog(const char* msg, enum LogLevel, const char*, uint64) { std::printf("  [log] %s\n", msg); return 0; }
static void FakeConfigPath(char* path, size_t max) { std::snprintf(path, max, "%s", getenv("TEMP")); }
static void FakePrint(const char* m) { std::printf("  [tab] %s\n", m); }

template <class T>
static T Sym(HMODULE m, const char* name)
{
    T f = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m, name)));
    std::printf("%-40s %s\n", name, f ? "ok" : "FEHLT");
    return f;
}

int main(int argc, char** argv)
{
    HMODULE m = LoadLibraryA(argc > 1 ? argv[1] : "VoiceTune_win64.dll");
    if (!m) { std::printf("LoadLibrary fehlgeschlagen: %lu\n", GetLastError()); return 1; }

    auto name    = Sym<const char* (*)()>(m, "ts3plugin_name");
    auto version = Sym<const char* (*)()>(m, "ts3plugin_version");
    auto api     = Sym<int (*)()>(m, "ts3plugin_apiVersion");
    auto author  = Sym<const char* (*)()>(m, "ts3plugin_author");
    auto desc    = Sym<const char* (*)()>(m, "ts3plugin_description");
    auto setFn   = Sym<void (*)(const TS3Functions)>(m, "ts3plugin_setFunctionPointers");
    auto init    = Sym<int (*)()>(m, "ts3plugin_init");
    auto shut    = Sym<void (*)()>(m, "ts3plugin_shutdown");
    auto regId   = Sym<void (*)(const char*)>(m, "ts3plugin_registerPluginID");
    auto menus   = Sym<void (*)(PluginMenuItem***, char**)>(m, "ts3plugin_initMenus");
    auto freeMem = Sym<void (*)(void*)>(m, "ts3plugin_freeMemory");
    auto edit    = Sym<void (*)(uint64, short*, int, int, int*)>(m, "ts3plugin_onEditCapturedVoiceDataEvent");
    if (!name || !version || !api || !author || !desc || !setFn || !init || !shut) return 2;

    std::printf("name=%s version=%s api=%d author=%s\n", name(), version(), api(), author());

    TS3Functions f{};
    f.logMessage               = FakeLog;
    f.getConfigPath            = FakeConfigPath;
    f.printMessageToCurrentTab = FakePrint;
    f.getPreProcessorConfigValue = FakeGetPre;
    f.setPreProcessorConfigValue = FakeSetPre;
    f.freeMemory                 = FakeFree;
    f.getClientSelfVariableAsInt = FakeGetSelf;
    f.setClientSelfVariableAsInt = FakeSetSelf;
    f.flushClientSelfUpdates     = FakeFlush;
    f.getClientID                = FakeClientId;
    f.getChannelOfClient         = FakeChannelOf;
    f.getChannelVariableAsInt    = FakeChannelInt;
    f.getEncodeConfigValue       = FakeEncodeConfig;
    f.showHotkeySetup            = [] { std::printf("  [ts3] showHotkeySetup()\n"); };
    f.getCurrentServerConnectionHandlerID = [] { return static_cast<uint64>(1); };
    setFn(f);
    int r = init();
    std::printf("init() = %d\n", r);
    if (regId) regId("test-plugin-id");
    if (menus) {
        PluginMenuItem** items = nullptr;
        char*            icon  = nullptr;
        menus(&items, &icon);
        for (int i = 0; items && items[i]; ++i) std::printf("  menu %d: %s\n", items[i]->id, items[i]->text);
        if (freeMem) {
            for (int i = 0; items && items[i]; ++i) freeMem(items[i]);
            freeMem(items);
            if (icon) freeMem(icon);
        }
    }
    // Hotkeys: Liste wie TS3 abfragen, Ereignisse ausloesen (micmute zweimal -> Chat-Zeilen, sound1 bei leerem Slot und ein unbekanntes Schluesselwort schaden nicht)
    {
        auto hot   = Sym<void (*)(PluginHotkey***)>(m, "ts3plugin_initHotkeys");
        auto onHot = Sym<void (*)(const char*)>(m, "ts3plugin_onHotkeyEvent");
        if (hot) {
            PluginHotkey** hk = nullptr;
            hot(&hk);
            int n = 0;
            for (; hk && hk[n]; ++n) std::printf("  hotkey %-14s %s\n", hk[n]->keyword, hk[n]->description);
            std::printf("%d Hotkeys\n", n);
            if (freeMem) {
                for (int i = 0; i < n; ++i) freeMem(hk[i]);
                freeMem(hk);
            }
        }
        if (onHot) {
            onHot("micmute");
            onHot("micmute");
            onHot("sound1");
            onHot("gibtsnicht");
            onHot(nullptr);
        }
    }
    short buf[960] = {};
    int   edited   = 0;
    if (edit) edit(1, buf, 960, 1, &edited);
    std::printf("Mix im Leerlauf: edited=%d\n", edited);

    // --vad <sekunden>: Musikmodus. Musik laeuft, danach Stille; Mikrofon liefert leises Rauschen. Zeigt, wann TS3-Filter aus-/eingeschaltet werden
    // und ob das plugin-eigene Gate das Rauschen waehrend der Umgehung der Sprachaktivierung kappt.
    if (argc > 3 && std::strcmp(argv[2], "--vad") == 0 && edit) {
        int seconds = std::atoi(argv[3]);
        g_ptt = std::getenv("AAM_PTT") != nullptr; // Push-to-Talk: vad aus und Eingang deaktiviert, solange keine Taste gedrueckt ist
        const bool doSwitch = std::getenv("AAM_SWITCH") != nullptr; // wie "Test starten": nach 5 s kommt der Callback von einer neuen Verbindung
        timeBeginPeriod(1);
        g_t0 = timeGetTime();
        int peakEngaged = 0, peakRestored = 0, blocksEngaged = 0, sendOpen = 0, sendBlocks = 0;
        unsigned seed = 7;
        for (int n = 0; timeGetTime() - g_t0 < static_cast<DWORD>(seconds * 1000); ++n) {
            short mic[960];
            for (short& v : mic) { seed = seed * 1664525u + 1013904223u; v = static_cast<short>((seed >> 16) % 61) - 30; } // ca. -60 dBFS Rauschen
            const DWORD t   = timeGetTime() - g_t0;
            const uint64 sch = (doSwitch && t > 5000) ? 2 : 1;
            int ed = (Pre(sch)["vad"] == "false" && Input(sch) == 0) ? 2 : 0; // nachgestelltes TS3: sendet nur bei Dauersenden, weil niemand spricht
            edit(sch, mic, 960, 1, &ed);
            if (t > 7000 && t < 8000) { sendOpen += (ed & 2) ? 1 : 0; ++sendBlocks; }
            int peak = 0;
            for (short v : mic) peak = std::max(peak, std::abs(static_cast<int>(v)));
            if (t > 9000 && Pre(sch)["vad"] == "false") { peakEngaged = std::max(peakEngaged, peak); ++blocksEngaged; } // Musik ist vorbei, Filter noch umgangen
            if (t > 9000 && Pre(sch)["vad"] == "true") peakRestored = std::max(peakRestored, peak);
            DWORD due = g_t0 + (n + 1) * 20, now = timeGetTime();
            if (due > now) Sleep(due - now);
        }
        timeEndPeriod(1);
        std::printf("Rauschen am Mikro nach dem Ende der Musik: Spitze %d waehrend die Filter umgangen waren (%d Bloecke, Gate zu), Spitze %d nach der Wiederherstellung\n",
                    peakEngaged, blocksEngaged, peakRestored);
        for (uint64 sch : {1ULL, 2ULL})
            std::printf("Endzustand schid %llu: vad=%s agc=%s denoise=%s Eingang=%s\n", static_cast<unsigned long long>(sch), Pre(sch)["vad"].c_str(), Pre(sch)["agc"].c_str(), Pre(sch)["denoise"].c_str(),
                        Input(sch) ? "deaktiviert" : "aktiv");
        std::printf("Sendefreigabe 2 s nach dem Wechsel der Verbindung: %d von %d Bloecken\n", sendOpen, sendBlocks);
    }

    // --toggle: Plugin-Menue "Ein/Aus" zweimal ausloesen -> zeigt die Chat-Meldungen (BBCode)
    if (argc > 2 && std::strcmp(argv[2], "--toggle") == 0) {
        auto onMenu = Sym<void (*)(uint64, int, int, uint64)>(m, "ts3plugin_onMenuItemEvent");
        if (onMenu) {
            onMenu(1, 0, 1, 0);
            Sleep(200);
            onMenu(1, 0, 1, 0);
            Sleep(200);
        }
    }

    // --ui <ausgabe.bmp>: oeffnet das Einstellungsfenster, laesst es 3 s laufen (Mikrofon-Callback aktiv) und speichert einen Screenshot.
    if (argc > 3 && std::strcmp(argv[2], "--ui") == 0) {
        auto configure = Sym<void (*)(void*, void*)>(m, "ts3plugin_configure");
        if (configure) configure(nullptr, nullptr);
        std::atomic<bool>  micStop{false}; // Mikrofon-Callback laeuft im Hintergrund weiter (Pegel, Spektrum, Tonspur)
        std::atomic<bool>  record{false};
        std::vector<short> recorded; // nur der Mikrofon-Thread schreibt; gelesen wird erst nach join()
        timeBeginPeriod(1);
        std::thread micThread([&] {
            const DWORD t0 = timeGetTime();
            for (int n = 0; !micStop; ++n) {
                short mic[960] = {};
                int   ed       = 0;
                if (edit) edit(1, mic, 960, 1, &ed);
                if (record) recorded.insert(recorded.end(), mic, mic + 960);
                const DWORD due = t0 + (n + 1) * 20, now = timeGetTime();
                if (due > now) Sleep(due - now);
            }
        });
        struct Find { HWND hwnd = nullptr; } found; // nur Fenster dieses Prozesses (ein laufendes TS3 hat evtl. dieselbe Fensterklasse)
        auto findWindow = [&] {
            found.hwnd = nullptr;
            EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                wchar_t cls[64] = {};
                GetClassNameW(h, cls, 64);
                if (pid == GetCurrentProcessId() && std::wcscmp(cls, L"VoiceTuneConfig") == 0) reinterpret_cast<Find*>(lp)->hwnd = h;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&found));
            return found.hwnd;
        };
        // Das Testfenster erscheint auf dem Bildschirm des Nutzers: fuer echte Mausklicks/Tasten sperren, damit nur die simulierten Nachrichten wirken.
        for (int i = 0; i < 60 && !findWindow(); ++i) Sleep(50);
        if (found.hwnd && !std::getenv("AAM_LIVE")) EnableWindow(found.hwnd, FALSE); // AAM_LIVE=<Sekunden>: Fenster bedienbar lassen (zum Ausprobieren)
        Sleep(5500);
        findWindow();
        if (HWND w = found.hwnd) {
            RECT rc;
            GetWindowRect(w, &rc);
            RECT cl;
            GetClientRect(w, &cl);
            std::printf("Fenster %ld,%ld %ldx%ld  Client %ldx%ld  iconic=%d visible=%d  Bildschirm %dx%d\n", rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                        cl.right, cl.bottom, IsIconic(w), IsWindowVisible(w), GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
            auto shoot = [&](const std::string& path) {
                int     cw = rc.right - rc.left, ch = rc.bottom - rc.top;
                HDC     sdc = GetDC(nullptr), mdc = CreateCompatibleDC(sdc);
                HBITMAP bmp = CreateCompatibleBitmap(sdc, cw, ch);
                SelectObject(mdc, bmp);
                PrintWindow(w, mdc, 2 /*PW_RENDERFULLCONTENT*/);
                BITMAPINFOHEADER bi{sizeof(bi), cw, -ch, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
                std::vector<unsigned char> px(static_cast<size_t>(cw) * ch * 4);
                GetDIBits(mdc, bmp, 0, ch, px.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
                BITMAPFILEHEADER fh{0x4D42, static_cast<DWORD>(sizeof(fh) + sizeof(bi) + px.size()), 0, 0, sizeof(fh) + sizeof(bi)};
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    std::fwrite(&fh, sizeof(fh), 1, f);
                    std::fwrite(&bi, sizeof(bi), 1, f);
                    std::fwrite(px.data(), 1, px.size(), f);
                    std::fclose(f);
                }
                DeleteObject(bmp);
                DeleteDC(mdc);
                ReleaseDC(nullptr, sdc);
                std::printf("Screenshot %dx%d -> %s\n", cw, ch, path.c_str());
            };
            shoot(argv[3]);

            // Nur "Dateien": Drag & Drop simulieren (WM_DROPFILES wie vom Explorer), Mikrofon-Ausgabe aufzeichnen, Bedienung pruefen
            if (const char* fdir = std::getenv("AAM_FILES")) {
                auto wide = [](const std::string& str) { return std::wstring(str.begin(), str.end()); };
                auto click = [&](int x, int y) {
                    SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                    SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                    SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                };
                auto drop = [&](const std::vector<std::wstring>& paths) {
                    std::vector<wchar_t> list;
                    for (const auto& pth : paths) {
                        list.insert(list.end(), pth.begin(), pth.end());
                        list.push_back(L'\0');
                    }
                    list.push_back(L'\0');
                    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DropFilesHeader) + list.size() * sizeof(wchar_t));
                    auto*   df  = static_cast<DropFilesHeader*>(GlobalLock(mem));
                    df->pFiles  = sizeof(DropFilesHeader);
                    df->fWide   = TRUE;
                    std::memcpy(reinterpret_cast<char*>(df) + sizeof(DropFilesHeader), list.data(), list.size() * sizeof(wchar_t));
                    GlobalUnlock(mem);
                    SendMessageW(w, WM_DROPFILES, reinterpret_cast<WPARAM>(mem), 0);
                };
                const std::wstring d = wide(fdir);
                record               = true;
                const DWORD t0       = timeGetTime();
                std::printf("Drop: tone.wav, notes.txt (keine Audio-Datei), tone.mp3\n");
                drop({d + L"\\tone.wav", d + L"\\notes.txt", d + L"\\tone.mp3"});
                Sleep(2000);
                shoot(std::string(argv[3]) + "f1.bmp"); // Datei 1 laeuft
                click(360, 253);                         // "Wiederholen: Aus" -> "Alle"
                SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(300, 395));
                while (timeGetTime() - t0 < 7500) Sleep(20);
                shoot(std::string(argv[3]) + "f2.bmp"); // Datei 2 (nahtloser Uebergang)
                click(121, 253);                         // Pause
                Sleep(800);
                shoot(std::string(argv[3]) + "f3.bmp");
                click(121, 253);                         // weiter
                click(32 + 756 / 4, 205);                // Springen auf ein Viertel (beim Loslassen)
                Sleep(1500);
                click(241, 253);                         // "Weiter": naechster Titel
                Sleep(1200);
                shoot(std::string(argv[3]) + "f4.bmp");
                click(187, 253);                         // Stopp
                Sleep(1500);
                record = false;
                shoot(std::string(argv[3]) + "f5.bmp");
                click(400, 348);                         // erste Zeile antippen: spielt sie
                Sleep(800);
                click(765, 388);                         // x-Knopf der zweiten Zeile: entfernen
                Sleep(500);
                shoot(std::string(argv[3]) + "f6.bmp");
                click(506, 253);                         // Liste leeren -> Ablageflaeche
                Sleep(500);
                shoot(std::string(argv[3]) + "f7.bmp");
                micStop = true;
                micThread.join();
                timeEndPeriod(1);
                shut();
                if (const char* outRaw = std::getenv("AAM_RECORD")) {
                    if (FILE* f = std::fopen(outRaw, "wb")) {
                        std::fwrite(recorded.data(), 2, recorded.size(), f);
                        std::fclose(f);
                    }
                }
                std::printf("Aufgezeichnet: %.1f s\n", recorded.size() / 48000.0);
                return 0;
            }

            // Einstellungsseite durchklicken (AAM_SET=1): alle Reiter fotografieren (DE und EN, "Erweitert" zu und offen), jedes Element der Seite
            // "Einstellungen" bedienen und die gespeicherten Werte in %TEMP%\voicetune.ini pruefen. Bilder set_*.bmp.
            if (std::getenv("AAM_SET")) {
                int fails = 0;
                auto click = [&](int x, int y) {
                    SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                    SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                    SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                };
                char tmp[MAX_PATH];
                GetEnvironmentVariableA("TEMP", tmp, MAX_PATH);
                const std::string ini  = std::string(tmp) + "\\voicetune.ini";
                auto              val  = [&](const char* key, int def) { return static_cast<int>(GetPrivateProfileIntA("VoiceTune", key, def, ini.c_str())); };
                auto              check = [&](bool ok, const char* what) {
                    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
                    if (!ok) ++fails;
                };
                const int gearX = 760, gearY = 36; // Zahnrad neben dem Status-Schild
                const std::string base = argv[3];
                auto shotPages = [&](const char* prefix) {
                    for (int i = 0; i < 4; ++i) {
                        click(44, 126 + i * 76);  // Seitenleiste
                        Sleep(700);
                        shoot(base + prefix + std::to_string(i) + ".bmp");
                    }
                };

                shotPages("set_de");                       // alle 4 Seiten, Deutsch
                click(gearX, gearY);                       // Zahnrad: Einstellungs-Popup auf
                Sleep(500);
                shoot(base + "set_de4.bmp");               // Popup, "Erweitert" zu
                click(498, 450);                           // "Erweitert" aufklappen
                Sleep(500);
                shoot(base + "set_deadv.bmp");

                // Jedes Element bedienen
                click(298, 152);                           // Sprache: Deutsch
                click(178, 152);                            // Sprache: Automatisch
                click(418, 152);                           // Sprache: English
                click(298, 235);                           // Hotkeys festlegen ...
                click(448, 335);                           // Titel melden: eigener Chat aus
                click(448, 363);                           // Kanal-Chat an
                click(448, 391);                           // Client-Metadaten an
                click(848, 121);                           // Musik immer senden aus
                click(848, 171);                           // Mono an
                click(578, 246);                           // Qualitaet: niedrige Latenz
                click(448, 495);                           // Musikmodus aus
                click(288, 545);                           // Mikro-Gate irgendwo in der Mitte
                click(848, 495);                           // Opus optimieren aus
                click(848, 545);                           // Opus-Vorschau aus
                Sleep(1800);                               // Einstellungen werden im Sekundentakt gespeichert
                check(val("Language", -1) == 2, "Sprache English gespeichert");
                check(val("NowPlayingChat", -1) == 0 && val("NowPlayingChannel", -1) == 1 && val("NowPlayingMeta", -1) == 1, "Titel melden: Chat aus, Kanal an, Metadaten an");
                check(val("AlwaysSend", -1) == 0, "Musik immer senden aus");
                check(val("Mono", -1) == 1, "Mono an");
                check(val("Quality", -1) == 0, "Qualitaet niedrige Latenz");
                check(val("MusicMode", -1) == 0, "Musikmodus aus");
                check(val("GateDb", 0) > -70 && val("GateDb", 0) < -25 && val("GateDb", 0) != -50, "Mikro-Gate verstellt");
                check(val("OptimizeForOpus", -1) == 0, "Opus optimieren aus");
                check(val("OpusPreview", -1) == 0, "Opus-Vorschau aus");
                check(val("AdvancedOpen", -1) == 1, "Erweitert aufgeklappt gespeichert");

                shoot(base + "set_enadv.bmp");             // Englisch, aufgeklappt, alles umgestellt
                click(498, 450);                           // Erweitert zuklappen
                Sleep(1400);
                check(val("AdvancedOpen", -1) == 0, "Erweitert zugeklappt gespeichert");
                click(400, 612);                            // Klick neben das Panel schliesst das Popup
                Sleep(300);
                shoot(base + "set_enclosed.bmp");          // zu: Hauptseite wieder sichtbar und bedienbar
                shotPages("set_en");                       // alle 4 Seiten, English
                click(gearX, gearY);                       // Popup wieder auf
                click(178, 152);                            // zurueck auf Automatisch (die Einstellungen landen nur in %TEMP%)
                Sleep(500);
                std::printf("UI-Pruefung: %d Fehler\n", fails);
                micStop = true;
                micThread.join();
                timeEndPeriod(1);
                shut();
                return fails ? 1 : 0;
            }

            // Zum Ausprobieren: Fenster N Sekunden offen und bedienbar lassen (AAM_LIVE=45), danach beenden
            if (const char* live = std::getenv("AAM_LIVE")) {
                std::printf("Fenster offen fuer %s s\n", live);
                if (std::getenv("AAM_OPENSET")) { // Zahnrad klicken: Einstellungs-Popup gleich offen
                    SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(760, 36));
                    SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(760, 36));
                    SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(760, 36));
                }
                Sleep(static_cast<DWORD>(std::atoi(live)) * 1000);
                micStop = true;
                micThread.join();
                timeEndPeriod(1);
                shut();
                return 0;
            }

            // Profile, Titel melden und Soundboard bedienen (AAM_UI3=<ordner mit tone.wav/tone.mp3>); Screenshots p*.bmp (Seite 0) und s*.bmp (Sounds)
            if (const char* sdir = std::getenv("AAM_UI3")) {
                auto click = [&](int x, int y) {
                    SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                    SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                    SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                };
                auto rclick = [&](int x, int y) {
                    SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                    SendMessageW(w, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(x, y));
                    SendMessageW(w, WM_RBUTTONUP, 0, MAKELPARAM(x, y));
                };
                auto type  = [&](const wchar_t* text) { for (; *text; ++text) SendMessageW(w, WM_CHAR, *text, 0); };
                auto enter = [&] { SendMessageW(w, WM_CHAR, 13, 0); };
                auto drop  = [&](const std::vector<std::wstring>& paths, int px, int py) {
                    std::vector<wchar_t> list;
                    for (const auto& pth : paths) {
                        list.insert(list.end(), pth.begin(), pth.end());
                        list.push_back(L'\0');
                    }
                    list.push_back(L'\0');
                    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DropFilesHeader) + list.size() * sizeof(wchar_t));
                    auto*   df  = static_cast<DropFilesHeader*>(GlobalLock(mem));
                    df->pFiles  = sizeof(DropFilesHeader);
                    df->pt.x    = px;
                    df->pt.y    = py;
                    df->fWide   = TRUE;
                    std::memcpy(reinterpret_cast<char*>(df) + sizeof(DropFilesHeader), list.data(), list.size() * sizeof(wchar_t));
                    GlobalUnlock(mem);
                    SendMessageW(w, WM_DROPFILES, reinterpret_cast<WPARAM>(mem), 0);
                };
                const std::string  base = argv[3];
                const std::wstring d    = std::wstring(sdir, sdir + std::strlen(sdir));
                // Seite 0: Profil speichern (Namenseingabe), Wert aendern -> "(geaendert)"
                click(594, 561);                         // "Speichern als ..."
                type(L"Abend");
                Sleep(400);
                shoot(base + "p1.bmp");                  // Eingabe offen
                enter();
                Sleep(400);
                shoot(base + "p2.bmp");                  // gespeichert, Meldung in der Fusszeile
                click(213, 483);                         // Musik-Lautstaerke aendern
                Sleep(500);
                shoot(base + "p3.bmp");                  // Profil "(geaendert)"
                // Seite Sounds
                click(44, 354);
                drop({d + L"\\tone.wav", d + L"\\tone.mp3"}, 0, 0);
                Sleep(1800);
                shoot(base + "s1.bmp");                  // zwei Kacheln belegt
                click(213, 217);                         // Kachel 1 spielen
                Sleep(150);
                shoot(base + "s2.bmp");                  // Aufleuchten
                rclick(403, 217);                        // Menue an Kachel 2
                SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(419, 239));
                shoot(base + "s3.bmp");
                click(419, 239);                         // "Umbenennen"
                type(L"Hupe laut");
                Sleep(300);
                shoot(base + "s4.bmp");                  // Namenseingabe
                enter();
                Sleep(400);
                shoot(base + "s5.bmp");
                click(44, 126);                          // zurueck auf Seite 0
                Sleep(400);
                shoot(base + "p4.bmp");
                if (const char* hold = std::getenv("AAM_HOLD")) Sleep(static_cast<DWORD>(std::atoi(hold)) * 1000);
                micStop = true;
                micThread.join();
                timeEndPeriod(1);
                shut();
                return 0;
            }

            // Bedienung simulieren: Musik-Regler auf ~50 % ziehen, Hauptschalter und "immer senden" umlegen, App-Zeile waehlen.
            auto click = [&](int x, int y) {
                SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            };
            // Seite "Quelle & Mix": Musik-Regler auf 50 %, Verzoegerung ~500 ms, Schalter, erste App-Kachel
            SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(125, 595));
            SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(125, 595));
            SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(432 + 8 + 85, 575));
            SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(432 + 8 + 85, 575));
            click(780, 162); // Hauptschalter
            click(760, 512); // Mithoeren
            click(760, 624); // Musik immer senden
            click(80, 290);  // erste Kachel
            Sleep(1200);
            shoot(std::string(argv[3]) + "2.bmp");

            // Apps ausblenden / einblenden per Rechtsklick
            auto rclick = [&](int x, int y) {
                SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                SendMessageW(w, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(x, y));
                SendMessageW(w, WM_RBUTTONUP, 0, MAKELPARAM(x, y));
            };
            rclick(200, 290);                                   // zweite Kachel -> Menue
            SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(230, 312));
            shoot(std::string(argv[3]) + "2a.bmp");             // Menue offen
            click(230, 312);                                    // "... ausblenden"
            Sleep(900);                                         // Kacheln gleiten nach
            shoot(std::string(argv[3]) + "2b.bmp");             // eine App weniger, "1 ausgeblendet"
            rclick(740, 410);                                   // ins Leere -> "Alle Apps einblenden (1)"
            click(760, 432);
            Sleep(2200);                                        // naechste Live-Aktualisierung bringt sie zurueck
            shoot(std::string(argv[3]) + "2c.bmp");

            // Seite "Equalizer": 61 Baender, Preset Loudness, Kurve von Hand zeichnen, Qualitaet "Stabil", dann "Einstellungen anhoeren" halten
            click(250, 106);              // Tab 2
            click(288, 155);              // 61 Baender
            auto dump = [&](const char* label) { // Diagnose: wichtige INI-Werte nach einem Schritt
                Sleep(1300);
                char path[MAX_PATH];
                GetEnvironmentVariableA("TEMP", path, MAX_PATH);
                std::string ini = std::string(path) + "\\voicetune.ini";
                char        e[16], q[16];
                GetPrivateProfileStringA("VoiceTune", "Enabled", "?", e, 16, ini.c_str());
                GetPrivateProfileStringA("VoiceTune", "EqEnabled", "?", q, 16, ini.c_str());
                std::printf("  [%s] Enabled=%s EqEnabled=%s\n", label, e, q);
            };
            dump("nach Seite 1");
            auto type = [&](const wchar_t* text) { for (; *text; ++text) SendMessageW(w, WM_CHAR, *text, 0); };
            auto enter = [&] { SendMessageW(w, WM_CHAR, 13, 0); };
            auto drawCurve = [&](int y0, int slope) {
                SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(110, y0));
                for (int x = 110; x <= 330; x += 6) SendMessageW(w, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y0 - (x - 110) / slope));
                SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(330, y0 - 220 / slope));
            };
            // Preset-Feld: Dropdown oeffnen, "Loudness" waehlen (Liste: Kopf 24 px, dann je 28 px), Kurve veraendern -> "(geaendert)"
            dump("nach Tab+61");
            click(150, 192);              // Dropdown auf
            click(150, 364);              // Eintrag 5 = Loudness
            dump("nach Preset");
            drawCurve(330, 3);
            shoot(std::string(argv[3]) + "3.bmp");
            // Eigenen Filter speichern: "Speichern als ..." -> Namen tippen -> Enter
            click(406, 192);
            type(L"Mein Bass");
            shoot(std::string(argv[3]) + "4.bmp");   // Eingabe-Zustand
            enter();
            click(150, 192);              // Dropdown auf: zeigt jetzt "Eigene Filter"
            SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(150, 364 + 28 * 3));
            shoot(std::string(argv[3]) + "5.bmp");
            SendMessageW(w, WM_KEYDOWN, VK_ESCAPE, 0);
            // Umbenennen (Eingabe ist vorbelegt und markiert: Tippen ersetzt sie), danach einen zweiten Filter anlegen und wieder loeschen
            click(530, 192);
            type(L"Warmer Bass");
            enter();
            drawCurve(300, 4);
            click(406, 192);              // "Speichern als ..." mit Vorschlag "Eigener Filter 1"
            enter();
            click(652, 192);              // Loeschen: erster Klick scharf schalten
            shoot(std::string(argv[3]) + "6.bmp");
            click(652, 192);              // zweiter Klick loescht
            Sleep(300);
            // Fehlerfaelle: reservierter Name
            click(406, 192);
            type(L"flach");
            enter();
            shoot(std::string(argv[3]) + "7.bmp");
            SendMessageW(w, WM_KEYDOWN, VK_ESCAPE, 0);
            click(500, 651);              // Qualitaet "Stabil"
            SendMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(500, 330)); // Hover-Info
            SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(678, 479)); // "Einstellungen anhoeren" halten
            Sleep(800);
            SendMessageW(w, WM_LBUTTONUP, 0, MAKELPARAM(678, 479));
            click(530, 479);                                    // "Dauerhaft"-Schalter: bleibt an, ohne zu halten
            Sleep(900);
            shoot(std::string(argv[3]) + "8.bmp");
            click(530, 479);                                    // wieder aus
        } else {
            std::printf("Fenster nicht gefunden\n");
        }
        micStop = true;
        micThread.join();
    }

    // --monitor <sekunden>: Mikrofon-Callback (Hauptthread) und Wiedergabe-Callback (eigener Thread, Stereo) laufen parallel wie in TS3.
    // Misst, wann die Musik im Mikrofonsignal und wann sie in der Wiedergabe (Mithoeren) zum ersten Mal auftaucht -> Differenz = Verzoegerung.
    if (argc > 3 && std::strcmp(argv[2], "--monitor") == 0 && edit) {
        auto playback = Sym<void (*)(uint64, short*, int, int, const unsigned int*, unsigned int*)>(m, "ts3plugin_onEditMixedPlaybackVoiceDataEvent");
        int   seconds = std::atoi(argv[3]);
        std::atomic<bool> stop{false};
        std::atomic<int>  micFirst{-1}, playFirst{-1}, playBlocks{0}, playLoud{0};
        std::atomic<int>  playPeak{0};
        timeBeginPeriod(1);
        DWORD t0 = timeGetTime();
        std::thread player([&] {
            for (int n = 0; !stop; ++n) {
                short out[960 * 2] = {};
                unsigned int spk[2] = {0x1, 0x2}, fill = 0;
                if (playback) playback(1, out, 960, 2, spk, &fill);
                int peak = 0;
                for (short s : out) peak = std::max(peak, std::abs(static_cast<int>(s)));
                if (peak > 50 && playFirst < 0) playFirst = static_cast<int>(timeGetTime() - t0);
                if (peak > 50) ++playLoud;
                playPeak = std::max<int>(playPeak, peak);
                ++playBlocks;
                DWORD due = t0 + (n + 1) * 20, now = timeGetTime();
                if (due > now) Sleep(due - now);
            }
        });
        for (int n = 0; timeGetTime() - t0 < static_cast<DWORD>(seconds * 1000); ++n) {
            short mic[960] = {};
            int   ed       = 0;
            edit(1, mic, 960, 1, &ed);
            int peak = 0;
            for (short s : mic) peak = std::max(peak, std::abs(static_cast<int>(s)));
            if (peak > 50 && micFirst < 0) micFirst = static_cast<int>(timeGetTime() - t0);
            DWORD due = t0 + (n + 1) * 20, now = timeGetTime();
            if (due > now) Sleep(due - now);
        }
        stop = true;
        player.join();
        timeEndPeriod(1);
        std::printf("Mikrofon: Musik ab %d ms | Wiedergabe: Musik ab %d ms (%d von %d Bloecken laut, Spitze %d) | Verzoegerung = %d ms\n", micFirst.load(), playFirst.load(),
                    playLoud.load(), playBlocks.load(), playPeak.load(), playFirst.load() - micFirst.load());
        Sleep(500);
    }

    // --mix <sekunden> <ausgabe.raw>: ruft den Mikrofon-Callback alle 20 ms auf (Mono, 960 Frames) und speichert die Ausgabe (int16).
    if (argc > 3 && std::strcmp(argv[2], "--mix") == 0 && edit) {
        int   seconds = std::atoi(argv[3]);
        FILE* out     = argc > 4 ? std::fopen(argv[4], "wb") : nullptr;
        timeBeginPeriod(1);
        int   sendBlocks = 0, blocks = 0;
        DWORD start = timeGetTime();
        for (int n = 0; timeGetTime() - start < static_cast<DWORD>(seconds * 1000); ++n) {
            short mic[960] = {};
            int   ed       = 0;
            edit(1, mic, 960, 1, &ed);
            if (ed & 2) ++sendBlocks;
            ++blocks;
            if (out) std::fwrite(mic, 2, 960, out);
            DWORD due = start + (n + 1) * 20;
            DWORD now = timeGetTime();
            if (due > now) Sleep(due - now);
        }
        timeEndPeriod(1);
        if (out) std::fclose(out);
        std::printf("Mix-Schleife: %d Bloecke, davon %d mit Sende-Bit\n", blocks, sendBlocks);
        Sleep(6000); // Statistik-Zeile abwarten
    }
    Sleep(500);
    shut();
    std::printf("shutdown ok\n");
    return r;
}
