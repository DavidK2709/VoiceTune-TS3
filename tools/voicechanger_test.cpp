
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

// Selbsttest des Voicechangers: Klang (Tonhoehe, Formant, Gain, Spiegeln, Invertieren, Dry/Wet, Latenz, Blockgroessen, Umschalten), Stimmen und das Einhaengen in die Engine.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#include "fft.h"
#include "voicechanger.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

namespace {
constexpr int    kRate = 48000;
constexpr double kPi   = 3.14159265358979323846;
using Pcm              = std::vector<int16_t>;

Pcm Sine(double hz, double secs, double amp = 8000)
{
    Pcm out(static_cast<size_t>(secs * kRate));
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<int16_t>(amp * std::sin(2 * kPi * hz * static_cast<double>(i) / kRate));
    return out;
}

// Vokal-artige Stimme: Pulsfolge (Grundton f0) durch drei Resonatoren (Formanten)
Pcm Vowel(double f0, double secs)
{
    const double formants[3] = {700, 1200, 2600}, bw[3] = {90, 110, 170};
    Pcm          out(static_cast<size_t>(secs * kRate));
    double       y1[3] = {}, y2[3] = {};
    double       phase = 1;
    for (size_t i = 0; i < out.size(); ++i) {
        phase += f0 / kRate;
        double src = 0;
        if (phase >= 1) {
            phase -= 1;
            src = 1;
        }
        double sum = 0;
        for (int k = 0; k < 3; ++k) {
            const double r = std::exp(-kPi * bw[k] / kRate), c = 2 * r * std::cos(2 * kPi * formants[k] / kRate);
            const double y = src + c * y1[k] - r * r * y2[k];
            y2[k]          = y1[k];
            y1[k]          = y;
            sum += y;
        }
        out[i] = static_cast<int16_t>(std::clamp(sum * 1200.0, -30000.0, 30000.0));
    }
    return out;
}

Pcm Effect(Pcm in, const VoiceParams& p, int block = 960, bool longWindow = false)
{
    auto vc = std::make_unique<VoiceChanger>();
    vc->Reset(true);
    for (size_t i = 0; i < in.size(); i += static_cast<size_t>(block)) vc->Process(&in[i], static_cast<int>(std::min<size_t>(block, in.size() - i)), 1, p, true, longWindow);
    return in;
}

double Rms(const Pcm& x, size_t from = 4800)
{
    double e = 0;
    size_t n = 0;
    for (size_t i = from; i < x.size(); ++i, ++n) e += static_cast<double>(x[i]) * x[i];
    return n ? std::sqrt(e / static_cast<double>(n)) : 0;
}

std::vector<float> Spectrum(const Pcm& x, size_t n = 16384)
{
    std::vector<float> s(n);
    const size_t       start = x.size() - n;
    for (size_t i = 0; i < n; ++i) s[i] = x[start + i] / 32768.0f;
    return fft::MagnitudeDb(s.data(), n);
}

double PeakHz(const Pcm& x)
{
    const auto db  = Spectrum(x);
    size_t     max = 2;
    for (size_t k = 2; k < db.size() - 1; ++k)
        if (db[k] > db[max]) max = k;
    const double a = db[max - 1], b = db[max], c = db[max + 1], off = 0.5 * (a - c) / (a - 2 * b + c);
    return (static_cast<double>(max) + off) * kRate / (2.0 * static_cast<double>(db.size()));
}

// Pegel (dB) einer Frequenz im Spektrum (Maximum in +-30 Hz)
double LevelAt(const Pcm& x, double hz)
{
    const auto   db = Spectrum(x);
    const double bin = kRate / (2.0 * static_cast<double>(db.size()));
    double       m   = -200;
    for (size_t k = static_cast<size_t>((hz - 30) / bin); k <= static_cast<size_t>((hz + 30) / bin) && k < db.size(); ++k) m = std::max(m, static_cast<double>(db[k]));
    return m;
}

// Grundton ueber die Autokorrelation (50..600 Hz)
double F0(const Pcm& x)
{
    const size_t n = 9600, start = x.size() - n - 1000;
    double       best = -1;
    int          lag  = 0;
    double       c0   = 1e-9;
    for (size_t i = 0; i < n; ++i) c0 += static_cast<double>(x[start + i]) * x[start + i];
    std::vector<double> corr(kRate / 50 + 2, 0.0);
    for (int l = kRate / 600; l <= kRate / 50; ++l) {
        double c = 0;
        for (size_t i = 0; i < n; ++i) c += static_cast<double>(x[start + i]) * x[start + i + static_cast<size_t>(l)];
        corr[l] = c / c0;
    }
    double top = 0;
    for (int l = kRate / 600; l <= kRate / 50; ++l) top = std::max(top, corr[l]);
    for (int l = kRate / 600 + 1; l < kRate / 50; ++l) // kleinste Verzoegerung nahe am Maximum: sonst wuerde eine Oktave tiefer gewinnen
        if (corr[l] > 0.85 * top && corr[l] >= corr[l - 1] && corr[l] >= corr[l + 1]) {
            best = corr[l];
            lag  = l;
            break;
        }
    return best > 0 ? static_cast<double>(kRate) / lag : 0;
}

// Schwerpunkt der Spektralhuelle von 200 bis 5000 Hz (lineare Leistung): steigt, wenn die Formanten nach oben wandern
double Centroid(const Pcm& x)
{
    const auto   db  = Spectrum(x);
    const double bin = kRate / (2.0 * static_cast<double>(db.size()));
    double       s = 0, w = 0;
    for (size_t k = static_cast<size_t>(200 / bin); k < static_cast<size_t>(5000 / bin); ++k) {
        const double pow = std::pow(10.0, db[k] / 10.0);
        s += pow * static_cast<double>(k) * bin;
        w += pow;
    }
    return w > 0 ? s / w : 0;
}

void TestDsp()
{
    constexpr int kLat = VoiceChanger::kFftShort;
    auto          vc   = std::make_unique<VoiceChanger>();

    { // Aus: Block bleibt unberuehrt
        Pcm a = Vowel(120, 0.2), b = a;
        CHECK(!vc->Process(b.data(), static_cast<int>(b.size()), 1, VoiceParams{}, false) && a == b && !vc->Busy(), "aus = unveraendert");
    }
    { // Neutral: exakt das Original, um die Latenz verzoegert
        const Pcm in = Vowel(120, 1.0), out = Effect(in, VoiceParams{});
        int       maxErr = 0;
        for (size_t i = 4096; i + kLat < in.size(); ++i) maxErr = std::max(maxErr, std::abs(out[i + kLat] - in[i]));
        CHECK(maxErr <= 2, "neutral weicht um %d ab", maxErr);
        CHECK(kLat * 1000 / kRate <= 22, "Latenz %d ms", kLat * 1000 / kRate);
        // Langes Fenster: dieselbe Treue, doppelte Latenz
        const Pcm lng = Effect(in, VoiceParams{}, 960, true);
        maxErr        = 0;
        for (size_t i = 8192; i + VoiceChanger::kFftLong < in.size(); ++i) maxErr = std::max(maxErr, std::abs(lng[i + VoiceChanger::kFftLong] - in[i]));
        CHECK(maxErr <= 2, "neutral, langes Fenster: %d", maxErr);
    }
    { // Tonhoehe eines Sinus
        for (const double st : {12.0, 7.0, 5.0, 1.0, -1.0, -5.0, -12.0}) {
            VoiceParams p;
            p.pitch = static_cast<float>(st);
            const double want = 440 * std::pow(2.0, st / 12);
            const Pcm    out  = Effect(Sine(440, 1.5), p);
            const double got  = PeakHz(out);
            CHECK(std::fabs(got - want) / want < 0.005, "%+.0f Halbtoene: %.1f Hz statt %.1f", st, got, want);
            // Formant laeuft mit: die Huelle wandert mit dem Ton, der Pegel muss bleiben. (Allein mit Pitch wandert ein Sinus aus seiner Huelle und wird zu Recht leiser.)
            p.formant        = p.pitch;
            const double rms = Rms(Effect(Sine(440, 1.5), p));
            CHECK(rms > 0.7 * 8000 / std::sqrt(2.0) && rms < 1.3 * 8000 / std::sqrt(2.0), "%+.0f Halbtoene mit Formant: Pegel %.0f", st, rms);
        }
    }
    { // Tonhoehe einer Stimme: Grundton wandert, die Formanten bleiben stehen
        const Pcm in = Vowel(120, 2.0);
        for (const double st : {5.0, -5.0, 3.0}) {
            VoiceParams p;
            p.pitch = static_cast<float>(st);
            const Pcm    out = Effect(in, p);
            const double f0  = F0(out), want = F0(in) * std::pow(2.0, st / 12);
            CHECK(std::fabs(f0 - want) / want < 0.03, "Stimme %+.0f: f0 %.1f statt %.1f", st, f0, want);
            CHECK(std::fabs(Centroid(out) - Centroid(in)) / Centroid(in) < 0.2, "Stimme %+.0f: Schwerpunkt %.0f statt %.0f", st, Centroid(out), Centroid(in));
            CHECK(Rms(out) > 0.5 * Rms(in) && Rms(out) < 2.0 * Rms(in), "Stimme %+.0f: Pegel %.0f statt %.0f", st, Rms(out), Rms(in));
        }
    }
    { // Tiefe Stimme: das lange Fenster trifft die Tonhoehe genau, und auch dort wandern die Formanten nicht mit
        const Pcm in = Vowel(95, 2.0);
        for (const double st : {-5.0, 5.0}) {
            VoiceParams p;
            p.pitch = static_cast<float>(st);
            const Pcm    out = Effect(in, p, 960, true);
            const double want = F0(in) * std::pow(2.0, st / 12);
            CHECK(std::fabs(F0(out) - want) / want < 0.02, "tief %+.0f: f0 %.1f statt %.1f", st, F0(out), want);
            CHECK(std::fabs(Centroid(out) - Centroid(in)) / Centroid(in) < 0.2, "tief %+.0f: Schwerpunkt %.0f statt %.0f", st, Centroid(out), Centroid(in));
        }
    }
    { // Formant: Grundton bleibt, der Schwerpunkt wandert
        const Pcm in = Vowel(120, 2.0);
        VoiceParams up, down;
        up.formant = 6;
        down.formant = -6;
        const Pcm a = Effect(in, up), b = Effect(in, down);
        CHECK(std::fabs(F0(a) - F0(in)) / F0(in) < 0.02 && std::fabs(F0(b) - F0(in)) / F0(in) < 0.02, "Formant aendert den Grundton: %.1f / %.1f / %.1f", F0(a), F0(b), F0(in));
        CHECK(Centroid(a) > 1.2 * Centroid(in), "Formant hoch: %.0f gegen %.0f", Centroid(a), Centroid(in));
        CHECK(Centroid(b) < 0.8 * Centroid(in), "Formant runter: %.0f gegen %.0f", Centroid(b), Centroid(in));
    }
    { // Gain
        VoiceParams p;
        p.gainDb = 6;
        const Pcm in = Sine(440, 1.0, 4000);
        CHECK(std::fabs(Rms(Effect(in, p)) / Rms(in) - 1.995) < 0.05, "+6 dB: %.2f", Rms(Effect(in, p)) / Rms(in));
        p.gainDb = -12;
        CHECK(std::fabs(Rms(Effect(in, p)) / Rms(in) - 0.251) < 0.01, "-12 dB: %.3f", Rms(Effect(in, p)) / Rms(in));
    }
    { // Spiegeln: f -> 3984,4 Hz - f, ohne dass sich die Frames ausloeschen
        VoiceParams p;
        p.mirror = true;
        for (const bool lng : {false, true})
            for (const double hz : {300.0, 1000.0, 2500.0}) {
                const double axis = VoiceChanger::MirrorHz(lng);
                const Pcm    out  = Effect(Sine(hz, 1.0), p, 960, lng);
                CHECK(std::fabs(PeakHz(out) - (axis - hz)) < 3, "%.0f Hz -> %.1f statt %.1f", hz, PeakHz(out), axis - hz);
                CHECK(Rms(out) > 0.8 * 8000 / std::sqrt(2.0), "Spiegeln %.0f Hz: Pegel %.0f", hz, Rms(out));
            }
    }
    { // Invertieren: 100 % Effekt = Vorzeichen gedreht, 50 % neutral = Ausloeschung
        const Pcm   in = Vowel(120, 1.0);
        VoiceParams p;
        p.invert      = true;
        const Pcm a   = Effect(in, p);
        int       err = 0;
        for (size_t i = 4096; i + kLat < in.size(); ++i) err = std::max(err, std::abs(a[i + kLat] + in[i]));
        CHECK(err <= 2, "Vorzeichen: %d", err);
        p.wetPct = 50;
        CHECK(Rms(Effect(in, p)) < 0.01 * Rms(in), "Ausloeschung: %.1f", Rms(Effect(in, p)));
    }
    { // Dry/Wet: 0 % = nur das Original (auch mit Effekt), 50 % = beide zu hoeren
        VoiceParams p;
        p.pitch   = 12;
        p.formant = 12;
        p.wetPct  = 0;
        const Pcm in = Sine(440, 1.5), dry = Effect(in, p);
        int       err = 0;
        for (size_t i = 4096; i + kLat < in.size(); ++i) err = std::max(err, std::abs(dry[i + kLat] - in[i]));
        CHECK(err <= 1, "Dry weicht ab: %d", err);
        p.wetPct = 50;
        const Pcm mix = Effect(in, p);
        CHECK(LevelAt(mix, 440) > -30 && LevelAt(mix, 880) > -30, "Mix: %.1f / %.1f dB", LevelAt(mix, 440), LevelAt(mix, 880));
    }
    { // Blockgroesse beeinflusst das Ergebnis nicht
        VoiceParams p;
        p.pitch = 5;
        p.formant = -3;
        const Pcm in = Vowel(120, 0.8), ref = Effect(in, p, 960);
        for (const int block : {1, 3, 160, 480, 1024, 4096}) CHECK(Effect(in, p, block) == ref, "Blockgroesse %d", block);
        const Pcm refLong = Effect(in, p, 960, true);
        for (const int block : {1, 480, 4096}) CHECK(Effect(in, p, block, true) == refLong, "Blockgroesse %d, langes Fenster", block);
        // Umschalten mitten im Strom: kurz leer, danach wieder Klang
        Pcm sw = Sine(440, 1.0);
        vc->Reset(true);
        for (size_t i = 0; i + 960 <= sw.size(); i += 960) vc->Process(&sw[i], 960, 1, VoiceParams{}, true, i >= 24000);
        CHECK(vc->LatencyFrames() == VoiceChanger::kFftLong && Rms(Pcm(sw.begin() + 40000, sw.end() - 960), 0) > 4000, "Umschalten: Pegel %.0f", Rms(Pcm(sw.begin() + 40000, sw.end() - 960), 0));
    }
    { // Mehrkanal: alle Kanaele tragen die Mono-Summe
        VoiceParams p;
        p.pitch = 3;
        const Pcm mono = Sine(440, 0.5);
        Pcm       st(mono.size() * 2);
        for (size_t i = 0; i < mono.size(); ++i) st[2 * i] = st[2 * i + 1] = mono[i];
        vc->Reset(true);
        for (size_t i = 0; i < mono.size(); i += 960) vc->Process(&st[2 * i], static_cast<int>(std::min<size_t>(960, mono.size() - i)), 2, p, true);
        const Pcm ref = Effect(mono, p);
        bool      eq  = true;
        for (size_t i = 0; i < mono.size(); ++i) eq = eq && st[2 * i] == ref[i] && st[2 * i + 1] == ref[i];
        CHECK(eq, "Stereo = Mono");
    }
    { // Stille bleibt Stille; Vollaussteuerung und Rauschen bleiben stabil, auch mit extremen Reglern
        VoiceParams p;
        p.pitch = 12;
        p.formant = 12;
        p.gainDb = 24;
        CHECK(Rms(Effect(Pcm(48000, 0), p)) == 0, "Stille");
        Pcm noise(96000);
        unsigned seed = 1;
        for (auto& s : noise) {
            seed = seed * 1103515245u + 12345u;
            s    = static_cast<int16_t>((seed >> 16) & 0xFFFF);
        }
        const Pcm a = Effect(noise, p);
        CHECK(Rms(a) > 100, "Rauschen bleibt hoerbar: %.0f", Rms(a));
        p.pitch = -12;
        p.formant = -12;
        p.mirror = true;
        p.gainDb = 0;
        const Pcm b = Effect(noise, p);
        CHECK(Rms(b) > 100, "Rauschen, tief + gespiegelt: %.0f", Rms(b));
        const Pcm after = Effect(Sine(440, 1.0), p); // NaN wuerde hier als Stille oder Dauerpegel auffallen
        CHECK(Rms(after) > 100, "danach weiter Klang: %.0f", Rms(after));
    }
    { // Umschalten waehrend der Stimme: keine Knackser, nach dem Ausblenden wieder bitgenau das Original
        VoiceParams p;
        p.pitch = 4;
        Pcm in = Sine(200, 1.0, 6000), out = in;
        vc->Reset(false);
        const int b = 480;
        const int blocks = static_cast<int>(out.size()) / b;
        for (int i = 0; i < blocks; ++i) vc->Process(&out[static_cast<size_t>(i) * b], b, 1, p, i >= 20 && i < 60);
        CHECK(!vc->Busy(), "ganz aus");
        int jump = 0;
        for (size_t i = 1; i < static_cast<size_t>(blocks) * b; ++i) jump = std::max(jump, std::abs(out[i] - out[i - 1]));
        CHECK(jump < 1200, "Sprung %d", jump);
        bool same = true;
        for (size_t i = 62 * static_cast<size_t>(b); i < static_cast<size_t>(blocks) * b; ++i) same = same && out[i] == in[i];
        CHECK(same, "nach dem Ausblenden unveraendert");
        // Regler ziehen knackt nicht
        vc->Reset(true);
        Pcm       sweep = Sine(200, 1.0, 6000);
        VoiceParams q;
        for (int i = 0; i < 100; ++i) {
            q.wetPct = 100 - i;
            q.gainDb = static_cast<float>(i % 2 ? 0 : -6); // Sprungziel: die Glaettung faengt es ab
            vc->Process(&sweep[static_cast<size_t>(i) * b], b, 1, q, true);
        }
        int jump2 = 0;
        for (size_t i = 4096; i < 48000; ++i) jump2 = std::max(jump2, std::abs(sweep[i] - sweep[i - 1]));
        CHECK(jump2 < 400, "Regler-Sprung %d", jump2);
    }
    { // Echtzeit: 10 s Audio muessen weit schneller als 10 s rechnen
        VoiceParams p;
        p.pitch = 5;
        p.formant = 3;
        const Pcm     in = Vowel(120, 10.0);
        LARGE_INTEGER f, a, z;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        const Pcm out = Effect(in, p);
        QueryPerformanceCounter(&z);
        const double secs = static_cast<double>(z.QuadPart - a.QuadPart) / static_cast<double>(f.QuadPart);
        std::printf("Rechenzeit: %.1f %% eines CPU-Kerns\n", secs / 10.0 * 100.0);
        CHECK(secs < 1.0 && Rms(out) > 0, "10 s Audio brauchen %.2f s", secs);
    }
}

void TestPresets()
{
    voicepresets::Book b;
    VoiceParams        p;
    p.pitch = 99; // wird begrenzt
    CHECK(b.Save(L"  Meine \"Stimme\" ", p) == PresetResult::Ok && b.items[0].name == L"Meine 'Stimme'" && b.items[0].params.pitch == 12.0f, "anlegen");
    CHECK(b.Save(L"meine 'stimme'", VoiceParams{}) == PresetResult::Updated && b.items.size() == 1 && b.items[0].params.pitch == 0.0f, "ueberschreiben");
    CHECK(b.Save(L"", p) == PresetResult::Empty && b.Save(L"Monster", p) == PresetResult::Reserved && b.Save(L"KOBOLD", p) == PresetResult::Reserved, "ungueltige Namen");
    CHECK(b.Save(L"Zweite", p) == PresetResult::Ok && b.Rename(L"Zweite", L"Monster") == PresetResult::Reserved && b.Rename(L"Zweite", L"meine 'stimme'") == PresetResult::Exists &&
              b.Rename(L"gibtsnicht", L"x") == PresetResult::NotFound && b.Rename(L"Zweite", L"Dritte") == PresetResult::Ok && b.Find(L"dritte"),
          "umbenennen");
    CHECK(b.Delete(L"DRITTE") && !b.Delete(L"Dritte") && b.items.size() == 1, "loeschen");
    CHECK(b.SuggestName() == L"Stimme 1", "Vorschlag");
    for (size_t i = 0; i < voicepresets::kMaxUser - 1; ++i) CHECK(b.Save(L"V" + std::to_wstring(i), p) == PresetResult::Ok, "fuellen");
    CHECK(b.Save(L"zu viel", p) == PresetResult::Full, "voll");
}

// Einmal durch die Engine, wie es TeamSpeak tut. mic = Mono-Bloecke zu 960 Frames.
Pcm MixAll(Engine& en, uint64_t schid, Pcm in, int* editedOut = nullptr)
{
    for (size_t i = 0; i + 960 <= in.size(); i += 960) {
        int edited = 0;
        en.Mix(schid, reinterpret_cast<short*>(&in[i]), 960, 1, &edited);
        if (editedOut) *editedOut |= edited;
    }
    return in;
}

void TestEngine()
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring cfg = std::wstring(tmp) + L"aam_voice_test", ini = cfg + L"\\voicetune.ini";
    CreateDirectoryW(cfg.c_str(), nullptr);
    DeleteFileW(ini.c_str());

