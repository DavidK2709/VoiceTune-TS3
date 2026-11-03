
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
 
#pragma once
// Bereitet das Musiksignal fuer die Uebertragung ueber Opus (Voice-Codec: mono, 48 kHz) auf, damit nach dem Encoder moeglichst wenig verloren geht.
// Header-only, ohne Windows-Abhaengigkeit (testbar). Arbeitet auf interleaved Stereo (int16) und laesst Stereo intern so lange wie moeglich erhalten.
//   1. Hochpass 25 Hz: nimmt Gleichanteil und Subsonic-Rumpeln weg (kostet Opus nur Bits).
//   2. Phasensichere Mono-Summe (nur wenn das Mikrofon mono ist): gegenphasige Anteile loeschen sich bei (L+R)/2 aus; bei negativer Korrelation wird
//      (L-R)/2 genommen, und der Pegel wird auf gleiche Leistung wie das Stereosignal gebracht (maximal +3 dB), damit nichts leiser wird.
//   3. Begrenzer auf ca. -1,7 dBFS (Attack ~1 ms, Release ~80 ms): Opus uebersteuert an Spitzen hoerbar, der Begrenzer faengt sie vorher weich ab.
#include <algorithm>
#include <cmath>
#include <cstdint>

class VoiceOptimizer {
public:
    static constexpr float kLimitThreshold = 27000.0f; // ~ -1,7 dBFS

    explicit VoiceOptimizer(double sampleRate = 48000.0) { SetSampleRate(sampleRate); }

    void SetSampleRate(double sr)
    {
        sr_       = sr;
        hpCoef_   = static_cast<float>(std::exp(-2.0 * M_PI * 25.0 / sr));
        atkCoef_  = static_cast<float>(1.0 - std::exp(-1.0 / (0.001 * sr)));
        relCoef_  = static_cast<float>(1.0 - std::exp(-1.0 / (0.080 * sr)));
    }

    // mono = true: das Ergebnis ist Mono (L = R); false: Stereo bleibt Stereo (nur Hochpass + Begrenzer).
    void Process(int16_t* io, int frames, bool mono)
    {
        if (frames <= 0) return;
        // 1. Hochpass (1. Ordnung, je Kanal): y[n] = a * (y[n-1] + x[n] - x[n-1])
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c) {
                const float x = io[2 * i + c];
                hpY_[c]       = hpCoef_ * (hpY_[c] + x - hpX_[c]);
                hpX_[c]       = x;
                io[2 * i + c] = static_cast<int16_t>(std::clamp(hpY_[c], -32768.0f, 32767.0f));
            }

        if (mono) MonoPhaseSafe(io, frames);

        // 3. Begrenzer: der benoetigte Pegel-Abzug folgt den Spitzen schnell (Attack) und erholt sich langsam (Release)
        for (int i = 0; i < frames; ++i) {
            const float peak   = std::max(std::fabs(static_cast<float>(io[2 * i])), std::fabs(static_cast<float>(io[2 * i + 1])));
            const float needed = peak > kLimitThreshold ? kLimitThreshold / peak : 1.0f;
            lim_ += needed < lim_ ? (needed - lim_) * atkCoef_ * 8.0f : (1.0f - lim_) * relCoef_; // Attack deutlich schneller als der Rueckweg
            lim_ = std::min(lim_, 1.0f);
            const float g = std::min(lim_, needed); // nie ueber die Schwelle hinaus
            io[2 * i]     = static_cast<int16_t>(std::clamp(io[2 * i] * g, -32768.0f, 32767.0f));
            io[2 * i + 1] = static_cast<int16_t>(std::clamp(io[2 * i + 1] * g, -32768.0f, 32767.0f));
        }
    }

private:
    // 2. Mono-Summe mit Vorzeichenwahl nach der Korrelation von L und R und Leistungsausgleich
    void MonoPhaseSafe(int16_t* io, int frames)
    {
        double eL = 0, eR = 0, eLR = 0;
        for (int i = 0; i < frames; ++i) {
            const double l = io[2 * i], r = io[2 * i + 1];
            eL += l * l;
            eR += r * r;
            eLR += l * r;
        }
        const double eS = 0.5 * (eL + eR); // Leistung des Stereosignals (Mittel der Kanaele)
        if (eS > 1e-3 * frames) {
            const double corr = eLR / std::sqrt(eL * eR + 1e-9);
            corrSmooth_       = corrSmooth_ * 0.7 + corr * 0.3;
        }
        const float targetSign = corrSmooth_ < -0.2 ? -1.0f : 1.0f; // gegenphasig: Differenz statt Summe
        // Leistung der gewaehlten Summe, daraus Ausgleichs-Gain (>= 1, hoechstens +3 dB)
        const double eM    = 0.25 * (eL + eR + 2.0 * targetSign * eLR);
        float        gainT = 1.0f;
        if (eS > 1e-3 * frames && eM > 1e-9) gainT = static_cast<float>(std::clamp(std::sqrt(eS / eM), 1.0, 1.4142));
        const float gain0 = gain_, sign0 = sign_;
        for (int i = 0; i < frames; ++i) { // Gain und Vorzeichen ueber den Block weich ueberblenden (kein Knacken)
            const float t    = static_cast<float>(i + 1) / static_cast<float>(frames);
            const float gain = gain0 + (gainT - gain0) * t, sign = sign0 + (targetSign - sign0) * t;
            const float m    = 0.5f * (io[2 * i] + sign * io[2 * i + 1]) * gain;
            const int16_t o  = static_cast<int16_t>(std::clamp(m, -32768.0f, 32767.0f));
            io[2 * i] = io[2 * i + 1] = o;
        }
        gain_ = gainT;
        sign_ = targetSign;
    }

    double sr_ = 48000.0;
    float  hpCoef_ = 0.9967f, atkCoef_ = 0.02f, relCoef_ = 0.00026f;
    float  hpX_[2] = {0, 0}, hpY_[2] = {0, 0};
    float  lim_ = 1.0f, gain_ = 1.0f, sign_ = 1.0f;
    double corrSmooth_ = 1.0;
};
