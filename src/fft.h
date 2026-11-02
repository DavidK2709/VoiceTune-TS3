#pragma once
// Kleine FFT + Zuordnung der Spektrum-Betraege auf die Equalizer-Baender (fuer die Live-Anzeige).
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "equalizer.h"

namespace fft {

// In-place Radix-2 (a.size() muss Zweierpotenz sein).
inline void Transform(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double                ang = -2.0 * M_PI / static_cast<double>(len);
        const std::complex<double>  wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Betragsspektrum (dBFS, 0 dB = Vollaussteuerung-Sinus) eines Blocks von `n` Samples (Zweierpotenz), Hann-Fenster.
inline std::vector<float> MagnitudeDb(const float* samples, size_t n)
{
    std::vector<std::complex<double>> a(n);
    double                             wsum = 0;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n - 1));
        a[i]           = samples[i] * w;
        wsum += w;
    }
    Transform(a);
    std::vector<float> db(n / 2);
    for (size_t k = 0; k < n / 2; ++k) {
        const double mag = 2.0 * std::abs(a[k]) / wsum; // Amplitude des Sinus im Bin
        db[k]            = static_cast<float>(20.0 * std::log10(std::max(mag, 1e-9)));
    }
    return db;
}

// Pegel (dB) je Equalizer-Band: Maximum der Bins im Band, bei schmalen Baendern (unter Bin-Abstand) Interpolation am Mittelpunkt.
inline std::vector<float> BandLevelsDb(const std::vector<float>& binDb, double sampleRate, int bands)
{
    const size_t        n      = binDb.size() * 2;
    const double        binHz  = sampleRate / static_cast<double>(n);
    const auto          centers = eq::CenterFrequencies(bands);
    const double        half   = std::pow(2.0, 1.0 / (2.0 * eq::BandsPerOctave(bands)));
    std::vector<float>  out(bands, -120.0f);
    for (int i = 0; i < bands; ++i) {
        const double lo = centers[i] / half, hi = centers[i] * half;
        const size_t k0 = static_cast<size_t>(std::ceil(lo / binHz)), k1 = static_cast<size_t>(std::floor(hi / binHz));
        if (k1 >= k0 && k1 < binDb.size()) {
            float m = -200.0f;
            for (size_t k = k0; k <= k1; ++k) m = std::max(m, binDb[k]);
            out[i] = m;
        } else { // schmaler als ein Bin: linear zwischen den Nachbar-Bins
            const double pos = centers[i] / binHz;
            const size_t k   = std::min(static_cast<size_t>(pos), binDb.size() - 2);
            const double t   = pos - static_cast<double>(k);
            out[i]           = static_cast<float>(binDb[k] * (1.0 - t) + binDb[k + 1] * t);
        }
    }
    return out;
}

} // namespace fft