    Engine& en = Engine::Get();
    en.SetLogger([](const char*) {});
    en.Init(cfg);
    const Pcm tone = Sine(440, 1.5);

    // Aus: Mikrofon unberuehrt, kein "edited"
    CHECK(!en.VoiceEnabled(), "startet aus");
    int edited = 0;
    CHECK(MixAll(en, 1, tone, &edited) == tone && edited == 0, "aus: unveraendert (edited %d)", edited);

    // An: wirkt auch ganz ohne Musikquelle
    VoiceParams p;
    p.pitch   = 12;
    p.formant = 12;
    en.SetVoiceParams(p);
    en.SetVoiceEnabled(true);
    CHECK(en.VoiceEnabled() && en.MicAlive(), "an");
    edited = 0;
    const Pcm shifted = MixAll(en, 1, tone, &edited);
    CHECK(std::fabs(PeakHz(shifted) - 880) < 5 && (edited & 1), "Tonhoehe in der Engine: %.1f Hz, edited %d", PeakHz(shifted), edited);

    // Stumm geht vor
    en.SetMicMuted(true);
    CHECK(Rms(MixAll(en, 1, tone), 9600) == 0, "stumm bleibt stumm");
    en.SetMicMuted(false);

    // Zwei Verbindungen abwechselnd ("Test starten" neben dem Server): jede klingt sauber. Das Abhoeren speist nur eine.
    en.SetVoiceMonitor(40);
    MixAll(en, 1, Pcm(9600, 0));
    const uint64_t before = en.VoiceMonitorWritten();
    Pcm a = tone, b = tone;
    for (size_t i = 0; i + 960 <= a.size(); i += 960) {
        int e1 = 0, e2 = 0;
        en.Mix(1, reinterpret_cast<short*>(&a[i]), 960, 1, &e1);
        en.Mix(2, reinterpret_cast<short*>(&b[i]), 960, 1, &e2);
    }
    CHECK(std::fabs(PeakHz(a) - 880) < 5 && std::fabs(PeakHz(b) - 880) < 5 && Rms(a) > 3000 && Rms(b) > 3000, "zwei Verbindungen: %.1f / %.1f Hz, Pegel %.0f / %.0f", PeakHz(a), PeakHz(b), Rms(a), Rms(b));
    const uint64_t written = en.VoiceMonitorWritten() - before;
    CHECK(written == a.size() / 960 * 960, "Abhoeren einfach gespeist: %llu Frames", static_cast<unsigned long long>(written));
    en.SetVoiceMonitor(0);

