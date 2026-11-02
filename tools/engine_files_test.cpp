// Testet den Datei-Player der Engine ohne Fenster (Echtzeit, Mikrofon-Callback im 20-ms-Takt) und prueft das Ergebnis automatisch.
// Aufruf: engine_files_test.exe <ordner-mit-tone.wav-und-tone.mp3> [ausgabe.raw]     (tone.*: 3 s 440 Hz, danach 3 s 880 Hz)
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static Engine&            en = Engine::Get();
static int                g_micNoise = 0; // Amplitude des simulierten Mikrofonsignals (Rauschen), 0 = Stille
static std::vector<short> g_all; // alles, was der Mikrofon-Callback geliefert hat

// Echtzeit-Schleife wie der Mikrofon-Callback von TeamSpeak (20 ms, Mono). Rueckgabe: Index des ersten Samples dieses Abschnitts in g_all.
static size_t RunFor(DWORD ms)
{
    const size_t first = g_all.size();
    const DWORD  t0    = timeGetTime();
    for (int n = 0; timeGetTime() - t0 < ms; ++n) {
        short mic[960] = {};
        if (g_micNoise)
            for (int i = 0; i < 960; ++i) mic[i] = static_cast<short>((i * 7919 % 2001 - 1000) * g_micNoise / 1000); // Rauschen
        int ed = 0;
        en.Mix(1, mic, 960, 1, &ed);
        g_all.insert(g_all.end(), mic, mic + 960);
        const DWORD due = t0 + (n + 1) * 20, now = timeGetTime();
        if (due > now) Sleep(due - now);
    }
    return first;
}

static double Freq(size_t from, double sec, double dur = 0.4)
{
    const size_t i0 = from + static_cast<size_t>(sec * 48000), n = static_cast<size_t>(dur * 48000);
    if (i0 + n > g_all.size()) return -1;
    int c = 0;
    for (size_t i = i0 + 1; i < i0 + n; ++i)
        if (g_all[i - 1] < 0 && g_all[i] >= 0) ++c;
    return c / dur;
}

