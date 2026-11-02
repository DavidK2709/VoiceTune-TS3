// Selbsttest fuer Equalizer, FFT-Band-Zuordnung und Analyse-Puffer (ohne Windows-Abhaengigkeiten).
#include <cmath>
#include <cstdio>
#include <vector>

#include "analysis.h"
#include "equalizer.h"
#include "fft.h"
#include "presets.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

// Verstaerkung (dB) eines Sinus durch den EQ, nach Einschwingen gemessen.
static double GainDb(eq::Equalizer& e, double freq, double amp = 6000)
{
    const int          n = 48000;
    std::vector<int16_t> buf(n * 2);
    for (int i = 0; i < n; ++i) buf[2 * i] = buf[2 * i + 1] = static_cast<int16_t>(amp * std::sin(2 * M_PI * freq * i / 48000.0));
    std::vector<int16_t> in = buf;
    e.Process(buf.data(), n);
    double si = 0, so = 0;
    for (int i = n / 2; i < n; ++i) { si += double(in[2 * i]) * in[2 * i]; so += double(buf[2 * i]) * buf[2 * i]; }
    return 10.0 * std::log10(so / si);
}

static int NearestBand(int bands, double f)
{
    auto c = eq::CenterFrequencies(bands);
    int  best = 0;
    for (int i = 1; i < bands; ++i) if (std::fabs(std::log2(c[i] / f)) < std::fabs(std::log2(c[best] / f))) best = i;
    return best;
}

int main()
{
    // Raster
    for (int bands : {10, 31, 61}) {
        auto c = eq::CenterFrequencies(bands);
        CHECK((int)c.size() == bands, "bands=%d", bands);
        std::printf("%2d Baender: %.1f Hz .. %.1f Hz, Q=%.2f\n", bands, c.front(), c.back(), eq::BandQ(bands));
        CHECK(c.back() < 24000 * 0.9, "oberstes Band %.0f", c.back());
    }

    // Flach: Signal bleibt unveraendert
    {
        eq::Equalizer e;
        std::vector<float> g(31, 0.0f);
        e.Configure(31, g.data(), true);
        CHECK(!e.Active(), "flacher EQ muss inaktiv sein");
        std::vector<int16_t> a(2000), b;
        for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<int16_t>(10000 * std::sin(i * 0.05));
        b = a;
        e.Process(b.data(), 1000);
        CHECK(a == b, "Signal veraendert");
    }

    // Anhebung / Absenkung in allen Rastern
    for (int bands : {10, 31, 61}) {
        const int          i = NearestBand(bands, 1000);
        const double       fc = eq::CenterFrequencies(bands)[i];
        for (double db : {12.0, 6.0, -12.0}) {
            eq::Equalizer      e;
            std::vector<float> g(bands, 0.0f);
            g[i] = static_cast<float>(db);
            e.Configure(bands, g.data(), true);
            const double atCenter = GainDb(e, fc);
            eq::Equalizer e2;
            e2.Configure(bands, g.data(), true);
            const double farAway = GainDb(e2, fc / 8.0); // 3 Oktaven weiter unten
            CHECK(std::fabs(atCenter - db) < 0.7, "%d Baender, %+.0f dB: Mitte %.2f dB", bands, db, atCenter);
            CHECK(std::fabs(farAway) < 0.5, "%d Baender, %+.0f dB: 3 Okt. weiter %.2f dB", bands, db, farAway);
            if (db == 12.0) std::printf("%2d Baender +12 dB bei %.0f Hz: Mitte %+.2f dB, Nachbar-Oktave %+.2f dB\n", bands, fc, atCenter, [&] { eq::Equalizer e3; e3.Configure(bands, g.data(), true); return GainDb(e3, fc * 2); }());
        }
    }

    // Aus: keine Wirkung
    {
        eq::Equalizer      e;
        std::vector<float> g(31, 12.0f);
        e.Configure(31, g.data(), false);
        CHECK(std::fabs(GainDb(e, 1000)) < 0.01, "EQ aus");
    }

    // Presets & Umrechnung zwischen Rastern
    {
        auto bass = eq::PresetGains(eq::kBass, 31);
        auto f    = eq::CenterFrequencies(31);
        CHECK(bass[NearestBand(31, 40)] >= 5.0f, "Bass-Preset 40 Hz = %.1f", bass[NearestBand(31, 40)]);
        CHECK(std::fabs(bass[NearestBand(31, 5000)]) <= 0.5f, "Bass-Preset 5 kHz");
        auto flat = eq::PresetGains(eq::kFlat, 61);
        for (float v : flat) CHECK(v == 0.0f, "Flach");
        auto to61 = eq::ResampleCurve(31, bass.data(), 61);
        auto to31 = eq::ResampleCurve(61, to61.data(), 31);
        for (int i = 0; i < 31; ++i) CHECK(std::fabs(to31[i] - bass[i]) < 0.6f, "Roundtrip Band %d: %.2f vs %.2f", i, to31[i], bass[i]);
        (void)f;
    }

    // FFT: 1-kHz-Sinus landet im richtigen Band, Pegel ~ Amplitude
    {
        std::vector<float> s(8192);
        for (size_t i = 0; i < s.size(); ++i) s[i] = 0.5f * std::sin(2 * M_PI * 1000.0 * i / 48000.0);
        auto db = fft::MagnitudeDb(s.data(), s.size());
        for (int bands : {10, 31, 61}) {
            auto lv   = fft::BandLevelsDb(db, 48000, bands);
            int  best = int(std::max_element(lv.begin(), lv.end()) - lv.begin());
            CHECK(best == NearestBand(bands, 1000), "%d Baender: Band %d statt %d", bands, best, NearestBand(bands, 1000));
            CHECK(std::fabs(lv[best] - 20 * std::log10(0.5)) < 1.0, "Pegel %.2f dB (erwartet -6,0)", lv[best]);
        }
    }

    // Analyse-Puffer
    {
        Analysis a;
        std::vector<int16_t> blk(960 * 2, 0);
        for (int i = 0; i < 960; ++i) blk[2 * i] = blk[2 * i + 1] = (i % 2 ? 16384 : -16384);
        for (int k = 0; k < 5; ++k) a.Push(blk.data(), 960);
        float mn[8], mx[8];
        a.CopyEnvelope(mn, mx, 8);
        CHECK(mx[7] > 0.49f && mn[7] < -0.49f && mx[2] == 0.0f, "Huellkurve %.2f / %.2f / %.2f", mx[7], mn[7], mx[2]);
        std::vector<float> r(100);
        a.CopyRecent(r.data(), 100);
        CHECK(std::fabs(std::fabs(r[99]) - 0.5f) < 0.01f, "letztes Sample %.3f", r[99]);
    }

    std::printf(g_fail ? "%d FEHLER\n" : "eq_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
