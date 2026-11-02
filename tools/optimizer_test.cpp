// Selbsttest der Klang-Optimierung (Mono-Summe, Hochpass, Begrenzer) ohne Windows-Abhaengigkeiten.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "voice_optimizer.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static double RmsDb(const std::vector<int16_t>& v, size_t from, int channel, size_t frames)
{
    double s = 0;
    for (size_t i = from; i < from + frames; ++i) s += static_cast<double>(v[2 * i + channel]) * v[2 * i + channel];
    return 10.0 * std::log10(s / frames / (32768.0 * 32768.0) + 1e-12);
}

// Verarbeitet in Bloecken (wie der Mikrofon-Callback), Rueckgabe = bearbeitetes Signal
static std::vector<int16_t> Run(std::vector<int16_t> in, bool mono, int block = 480)
{
    VoiceOptimizer opt;
    for (size_t i = 0; i + block <= in.size() / 2; i += block) opt.Process(in.data() + 2 * i, block, mono);
    return in;
}

int main()
{
    const size_t N = 48000 * 2;
    std::mt19937 rng(5);
    std::normal_distribution<double> nd(0.0, 3000.0);

    // 1. Gegenphasiges Stereo (L = -R): die einfache Summe (L+R)/2 waere Stille, die Optimierung behaelt den Pegel
    {
        std::vector<int16_t> in(2 * N);
        for (size_t i = 0; i < N; ++i) {
            const int16_t v = static_cast<int16_t>(8000 * std::sin(2 * M_PI * 440 * i / 48000.0) + 4000 * std::sin(2 * M_PI * 1700 * i / 48000.0));
            in[2 * i] = v;
            in[2 * i + 1] = static_cast<int16_t>(-v);
        }
        const double naive = 0.0; // (L+R)/2
        auto         out   = Run(in, true);
        const double inDb = RmsDb(in, 48000, 0, 24000), outDb = RmsDb(out, 48000, 0, 24000);
        std::printf("gegenphasig: Stereo %.1f dBFS, naive Summe %.0f (Stille), optimiert %.1f dBFS\n", inDb, naive, outDb);
        CHECK(std::fabs(outDb - inDb) < 1.5, "Pegel %.1f dB gegen %.1f dB", outDb, inDb);
        bool same = true;
        for (size_t i = 48000; i < 72000; ++i) same = same && out[2 * i] == out[2 * i + 1];
        CHECK(same, "Ergebnis ist Mono (L == R)");
    }

    // 2. Gleichphasig (L = R): bleibt (bis auf Hochpass) unveraendert im Pegel
    {
        std::vector<int16_t> in(2 * N);
        for (size_t i = 0; i < N; ++i) in[2 * i] = in[2 * i + 1] = static_cast<int16_t>(8000 * std::sin(2 * M_PI * 440 * i / 48000.0));
        auto         out   = Run(in, true);
        const double inDb = RmsDb(in, 48000, 0, 24000), outDb = RmsDb(out, 48000, 0, 24000);
        std::printf("gleichphasig: Stereo %.2f dBFS, optimiert %.2f dBFS\n", inDb, outDb);
        CHECK(std::fabs(outDb - inDb) < 0.5, "gleichphasig %.2f dB", outDb - inDb);
    }

    // 3. Unkorreliertes Stereo (Rauschen): gleiche Leistung wie im Stereosignal (Ausgleich bis +3 dB)
    {
        std::vector<int16_t> in(2 * N);
        for (auto& v : in) v = static_cast<int16_t>(std::clamp(nd(rng), -32000.0, 32000.0));
        auto         out   = Run(in, true);
        const double inDb = RmsDb(in, 48000, 0, 24000), outDb = RmsDb(out, 48000, 0, 24000);
        std::printf("unkorreliert: Stereo %.2f dBFS, optimiert %.2f dBFS\n", inDb, outDb);
        CHECK(std::fabs(outDb - inDb) < 1.5, "unkorreliert %.2f dB", outDb - inDb);
    }

    // 4. Stereo bleibt Stereo, wenn das Mikrofon nicht mono ist
    {
        std::vector<int16_t> in(2 * N);
        for (size_t i = 0; i < N; ++i) {
            in[2 * i]     = static_cast<int16_t>(8000 * std::sin(2 * M_PI * 440 * i / 48000.0));
            in[2 * i + 1] = static_cast<int16_t>(8000 * std::sin(2 * M_PI * 880 * i / 48000.0));
        }
        auto         out = Run(in, false);
        const double l = RmsDb(out, 48000, 0, 24000), r = RmsDb(out, 48000, 1, 24000);
        bool         identical = true;
        for (size_t i = 48000; i < 72000; ++i) identical = identical && out[2 * i] == out[2 * i + 1];
        CHECK(!identical, "Stereo bleibt Stereo");
        CHECK(std::fabs(l - RmsDb(in, 48000, 0, 24000)) < 0.4 && std::fabs(r - RmsDb(in, 48000, 1, 24000)) < 0.4, "Kanaele unveraendert: %.2f / %.2f dB", l, r);
    }

    // 5. Begrenzer: uebersteuertes Signal (Amplitude 32000) bleibt nach dem Einschwingen unter der Schwelle, ohne Wrap-Around
    {
        std::vector<int16_t> in(2 * N);
        for (size_t i = 0; i < N; ++i) in[2 * i] = in[2 * i + 1] = static_cast<int16_t>(32000 * std::sin(2 * M_PI * 220 * i / 48000.0));
        auto out = Run(in, true);
        int  peak = 0;
        long wrap = 0;
        for (size_t i = 24000; i < N; ++i) {
            peak = std::max(peak, std::abs(static_cast<int>(out[2 * i])));
            if (i > 24000 && std::abs(out[2 * i] - out[2 * (i - 1)]) > 6000) ++wrap; // Spruenge = Wrap oder Clipping
        }
        std::printf("Begrenzer: Eingang 32000, Spitze danach %d (Schwelle %.0f)\n", peak, VoiceOptimizer::kLimitThreshold);
        CHECK(peak <= VoiceOptimizer::kLimitThreshold * 1.03f, "Spitze %d", peak);
        CHECK(wrap == 0, "%ld Spruenge", wrap);
    }

    // 6. Gleichanteil (DC) und leise Signale: DC verschwindet, ein leiser Ton bleibt unangetastet
    {
        std::vector<int16_t> in(2 * N);
        for (size_t i = 0; i < N; ++i) in[2 * i] = in[2 * i + 1] = static_cast<int16_t>(5000 + 500 * std::sin(2 * M_PI * 1000 * i / 48000.0));
        auto         out = Run(in, true);
        double       mean = 0;
        for (size_t i = 72000; i < 96000; ++i) mean += out[2 * i];
        mean /= 24000;
        CHECK(std::fabs(mean) < 60, "Gleichanteil %.0f", mean);
        const double outDb = RmsDb(out, 72000, 0, 24000);
        CHECK(outDb > -42 && outDb < -35, "1-kHz-Anteil bleibt (%.1f dBFS)", outDb);
    }

    std::printf(g_fail ? "%d FEHLER\n" : "optimizer_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
