#pragma once
// Grafischer Equalizer: Kette aus Peaking-Biquads (RBJ) auf Terz-/Sechstel-Oktav-Raster. Header-only und ohne Windows-Abhaengigkeit (testbar).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "mixer.h"

namespace eq {

constexpr int   kMaxBands = 61;
constexpr float kMaxDb    = 12.0f;

inline bool ValidBandCount(int n) { return n == 10 || n == 31 || n == 61; }
inline int  BandsPerOctave(int bands) { return bands == 10 ? 1 : bands == 31 ? 3 : 6; }

// Mittenfrequenzen: 10 Baender = Oktav-Raster 31,25 Hz..16 kHz; 31 / 61 Baender = 20 Hz * 2^(i/3) bzw. 2^(i/6) bis ~20,5 kHz.
inline std::vector<double> CenterFrequencies(int bands)
{
    std::vector<double> f;
    if (bands == 10) {
        for (int i = 0; i < 10; ++i) f.push_back(31.25 * std::pow(2.0, i));
    } else {
        const double perOct = BandsPerOctave(bands);
        for (int i = 0; i < bands; ++i) f.push_back(20.0 * std::pow(2.0, i / perOct));
    }
    return f;
}

// Guete, bei der benachbarte Baender sich bei halber Hoehe treffen.
inline double BandQ(int bands)
{
    const double r = std::pow(2.0, 1.0 / BandsPerOctave(bands));
    return std::sqrt(r) / (r - 1.0);
}

// Kurve auf ein anderes Raster uebertragen (lineare Interpolation ueber log-Frequenz).
inline std::vector<float> ResampleCurve(int oldBands, const float* oldGains, int newBands)
{
    const auto          fo = CenterFrequencies(oldBands), fn = CenterFrequencies(newBands);
    std::vector<float>  out(newBands, 0.0f);
    for (int i = 0; i < newBands; ++i) {
        if (fn[i] <= fo.front()) { out[i] = oldGains[0]; continue; }
        if (fn[i] >= fo.back()) { out[i] = oldGains[oldBands - 1]; continue; }
        int j = 0;
        while (j + 1 < oldBands && fo[j + 1] < fn[i]) ++j;
        const double t = (std::log2(fn[i]) - std::log2(fo[j])) / (std::log2(fo[j + 1]) - std::log2(fo[j]));
        out[i]         = static_cast<float>(oldGains[j] + (oldGains[j + 1] - oldGains[j]) * t);
    }
    return out;
}

enum Preset { kFlat = 0, kBass, kTreble, kSpeech, kLoudness, kVShape, kPresetCount };

inline const wchar_t* PresetName(int p)
{
    static const wchar_t* names[] = {L"Flach", L"Bass+", L"Höhen+", L"Sprache", L"Loudness", L"V-Form"};
    return names[std::clamp(p, 0, kPresetCount - 1)];
}

// Zielkurve eines Presets in dB bei Frequenz f.
inline double PresetDb(int preset, double f)
{
    auto low  = [&](double fc, double db) { return db / (1.0 + std::pow(f / fc, 2.0)); };
    auto high = [&](double fc, double db) { return db / (1.0 + std::pow(fc / f, 2.0)); };
    auto bell = [&](double fc, double db, double oct) { const double x = std::log2(f / fc) / oct; return db * std::exp(-x * x); };
    double db = 0;
    switch (preset) {
    case kBass: db = low(120, 7); break;
    case kTreble: db = high(5000, 6); break;
    case kSpeech: db = low(100, -8) + bell(2500, 4, 1.2) + high(9000, -4); break;
    case kLoudness: db = low(150, 6) + high(6000, 5) + bell(1000, -1.5, 1.5); break;
    case kVShape: db = low(200, 5) + high(4000, 5) + bell(900, -3, 1.2); break;
    default: break;
    }
    return std::clamp(db, -static_cast<double>(kMaxDb), static_cast<double>(kMaxDb));
}

inline std::vector<float> PresetGains(int preset, int bands)
{
    const auto         f = CenterFrequencies(bands);
    std::vector<float> g(bands);
    for (int i = 0; i < bands; ++i) g[i] = static_cast<float>(std::round(PresetDb(preset, f[i]) * 2.0) / 2.0); // 0,5-dB-Raster
    return g;
}

class Equalizer {
public:
    // Nur vom Audio-Thread aufrufen (Engine uebergibt neue Werte per Versionszaehler).
    void Configure(int bands, const float* gainsDb, bool enabled, double sampleRate = 48000.0)
    {
        if (bands != bands_) {
            for (auto& b : bq_) b = Biquad{};
            bands_ = bands;
        }
        enabled_ = enabled;
        active_.clear();
        const auto f = CenterFrequencies(bands);
        const double q = BandQ(bands);
        for (int i = 0; i < bands; ++i) {
            const double g = std::clamp(static_cast<double>(gainsDb[i]), -static_cast<double>(kMaxDb), static_cast<double>(kMaxDb));
            Biquad&      b = bq_[i];
            if (std::fabs(g) < 0.05 || f[i] > sampleRate * 0.45) {
                b = Biquad{};
                continue;
            }
            const double A = std::pow(10.0, g / 40.0), w0 = 2.0 * M_PI * f[i] / sampleRate, al = std::sin(w0) / (2.0 * q), c = std::cos(w0);
            const double a0 = 1.0 + al / A;
            b.b0 = (1.0 + al * A) / a0;
            b.b1 = (-2.0 * c) / a0;
            b.b2 = (1.0 - al * A) / a0;
            b.a1 = (-2.0 * c) / a0;
            b.a2 = (1.0 - al / A) / a0;
            active_.push_back(i);
        }
    }

    bool Active() const { return enabled_ && !active_.empty(); }

    // Stereo interleaved, in place. Ausgabe weich begrenzt.
    void Process(int16_t* io, int frames)
    {
        if (!Active()) return;
        for (int n = 0; n < frames; ++n) {
            for (int ch = 0; ch < 2; ++ch) {
                double x = io[2 * n + ch];
                for (int idx : active_) {
                    Biquad& b = bq_[idx];
                    const double y = b.b0 * x + b.z1[ch];
                    b.z1[ch]       = b.b1 * x - b.a1 * y + b.z2[ch];
                    b.z2[ch]       = b.b2 * x - b.a2 * y;
                    x              = y;
                }
                io[2 * n + ch] = mixer::SoftClip(static_cast<float>(x));
            }
        }
    }

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] = {0, 0}, z2[2] = {0, 0};
    };
    std::array<Biquad, kMaxBands> bq_{};
    std::vector<int>              active_;
    int                           bands_   = 0;
    bool                          enabled_ = false;
};

} // namespace eq
