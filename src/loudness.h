#pragma once
// Lautheit eines Clips messen und eine Angleich-Verstaerkung ableiten. Reine Logik ohne Windows-Abhaengigkeit.
// Messung: grobe K-Gewichtung (Hochpass ca. 100 Hz), RMS ueber alle nicht stillen 100-ms-Bloecke (Gate -60 dB), Ergebnis in dB (0 = Vollaussteuerung).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace loudness {

struct Measure {
    float rmsDb  = -120.0f; // gewichtete Lautheit (RMS) ueber die nicht stillen Bloecke
    float peakDb = -120.0f; // Spitze
    bool  valid  = false;
};

// pcm = interleaved Stereo, 48 kHz
inline Measure Analyze(const int16_t* pcm, size_t frames)
{
    Measure m;
    if (!pcm || frames < 480) return m;
    const float a = std::exp(-2.0f * 3.14159265f * 100.0f / 48000.0f); // Hochpass 1. Ordnung
    float       zl = 0, zr = 0, xl1 = 0, xr1 = 0;
    const size_t block = 4800; // 100 ms
    double       sumAll = 0;
    size_t       framesAll = 0;
    int          peak = 0;
    for (size_t start = 0; start < frames; start += block) {
        const size_t n = std::min(block, frames - start);
        double       sum = 0;
        for (size_t i = 0; i < n; ++i) {
            const float l = pcm[(start + i) * 2], r = pcm[(start + i) * 2 + 1];
            peak          = std::max(peak, std::max(std::abs(static_cast<int>(pcm[(start + i) * 2])), std::abs(static_cast<int>(pcm[(start + i) * 2 + 1]))));
            zl            = a * (zl + l - xl1);
            zr            = a * (zr + r - xr1);
            xl1           = l;
            xr1           = r;
            sum += static_cast<double>(zl) * zl + static_cast<double>(zr) * zr;
        }
        const double ms = sum / (2.0 * static_cast<double>(n)) / (32768.0 * 32768.0);
        if (10.0 * std::log10(ms + 1e-12) < -60.0) continue; // Stille nicht mitzaehlen
        sumAll += sum;
        framesAll += n;
    }
    if (framesAll == 0) return m;
    const double ms = sumAll / (2.0 * static_cast<double>(framesAll)) / (32768.0 * 32768.0);
    m.rmsDb         = static_cast<float>(10.0 * std::log10(ms + 1e-12));
    m.peakDb        = peak > 0 ? static_cast<float>(20.0 * std::log10(peak / 32768.0)) : -120.0f;
    m.valid         = true;
    return m;
}

// Verstaerkung in dB, die den Clip auf targetDb bringt; begrenzt auf +-limitDb und so, dass die Spitze nicht ueber -0,5 dBFS steigt.
inline float GainDb(const Measure& m, float targetDb, float limitDb = 12.0f)
{
    if (!m.valid) return 0.0f;
    float g = targetDb - m.rmsDb;
    g       = std::min(limitDb, std::max(-limitDb, g));
    if (m.peakDb + g > -0.5f) g = -0.5f - m.peakDb; // nicht uebersteuern
    return std::min(limitDb, std::max(-limitDb, g));
}

inline float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace loudness
