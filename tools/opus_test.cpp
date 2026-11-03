
/*
 * VoiceTune-TS3 - Voicechanger-Plugin für TeamSpeak 3
 * Copyright (C) 2026  David K., Tony S.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
 
// Selbsttest der lokalen Opus-Vorschau: Frequenz, SNR je Bitrate, Bandbreite, Blockgroessen, Mono/Stereo-Verhalten.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "opus_preview.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

// Verarbeitet in wechselnden Blockgroessen
static std::vector<int16_t> Run(OpusPreview& p, std::vector<int16_t> in)
{
    const int sizes[] = {480, 960, 1000, 333, 720};
    size_t    i = 0;
    int       k = 0;
    while (i < in.size() / 2) {
        const int n = static_cast<int>(std::min<size_t>(sizes[k++ % 5], in.size() / 2 - i));
        p.Process(in.data() + 2 * i, n);
        i += n;
    }
    return in;
}

// Signal-zu-Rausch (dB) des linken Kanals nach bester Ausrichtung (Verzoegerung 0..3000 Frames) und bestem Gain
static double Snr(const std::vector<int16_t>& ref, const std::vector<int16_t>& out, size_t from, size_t frames, int* lagOut = nullptr)
{
    double bestSnr = -100;
    int    bestLag = 0;
    for (int lag = 0; lag <= 3000; ++lag) {
        double xy = 0, yy = 0, xx = 0;
        for (size_t i = from; i < from + frames; i += 2) {
            const double x = ref[2 * i], y = out[2 * (i + lag)];
            xy += x * y;
            yy += y * y;
            xx += x * x;
        }
        if (yy < 1) continue;
        const double g = xy / yy, err = xx - 2 * g * xy + g * g * yy;
        const double snr = 10 * std::log10(xx / std::max(err, 1e-9));
        if (snr > bestSnr) { bestSnr = snr; bestLag = lag; }
    }
    if (lagOut) *lagOut = bestLag;
    return bestSnr;
}

static double ToneDb(const std::vector<int16_t>& v, size_t from, size_t frames, double freq, int ch = 0)
{
    double re = 0, im = 0;
    for (size_t i = from; i < from + frames; ++i) {
        re += v[2 * i + ch] * std::cos(2 * M_PI * freq * i / 48000.0);
        im += v[2 * i + ch] * std::sin(2 * M_PI * freq * i / 48000.0);
    }
    return 20 * std::log10(2 * std::sqrt(re * re + im * im) / frames / 32768.0 + 1e-9);
}

int main()
{
    CHECK(OpusPreview::Available(), "libopus muss eingebunden sein");
    CHECK(!OpusPreview::Create(3, 32000) && !OpusPreview::Create(1, 100) && !OpusPreview::Create(1, 1000000), "ungueltige Parameter");

    const size_t N = 48000 * 3;
    // Musik-aehnliches Testsignal: Tiefen, Mitten, Hoehen
    std::vector<int16_t> music(2 * N);
    std::mt19937 rng(3);
    std::normal_distribution<double> nd(0.0, 200.0);
    for (size_t i = 0; i < N; ++i) {
        const double t = i / 48000.0;
        const double v = 4500 * std::sin(2 * M_PI * 220 * t) + 3500 * std::sin(2 * M_PI * 1000 * t) + 2500 * std::sin(2 * M_PI * 3000 * t) +
                         1800 * std::sin(2 * M_PI * 9000 * t) + 1500 * std::sin(2 * M_PI * 14000 * t) + nd(rng);
        music[2 * i] = music[2 * i + 1] = static_cast<int16_t>(std::clamp(v, -30000.0, 30000.0));
    }

    // 1. Sinus bleibt ein Sinus, gleich viele Frames, Verzoegerung ~20 ms
    {
        auto p = OpusPreview::Create(1, 32000);
        CHECK(p && p->Channels() == 1 && p->Bitrate() == 32000, "Voice anlegen");
        std::vector<int16_t> sine(2 * 48000);
        for (size_t i = 0; i < 48000; ++i) sine[2 * i] = sine[2 * i + 1] = static_cast<int16_t>(9000 * std::sin(2 * M_PI * 440 * i / 48000.0));
        auto out = Run(*p, sine);
        CHECK(out.size() == sine.size(), "gleich viele Frames");
        const double snr = Snr(sine, out, 12000, 24000);
        std::printf("440 Hz Sinus bei 32 kbit/s: SNR %.1f dB\n", snr);
        CHECK(snr > 12, "SNR %.1f", snr);
        CHECK(ToneDb(out, 24000, 12000, 440) > ToneDb(out, 24000, 12000, 2000) + 25, "Ton bleibt bei 440 Hz");
    }

    // 1b. Verzoegerung: ein einzelner Klick (nicht periodisch, daher eindeutig) kommt ~20 ms + Encoder-Vorlauf spaeter wieder heraus
    {
        auto                 p = OpusPreview::Create(1, 64000);
        std::vector<int16_t> click(2 * 48000, 0);
        click[2 * 10000] = click[2 * 10000 + 1] = 20000;
        auto   out = Run(*p, click);
        size_t at = 0;
        int    best = 0;
        for (size_t i = 0; i < out.size() / 2; ++i)
            if (std::abs(out[2 * i]) > best) { best = std::abs(out[2 * i]); at = i; }
        const int lag = static_cast<int>(at) - 10000;
        std::printf("Verzoegerung der Vorschau (Klick): %d Frames (%.1f ms)\n", lag, lag / 48.0);
        CHECK(lag >= 800 && lag <= 1700, "Verzoegerung %d Frames", lag);
    }

    // 2. Je niedriger die Bitrate, desto schlechter: SNR sinkt und die Hoehen (14 kHz) gehen verloren
    double snrAt[3], hfAt[3];
    const int rates[3] = {8000, 24000, 64000};
    for (int k = 0; k < 3; ++k) {
        auto p = OpusPreview::Create(1, rates[k]);
        auto out = Run(*p, music);
        int lag  = 0;
        snrAt[k] = Snr(music, out, 48000, 24000, &lag);
        hfAt[k]  = ToneDb(out, 48000, 48000, 14000) - ToneDb(music, 48000, 48000, 14000);
        std::printf("Voice mono %2d kbit/s: SNR %5.1f dB, 14-kHz-Anteil %+6.1f dB\n", rates[k] / 1000, snrAt[k], hfAt[k]);
    }
    CHECK(snrAt[2] > snrAt[0] + 3, "SNR waechst mit der Bitrate (%.1f -> %.1f)", snrAt[0], snrAt[2]);
    CHECK(hfAt[2] > hfAt[0] + 6, "Hoehen bleiben bei hoher Bitrate eher erhalten (%.1f -> %.1f)", hfAt[0], hfAt[2]);

    // 3. Voice macht aus Stereo Mono (links = rechts), Music behaelt Stereo
    {
        std::vector<int16_t> lonly(2 * 48000);
        for (size_t i = 0; i < 48000; ++i) lonly[2 * i] = static_cast<int16_t>(9000 * std::sin(2 * M_PI * 700 * i / 48000.0));
        auto voice = OpusPreview::Create(1, 48000), mus = OpusPreview::Create(2, 96000);
        auto ov = Run(*voice, lonly), om = Run(*mus, lonly);
        bool same = true;
        for (size_t i = 24000; i < 36000; ++i) same = same && ov[2 * i] == ov[2 * i + 1];
        CHECK(same, "Voice: links == rechts");
        const double l = ToneDb(om, 24000, 12000, 700, 0), r = ToneDb(om, 24000, 12000, 700, 1);
        std::printf("Music stereo, nur links: L %.1f dB, R %.1f dB\n", l, r);
        CHECK(l > r + 20, "Music behaelt Stereo");
    }

    std::printf(g_fail ? "%d FEHLER\n" : "opus_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