    // Abhoeren geht auch ohne Effekt (Vergleich mit der eigenen Stimme), das gesendete Signal bleibt dabei unveraendert
    en.SetVoiceEnabled(false);
    MixAll(en, 1, Pcm(9600, 0)); // ausblenden lassen
    en.SetVoiceMonitor(10);
    const uint64_t w0 = en.VoiceMonitorWritten();
    CHECK(MixAll(en, 1, tone) == tone, "Abhoeren veraendert das Gesendete nicht");
    CHECK(en.VoiceMonitorWritten() > w0, "Abhoeren ohne Effekt");
    en.SetVoiceMonitor(0);

    // Stimmen und Speichern
    en.SetVoiceParams(p);
    CHECK(en.SaveVoicePreset(L"Größe") == PresetResult::Ok && en.GetSettings().voicePresetName == L"Größe", "Stimme speichern");
    VoiceParams other;
    other.pitch = -3;
    other.mirror = true;
    other.invert = true;
    other.wetPct = 70;
    other.gainDb = -5.5f;
    other.formant = 2.5f;
    en.SetVoiceParams(other);
    CHECK(en.SaveVoicePreset(L"Zweite") == PresetResult::Ok, "zweite Stimme");
    CHECK(en.ApplyVoicePreset(L"Größe") && en.GetSettings().voice.pitch == 12.0f, "anwenden");
    CHECK(en.ApplyVoicePreset(L"Monster") && voicepresets::Same(en.GetSettings().voice, voicepresets::FindBuiltin(L"Monster")->params), "mitgelieferte anwenden");
    CHECK(!en.ApplyVoicePreset(L"Gibt es nicht") && en.GetSettings().voicePresetName == L"Monster", "unbekannte Stimme");
    CHECK(en.SaveVoicePreset(L"Monster") == PresetResult::Reserved, "reserviert");