static int Peak(size_t from, double sec, double dur = 0.2)
{
    const size_t i0 = from + static_cast<size_t>(sec * 48000), n = static_cast<size_t>(dur * 48000);
    int          p  = 0;
    for (size_t i = i0; i < i0 + n && i < g_all.size(); ++i) p = std::max(p, std::abs(static_cast<int>(g_all[i])));
    return p;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    auto              wide = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };
    const std::wstring wav = wide(dir + "\\tone.wav"), mp3 = wide(dir + "\\tone.mp3"), bogus = wide(dir + "\\gibt-es-nicht.mp3");

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring cfg = std::wstring(tmp) + L"aam_engine_test";
    CreateDirectoryW(cfg.c_str(), nullptr);
    DeleteFileW((cfg + L"\\voicetune.ini").c_str());
    DeleteFileW((cfg + L"\\appaudiomixer.ini").c_str());

    en.SetLogger([](const char*) {});
    en.SetEncodeCallback([](uint64_t, EncodeInfo* e) { e->channels = 1; e->bitrateBps = 32000; e->exact = true; return true; }); // wie Opus Voice mit 32 kbit/s
    en.Init(cfg);
    timeBeginPeriod(1);

    // ---- 1. Zwei Dateien hintereinander: Takt, Frequenzen, nahtloser Uebergang -----------------------------------------------------------
    CHECK(en.PlayerAdd({wav, mp3}) == 2, "zwei Dateien hinzufuegen");
    en.PlayerPlay(0);
    const size_t s1 = RunFor(1000);
    PlayerSnapshot ps = en.GetPlayer();
    CHECK(ps.active && !ps.paused && ps.current == 0, "laeuft: active=%d", ps.active);
    CHECK(std::fabs(ps.position - 1.0) < 0.4, "Position nach 1 s: %.2f", ps.position);
    RunFor(13000);
    const size_t start = [&] { // Beginn der Musik
        for (size_t i = s1; i < g_all.size(); ++i)
            if (std::abs(g_all[i]) > 300) return i - s1;
        return size_t{0};
    }();
    const double t0 = static_cast<double>(start) / 48000.0;
    std::printf("Musik beginnt bei %.2f s\n", t0);
    for (double t : {0.6, 1.6, 2.4}) CHECK(std::fabs(Freq(s1, t0 + t) - 440) < 8, "Datei 1 bei %.1f s: %.0f Hz", t, Freq(s1, t0 + t));
    for (double t : {3.6, 4.6, 5.4}) CHECK(std::fabs(Freq(s1, t0 + t) - 880) < 12, "Datei 1 bei %.1f s: %.0f Hz", t, Freq(s1, t0 + t));
    for (double t : {6.7, 7.6, 8.4}) CHECK(std::fabs(Freq(s1, t0 + t) - 440) < 8, "Datei 2 bei %.1f s: %.0f Hz", t, Freq(s1, t0 + t));
    for (double t : {9.7, 10.6, 11.4}) CHECK(std::fabs(Freq(s1, t0 + t) - 880) < 12, "Datei 2 bei %.1f s: %.0f Hz", t, Freq(s1, t0 + t));
    // keine Luecke zwischen den Dateien und im Verlauf (Nullstrecke > 50 ms)
    {
        size_t run = 0, longest = 0;
        for (size_t i = s1 + start; i < s1 + start + static_cast<size_t>(11.5 * 48000) && i < g_all.size(); ++i) {
            run = std::abs(g_all[i]) <= 3 ? run + 1 : 0;
            longest = std::max(longest, run);
        }
        std::printf("laengste Stille im Verlauf: %.0f ms\n", longest / 48.0);
        CHECK(longest < 48 * 50, "Luecke %.0f ms", longest / 48.0);
    }
    size_t last = 0;
    for (size_t i = s1; i < g_all.size(); ++i)
        if (std::abs(g_all[i]) > 300) last = i - s1;
    CHECK(std::fabs(last / 48000.0 - t0 - 12.2) < 0.5, "Musik endet bei %.2f s", last / 48000.0);
    ps = en.GetPlayer();
    CHECK(!ps.active, "nach der Liste ist der Player aus");

    // ---- 2. Pause / Weiter, Springen, Springen nach dem Ende, Zurueck ---------------------------------------------------------------------
    en.PlayerClear();
    en.PlayerAdd({wav});
    en.PlayerPlay(0);
    RunFor(1500);
    en.PlayerTogglePause();
    const size_t sp  = RunFor(2000);
    const double pos0 = en.GetPlayer().position;
    CHECK(en.GetPlayer().paused, "pausiert");
    CHECK(Peak(sp, 1.4, 0.5) < 60, "waehrend der Pause bleibt es still (Spitze %d)", Peak(sp, 1.4, 0.5));
    RunFor(500);
    CHECK(std::fabs(en.GetPlayer().position - pos0) < 0.15, "Position haelt waehrend der Pause");
    en.PlayerTogglePause();
    const size_t sr = RunFor(1500);
    CHECK(!en.GetPlayer().paused && Peak(sr, 0.8, 0.5) > 300, "nach der Pause geht es weiter");

    en.PlayerSeek(4.0);
    const size_t sk = RunFor(1500);
    CHECK(std::fabs(Freq(sk, 0.8) - 880) < 15, "nach dem Springen auf 4,0 s: %.0f Hz", Freq(sk, 0.8));
    CHECK(en.GetPlayer().position > 4.8 && en.GetPlayer().position < 5.9, "Position nach dem Springen: %.2f", en.GetPlayer().position);

    RunFor(3500); // Dateiende + Puffer leeren
    CHECK(!en.GetPlayer().active, "Datei zu Ende");
    en.PlayerSeek(1.0); // Springen nach dem Ende startet an der Stelle neu
    const size_t se = RunFor(1500);
    CHECK(en.GetPlayer().active && Peak(se, 0.9, 0.4) > 300, "Springen nach dem Ende startet neu");
    en.PlayerSeek(3.2); // in den 880-Hz-Teil, damit "Zurueck" (>3 s) nach vorn springt
    RunFor(3500);
    en.PlayerPrev();
    RunFor(800);
    CHECK(en.GetPlayer().position < 1.6, "Zurueck nach >3 s springt an den Anfang (Position %.2f)", en.GetPlayer().position);

    // ---- 3. Entfernen des laufenden Titels, defekte Datei, Hauptschalter aus ------------------------------------------------------------------------
    en.PlayerClear();
    en.PlayerAdd({wav, mp3});
    en.PlayerPlay(0);
    RunFor(800);
    en.PlayerRemove(0);
    RunFor(800);
    ps = en.GetPlayer();
    CHECK(ps.items.size() == 1 && ps.current == 0 && ps.active && ps.items[0].title == L"tone", "laufenden Titel entfernt: der naechste rueckt nach (%zu)", ps.items.size());

    en.PlayerClear();
    en.PlayerAdd({bogus, wav});
    en.PlayerPlay(0);
    RunFor(1200);
    ps = en.GetPlayer();
    CHECK(ps.active && ps.current == 1, "defekte Datei wird uebersprungen (Titel %d)", ps.current);
    CHECK(ps.message.find(L"Nicht abspielbar") != std::wstring::npos, "Meldung zur defekten Datei");

    en.PlayerClear();
    en.PlayerAdd({bogus, bogus});
    en.PlayerPlay(0);
    RunFor(2500);
    CHECK(!en.GetPlayer().active, "nur defekte Dateien: kein Endlos-Ueberspringen");

    en.PlayerClear();
    en.PlayerAdd({wav});
    en.PlayerPlay(0);
    RunFor(1000);
    en.SetEnabled(false); // Hauptschalter aus
    const size_t so = RunFor(1500);
    CHECK(en.GetPlayer().paused, "Hauptschalter aus pausiert die Datei");
    CHECK(Peak(so, 0.9, 0.5) < 60, "Hauptschalter aus: still (Spitze %d)", Peak(so, 0.9, 0.5));
    en.PlayerStop();

    // ---- 4. Mikrofon stumm: gilt auch ohne Musik, die Musik laeuft weiter -----------------------------------------------------------------
    en.SetEnabled(true);
    en.PlayerClear();
    g_micNoise = 3000;
    const size_t m0 = RunFor(600); // keine Quelle: Mikrofon geht unveraendert durch
    CHECK(Peak(m0, 0.2, 0.3) > 2500, "ohne Stumm kommt das Mikrofon durch (Spitze %d)", Peak(m0, 0.2, 0.3));
    en.SetMicMuted(true);
    const size_t m1 = RunFor(600);
    CHECK(en.IsMicMuted() && Peak(m1, 0.2, 0.3) == 0, "stumm ohne Musikquelle: Mikrofon ist weg (Spitze %d)", Peak(m1, 0.2, 0.3));
    en.PlayerAdd({wav});
    en.PlayerPlay(0);
    const size_t m2 = RunFor(2000);
    CHECK(Peak(m2, 1.0, 0.5) > 300 && Peak(m2, 1.0, 0.5) < 2000, "stumm mit Datei: nur die Musik (Spitze %d, ohne Rauschen)", Peak(m2, 1.0, 0.5));
    CHECK(std::fabs(Freq(m2, 1.0) - 440) < 10, "stumm mit Datei: Musik klingt sauber (%.0f Hz)", Freq(m2, 1.0));
    en.SetMicMuted(false);
    en.PlayerStop();
    g_micNoise = 0;

    // ---- 5. Opus-Vorschau beim Mithoeren und Klang-Optimierung --------------------------------------------------------------------------
    {
        en.SetOpusPreview(true);
        en.SetOptimizeForOpus(true);
        en.SetMonitorDelay(100);
        en.SetMonitor(true);
        en.PlayerClear();
        en.PlayerAdd({wav});
        en.PlayerPlay(0);
        std::vector<short> mon; // Wiedergabe (Stereo, 2 Kanaele) so, wie sie TS3 an die Lautsprecher gibt
        auto runMon = [&](DWORD ms) {
            const size_t first = mon.size() / 2; // Frame-Index
            const DWORD  t0    = timeGetTime();
            for (int n = 0; timeGetTime() - t0 < ms; ++n) {
                short mic[960] = {};
                int   ed       = 0;
                en.Mix(1, mic, 960, 1, &ed);
                short play[960 * 2] = {};
                unsigned fill = 0;
                en.MixMonitor(play, 960, 2, nullptr, &fill);
                mon.insert(mon.end(), play, play + 960 * 2);
                const DWORD due = t0 + (n + 1) * 20, now = timeGetTime();
                if (due > now) Sleep(due - now);
            }
            return first;
        };
        auto monFreq = [&](size_t from, double sec) { // Nulldurchgaenge des linken Kanals
            const size_t i0 = from + static_cast<size_t>(sec * 48000), n = 19200;
            int          c = 0;
            for (size_t i = i0 + 1; i < i0 + n && 2 * i < mon.size(); ++i)
                if (mon[2 * (i - 1)] < 0 && mon[2 * i] >= 0) ++c;
            return c / 0.4;
        };
        auto monPeak = [&](size_t from, double sec) {
            int p = 0;
            const size_t i0 = from + static_cast<size_t>(sec * 48000);
            for (size_t i = i0; i < i0 + 9600 && 2 * i + 1 < mon.size(); ++i) p = std::max(p, std::abs(static_cast<int>(mon[2 * i])));
            return p;
        };
        const size_t a = runMon(2500);
        const std::wstring info = en.OpusInfo();
        std::wprintf(L"Opus-Info: %ls\n", info.c_str());
        CHECK(info.find(L"Opus Voice") != std::wstring::npos && info.find(L"32 kbit/s") != std::wstring::npos && info.find(L"gesch") == std::wstring::npos,
              "Opus-Info nennt Codec und die von TS3 gemeldete Bitrate");
        CHECK(monPeak(a, 1.2) > 300, "Mithoeren mit Opus-Vorschau ist hoerbar (Spitze %d)", monPeak(a, 1.2));
        CHECK(std::fabs(monFreq(a, 1.2) - 440) < 15, "Mithoeren mit Opus-Vorschau: Ton bleibt 440 Hz (%.0f)", monFreq(a, 1.2));
        en.SetOpusPreview(false);
        const size_t b = runMon(2500);
        CHECK(en.OpusInfo().empty(), "Vorschau aus: keine Opus-Info");
        CHECK(monPeak(b, 1.2) > 300 && std::fabs(monFreq(b, 1.2) - 880) < 25, "Mithoeren ohne Vorschau (Datei steht bei 3,7 s: 880 Hz) (Spitze %d, %.0f Hz)", monPeak(b, 1.2), monFreq(b, 1.2));
        en.SetOptimizeForOpus(false);
        const size_t c = runMon(1500);
        CHECK(monPeak(c, 0.7) > 300, "ohne Optimierung hoerbar");
        en.SetMonitor(false);
        en.PlayerStop();
        en.SetOptimizeForOpus(true);
        en.SetOpusPreview(true);
    }

    // ---- 6. Soundboard: Clip ueber das Mikrofon, auch ohne Musikquelle ------------------------------------------------------------------------
    {
        en.SetEnabled(false);
        en.PlayerClear();
        g_micNoise = 0;
        en.SoundSetSlot(0, wav);
        for (int i = 0; i < 400 && en.GetSounds()[0].state != Soundboard::State::Ready; ++i) Sleep(20);
        const auto sounds = en.GetSounds();
        CHECK(sounds.size() == 8 && sounds[0].state == Soundboard::State::Ready && sounds[0].name == L"tone" && sounds[0].seconds > 5.5, "Slot geladen (%zu, Name '%ls')", sounds.size(), sounds[0].name.c_str());
        en.SoundPlay(0);
        CHECK(en.GetSettings().enabled, "Sound schaltet \"Musik einspielen\" ein");
        const size_t k0 = RunFor(1800);
        CHECK(Peak(k0, 0.4, 0.3) > 300 && std::fabs(Freq(k0, 0.4) - 440) < 12, "Sound ohne Musikquelle (Spitze %d, %.0f Hz)", Peak(k0, 0.4, 0.3), Freq(k0, 0.4));
        const int full = Peak(k0, 0.4, 0.4);
        en.SoundStop();
        RunFor(300);
        const size_t k1 = RunFor(500);
        CHECK(Peak(k1, 0.1, 0.3) == 0, "nach Stopp still (%d)", Peak(k1, 0.1, 0.3));

        en.SetSoundVolume(50);
        en.SoundPlay(0);
        const size_t k2 = RunFor(1500);
        const int    half = Peak(k2, 0.4, 0.4);
        CHECK(half > full * 0.4 && half < full * 0.6, "Sound-Lautstaerke 50 %%: %d gegen %d", half, full);
        en.SoundStop();
        en.SetSoundVolume(100);

        // Mit stummem Mikrofon und Mikrofonrauschen: nur der Sound
        g_micNoise = 3000;
        en.SetMicMuted(true);
        en.SoundPlay(0);
        const size_t k3 = RunFor(1500);
        CHECK(Peak(k3, 0.4, 0.4) > 300 && Peak(k3, 0.4, 0.4) < 2500 && std::fabs(Freq(k3, 0.4) - 440) < 12, "Sound bei stummem Mikro (Spitze %d, %.0f Hz)", Peak(k3, 0.4, 0.4), Freq(k3, 0.4));
        en.SoundStop();
        en.SetMicMuted(false);
        g_micNoise = 0;

        en.SoundRename(0, L"  Hupe \"laut\"  ");
        CHECK(en.GetSounds()[0].name == L"Hupe 'laut'", "Umbenennen bereinigt");
        en.SoundSetSlot(0, L"");
        CHECK(en.GetSounds()[0].state == Soundboard::State::Empty && en.GetSounds()[0].path.empty(), "Slot geleert");
        en.SoundPlay(0); // leerer Slot: nichts
        en.SoundPlay(17);
        const size_t k4 = RunFor(400);
        CHECK(Peak(k4, 0.1, 0.2) == 0, "leerer/ungueltiger Slot bleibt still");
        en.SetEnabled(true);
    }

    // ---- 7. Profile ------------------------------------------------------------------------------------------------------------------------
    {
        en.SetMusicVolume(80);
        en.SetMicVolume(90);
        en.SetQuality(2);
        en.SetMono(true);
        en.SetEqBands(61);
        en.SetEqGain(10, 6.0f);
        en.SetMonitorDelay(300);
        en.SetOptimizeForOpus(false);
        CHECK(en.SaveProfile(L"Test A") == PresetResult::Ok && en.GetSettings().profileName == L"Test A", "Profil speichern");
        const Profile saved = ProfileFromSettings(en.GetSettings());
        CHECK(SameProfile(saved, ProfileFromSettings(en.GetSettings())), "unveraendert = gleich");

        en.SetMusicVolume(120);
        en.SetMicVolume(50);
        en.SetQuality(0);
        en.SetMono(false);
        en.SetEqBands(10);
        en.SetMonitorDelay(100);
        en.SetOptimizeForOpus(true);
        CHECK(!SameProfile(saved, ProfileFromSettings(en.GetSettings())), "geaendert = verschieden");
        CHECK(en.SaveProfile(L"Test B") == PresetResult::Ok, "zweites Profil");

        CHECK(en.ApplyProfile(L"test a"), "Profil anwenden (Gross-/Kleinschreibung egal)");
        const Settings a = en.GetSettings();
        CHECK(a.musicVolPct == 80 && a.micVolPct == 90 && a.quality == 2 && a.mono && a.eqBands == 61 && a.eqGains.size() == 61 && a.eqGains[10] == 6.0f && a.monitorDelayMs == 300 &&
                  !a.optimizeForOpus && a.profileName == L"Test A",
              "Werte aus dem Profil: %d %d %d %d %d", a.musicVolPct, a.micVolPct, a.quality, a.mono, a.eqBands);
        CHECK(SameProfile(saved, ProfileFromSettings(a)), "nach Anwenden gleich dem Profil");
        CHECK(!en.ApplyProfile(L"gibt es nicht"), "unbekanntes Profil");

        CHECK(en.ApplyNextProfile() && en.GetSettings().profileName == L"Test B" && en.GetSettings().musicVolPct == 120 && en.GetSettings().eqBands == 10, "naechstes Profil");
        CHECK(en.ApplyNextProfile() && en.GetSettings().profileName == L"Test A", "reihum");
        CHECK(en.RenameProfile(L"Test A", L"Alpha") == PresetResult::Ok && en.GetSettings().profileName == L"Alpha", "Umbenennen aktualisiert das gewaehlte");
        CHECK(en.ProfileNames().size() == 2 && en.ProfileNames()[0] == L"Alpha", "Namen");
        CHECK(en.DeleteProfile(L"Alpha") && en.GetSettings().profileName.empty() && en.ProfileNames().size() == 1, "Loeschen leert die Auswahl");
        CHECK(en.DeleteProfile(L"Test B") && !en.ApplyNextProfile(), "ohne Profile: nichts zu wechseln");
        en.SetMusicVolume(100);
        en.SetMicVolume(100);
        en.SetQuality(1);
        en.SetMono(false);
        en.SetOptimizeForOpus(true);
        en.SetMonitorDelay(250);
    }

    // ---- 8. Now-Playing: Titelwechsel melden (Datei), einmal je Titel, leer bei "Musik aus" -------------------------------------------------------
    {
        static std::vector<std::wstring> got;
        got.clear();
        en.SetNowPlayingCallback([](const std::wstring& t) { got.push_back(t); });
        en.SetNowPlayingChat(true);
        en.SetEnabled(true);
        en.PlayerClear();
        en.PlayerAdd({wav, mp3});
        en.PlayerSetRepeat(RepeatMode::Off);
        en.PlayerPlay(0);
        RunFor(2000);
        CHECK(got.empty(), "nach 2 s noch keine Meldung (3 s Beruhigung): %zu", got.size());
        RunFor(2800);
        CHECK(got.size() == 1 && got[0] == L"tone", "genau eine Meldung 'tone' nach ~5 s (%zu)", got.size());
        RunFor(1500);
        CHECK(got.size() == 1, "keine Wiederholung bei gleichem Titel (%zu)", got.size());
        en.PlayerTogglePause();
        RunFor(1800);
        en.PlayerTogglePause();
        RunFor(3500);
        CHECK(got.size() == 1, "Pause/Weiter meldet nicht erneut (%zu)", got.size());
        en.SetEnabled(false);
        RunFor(1500);
        CHECK(got.size() == 2 && got[1].empty(), "Musik aus: leere Meldung (%zu)", got.size());
        en.SetNowPlayingChat(false);
        en.PlayerStop();
        en.SetNowPlayingCallback(nullptr);
        en.SetEnabled(true);
    }

    timeEndPeriod(1);
    en.Shutdown();
    if (argc > 2)
        if (FILE* f = std::fopen(argv[2], "wb")) {
            std::fwrite(g_all.data(), 2, g_all.size(), f);
            std::fclose(f);
        }
    std::printf(g_fail ? "%d FEHLER\n" : "engine_files_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
