// Selbsttest der neuen reinen Logik: Ducker/Sprach-Erkennung, Lautheit, Soundboard-Datei (.vtboard)
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "board_file.h"
#include "ducker.h"
#include "loudness.h"
#include "update_logic.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static std::vector<int16_t> Sine(double seconds, double freq, int amp)
{
    std::vector<int16_t> v(static_cast<size_t>(seconds * 48000) * 2);
    for (size_t i = 0; i < v.size() / 2; ++i) v[2 * i] = v[2 * i + 1] = static_cast<int16_t>(amp * std::sin(2 * M_PI * freq * i / 48000.0));
    return v;
}

int main()
{
    // ---- Ducker: sinkt auf die Zieltiefe, kommt zurueck, ohne Spruenge ----
    {
        Ducker d;
        d.Configure(12.0f, 30.0f, 600.0f);
        float g0, g1, last = 1.0f;
        bool  monotone = true;
        for (int i = 0; i < 100; ++i) { // 2 s absenken
            d.Step(true, 960, &g0, &g1);
            if (g1 > last + 1e-6f || std::fabs(g0 - last) > 1e-6f) monotone = false;
            last = g1;
        }
        const float target = std::pow(10.0f, -12.0f / 20.0f);
        CHECK(monotone && std::fabs(d.Gain() - target) < 0.002f, "Absenkung auf %.3f (Ziel %.3f)", d.Gain(), target);
        d.Step(false, 960, &g0, &g1);
        CHECK(g1 > g0 && g1 < 1.0f, "Rueckkehr beginnt sanft: %.3f -> %.3f", g0, g1);
        for (int i = 0; i < 300; ++i) d.Step(false, 960, &g0, &g1);
        CHECK(d.Gain() == 1.0f, "wieder bei 1,0: %.4f", d.Gain());
        // Angriff ist schneller als die Rueckkehr
        Ducker a;
        a.Configure(12.0f, 30.0f, 600.0f);
        for (int i = 0; i < 3; ++i) a.Step(true, 960, &g0, &g1); // 60 ms
        const float down = a.Gain();
        Ducker b;
        b.Configure(12.0f, 30.0f, 600.0f);
        for (int i = 0; i < 100; ++i) b.Step(true, 960, &g0, &g1);
        for (int i = 0; i < 3; ++i) b.Step(false, 960, &g0, &g1);
        CHECK(down < 0.4f && b.Gain() < 0.5f, "Angriff schnell (%.2f), Rueckkehr langsam (%.2f)", down, b.Gain());
        // Rampe
        std::vector<float> buf(8, 1.0f);
        ApplyRamp(buf.data(), 4, 2, 1.0f, 0.5f);
        CHECK(std::fabs(buf[0] - 0.875f) < 1e-5f && std::fabs(buf[6] - 0.5f) < 1e-5f && buf[0] == buf[1], "Rampe: %.3f %.3f", buf[0], buf[6]);
    }

    // ---- Sprach-Erkennung ----
    {
        SpeechDetector sd;
        std::vector<int16_t> quiet(960, 30), loud = Sine(0.02, 300, 8000);
        std::vector<int16_t> mono(loud.begin(), loud.begin() + 960);
        CHECK(!sd.Process(quiet.data(), 960, 1, -45.0f), "leise = keine Sprache");
        CHECK(sd.Process(mono.data(), 960, 1, -45.0f), "laut = Sprache (%.1f dB)", sd.LevelDb());
        bool still = true;
        for (int i = 0; i < 10; ++i) still = still && sd.Process(quiet.data(), 960, 1, -45.0f); // 200 ms Nachlauf
        CHECK(still, "haelt nach");
        bool gone = false;
        for (int i = 0; i < 30; ++i) gone = !sd.Process(quiet.data(), 960, 1, -45.0f);
        CHECK(gone, "nach der Haltezeit wieder still");
    }

    // ---- Lautheit ----
    {
        const auto quietClip = Sine(2.0, 440, 2000), loudClip = Sine(2.0, 440, 16000);
        const auto mq = loudness::Analyze(quietClip.data(), quietClip.size() / 2), ml = loudness::Analyze(loudClip.data(), loudClip.size() / 2);
        CHECK(mq.valid && ml.valid && ml.rmsDb > mq.rmsDb + 17 && ml.rmsDb < mq.rmsDb + 19.5f, "Pegelabstand 18 dB: %.1f und %.1f", mq.rmsDb, ml.rmsDb);
        const float gq = loudness::GainDb(mq, -18.0f), gl = loudness::GainDb(ml, -18.0f);
        CHECK(gq > 3 && gq <= 12, "leiser Clip wird angehoben: %.1f dB", gq);
        CHECK(gl < 0, "lauter Clip wird abgesenkt: %.1f dB", gl);
        // Spitze darf nicht ueber -0,5 dBFS
        loudness::Measure m;
        m.valid = true; m.rmsDb = -30; m.peakDb = -3;
        CHECK(loudness::GainDb(m, -18.0f) <= 2.5f + 1e-4f, "Spitzen-Grenze: %.2f", loudness::GainDb(m, -18.0f));
        const std::vector<int16_t> silence(48000 * 2, 0);
        CHECK(!loudness::Analyze(silence.data(), 48000).valid, "Stille hat keine Lautheit");
        CHECK(loudness::GainDb(loudness::Measure(), -18.0f) == 0.0f, "ohne Messung keine Verstaerkung");
        // Stille am Anfang zaehlt nicht mit
        std::vector<int16_t> padded(48000 * 2, 0);
        padded.insert(padded.end(), loudClip.begin(), loudClip.end());
        const auto mp = loudness::Analyze(padded.data(), padded.size() / 2);
        CHECK(std::fabs(mp.rmsDb - ml.rmsDb) < 0.6f, "Stille ausgeblendet: %.1f gegen %.1f", mp.rmsDb, ml.rmsDb);
    }

    // ---- Soundboard-Datei ----
    {
        CHECK(board::RelativePath(L"C:\\Boards", L"C:\\Sounds\\a\\hupe.wav") == L"..\\Sounds\\a\\hupe.wav", "relativ: %ls", board::RelativePath(L"C:\\Boards", L"C:\\Sounds\\a\\hupe.wav").c_str());
        CHECK(board::RelativePath(L"C:\\Sounds", L"C:\\Sounds\\hupe.wav") == L"hupe.wav", "gleicher Ordner");
        CHECK(board::RelativePath(L"C:\\Sounds", L"c:\\sounds\\x\\y.wav") == L"x\\y.wav", "Gross-/Kleinschreibung");
        CHECK(board::RelativePath(L"C:\\Sounds", L"D:\\x\\y.wav") == L"D:\\x\\y.wav", "anderes Laufwerk bleibt absolut");
        CHECK(board::ResolvePath(L"C:\\Boards\\Neu", L"..\\..\\Sounds\\a.wav") == L"C:\\Sounds\\a.wav", "aufloesen: %ls", board::ResolvePath(L"C:\\Boards\\Neu", L"..\\..\\Sounds\\a.wav").c_str());
        CHECK(board::ResolvePath(L"C:\\Boards", L"D:\\x.wav") == L"D:\\x.wav", "absolut bleibt");

        board::Data d;
        d.rows = 3;
        d.cols = 5;
        d.slots[0]   = {L"C:\\Sounds\\hupe.wav", L"Hupe | laut", 0xC0392B, 150};
        d.slots[7]   = {L"C:\\Töne\\Ärger.mp3", L"Ärger", 0, 100};
        d.slots[399] = {L"D:\\x.wav", L"Letzter", 0x00FF7F, 20};
        const std::string text = board::FormatBoard(d, L"C:\\Boards");
        CHECK(text.find("..\\Sounds\\hupe.wav") != std::string::npos && text.find("Slot400=00FF7F|20|D:\\x.wav|Letzter") != std::string::npos, "Text:\n%s", text.c_str());
        board::Data back;
        CHECK(board::ParseBoard(text, L"C:\\Boards", &back), "Parsen");
        CHECK(back.rows == 3 && back.cols == 5, "Raster %d x %d", back.rows, back.cols);
        CHECK(back.slots[0].path == L"C:\\Sounds\\hupe.wav" && back.slots[0].name == L"Hupe | laut" && back.slots[0].color == 0xC0392B && back.slots[0].volPct == 150, "Knopf 1");
        CHECK(back.slots[7].path == L"C:\\Töne\\Ärger.mp3" && back.slots[7].name == L"Ärger", "Umlaute");
        CHECK(back.slots[399].path == L"D:\\x.wav" && back.slots[399].volPct == 20 && back.slots[3].path.empty(), "Knopf 400 und leere Knoepfe");
        // an anderem Ort geoeffnet: relative Pfade wandern mit
        board::Data moved;
        CHECK(board::ParseBoard(text, L"C:\\Backup\\Boards", &moved) && moved.slots[0].path == L"C:\\Backup\\Sounds\\hupe.wav", "relative Pfade folgen dem Ordner: %ls", moved.slots[0].path.c_str());
        // kaputte / fremde Dateien
        board::Data junk;
        CHECK(!board::ParseBoard("hallo welt", L"C:\\", &junk) && !board::ParseBoard("", L"C:\\", &junk), "keine Board-Datei");
        CHECK(board::ParseBoard("[VoiceTuneBoard]\r\nRows=99\r\nCols=99\r\nSlot0=x\r\nSlot1=zz|9|C:\\a.wav|n\r\nSlot2=kaputt\r\nSlot999=000000|1|C:\\b.wav|b\r\n", L"C:\\", &junk), "tolerant");
        CHECK(junk.rows * junk.cols <= kSoundSlots && junk.rows <= kSoundMaxRows && junk.cols <= kSoundMaxCols, "Raster begrenzt: %d x %d", junk.rows, junk.cols);
        CHECK(junk.slots[0].path == L"C:\\a.wav" && junk.slots[1].path.empty(), "nur gueltige Zeilen");
        // UTF-8-BOM am Anfang
        board::Data bom;
        CHECK(board::ParseBoard(std::string("\xEF\xBB\xBF[VoiceTuneBoard]\r\nRows=1\r\nCols=2\r\n"), L"C:\\", &bom) && bom.cols == 2, "BOM");
    }

    // ---- Update-Logik ----
    {
        using namespace updatelogic;
        CHECK(IsNewer(L"v0.9", L"0.8") && IsNewer(L"v1.0", L"0.9.9") && IsNewer(L"0.8.1", L"0.8") && !IsNewer(L"v0.8", L"0.8") && !IsNewer(L"v0.7", L"0.8") && !IsNewer(L"", L"0.8") && !IsNewer(L"nix", L"0.8"),
              "Versionsvergleich");
        CHECK(IsNewer(L"v0.10", L"0.9"), "0.10 ist neuer als 0.9 (numerisch)");
        CHECK(FirstTag("[{\"url\":\"x\",\"tag_name\":\"v0.9\",\"name\":\"y\"},{\"tag_name\":\"v0.8\"}]") == L"v0.9", "erster Tag");
        CHECK(FirstTag("{\"message\":\"Not Found\"}").empty() && FirstTag("").empty() && FirstTag("[{\"tag_name\":\"").empty(), "kein Tag");
        CHECK(FirstTag("[{\"tag_name\": \"v1.2-beta\"}]") == L"v1.2-beta", "mit Leerzeichen");
    }

    std::printf(g_fail ? "%d FEHLER\n" : "logic_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