    // Profile merken sich die Stimme
    CHECK(en.ApplyVoicePreset(L"Zweite") && en.SaveProfile(L"Arbeit") == PresetResult::Ok, "Profil speichern");
    CHECK(en.ApplyVoicePreset(L"Größe") && en.ApplyProfile(L"Arbeit") && en.GetSettings().voicePresetName == L"Zweite" && en.GetSettings().voice.mirror, "Profil wendet die Stimme an");
    CHECK(en.RenameVoicePreset(L"Zweite", L"Dritte") == PresetResult::Ok && en.GetSettings().profiles.Find(L"Arbeit")->voicePreset == L"Dritte" && en.GetSettings().voicePresetName == L"Dritte", "Umbenennen folgt im Profil");
    en.SetVoiceEnabled(true);
    en.SetVoiceMonitor(35);
    en.SetVoiceLongWindow(true);
    en.Shutdown();

    // Neu laden: Stimmen, Regler und Abhoeren bleiben, Ein/Aus nicht
    const Settings r = LoadSettings(ini);
    CHECK(r.voicePresets.items.size() == 2 && r.voicePresets.Find(L"Größe") && r.voicePresets.Find(L"Dritte") && r.voicePresetName == L"Dritte", "Stimmen in der INI: %zu", r.voicePresets.items.size());
    const VoiceParams v = r.voicePresets.Find(L"Dritte")->params;
    CHECK(v.pitch == -3.0f && v.formant == 2.5f && v.gainDb == -5.5f && v.wetPct == 70 && v.mirror && v.invert, "Werte der Stimme");
    CHECK(voicepresets::Same(r.voice, v) && r.voiceMonitorPct == 35 && r.voiceLongWindow, "aktuelle Regler");
    en.Init(cfg);
    CHECK(!en.VoiceEnabled() && en.GetSettings().voiceMonitorPct == 35, "nach dem Neustart aus");
    CHECK(en.DeleteVoicePreset(L"Dritte") && en.GetSettings().voicePresetName.empty() && en.GetSettings().profiles.Find(L"Arbeit")->voicePreset.empty(), "Loeschen raeumt auf");
    en.SetVoiceMonitor(0);
    en.Shutdown();
    DeleteFileW(ini.c_str());
    RemoveDirectoryW(cfg.c_str());
}
} // namespace

int main()
{
    TestDsp();
    TestPresets();
    TestEngine();
    std::printf(g_fail ? "%d FEHLER\n" : "voicechanger_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
