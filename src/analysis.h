#pragma once
// Live-Daten fuer die Anzeige: letzte Mono-Samples (fuer das Spektrum) und eine Huellkurve (Min/Max je 20 ms) fuer die Tonspur.
// Der Audio-Thread schreibt, die Oberflaeche liest; die kritischen Abschnitte sind winzig.
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

class Analysis {
public:
    static constexpr int kSamples     = 16384; // Mono-Ring (342 ms)
    static constexpr int kEnvelope    = 512;   // Spalten der Tonspur (10,2 s)
    static constexpr int kColumnFrames = 960;  // 20 ms je Spalte

    Analysis() : mono_(kSamples, 0.0f), envMin_(kEnvelope, 0.0f), envMax_(kEnvelope, 0.0f) {}

    void Push(const int16_t* stereo, int frames)
    {
        std::lock_guard<std::mutex> lk(m_);
        for (int i = 0; i < frames; ++i) {
            const float m = (stereo[2 * i] + stereo[2 * i + 1]) * (0.5f / 32768.0f);
            mono_[w_]     = m;
            w_            = (w_ + 1) % kSamples;
            accMin_       = std::min(accMin_, m);
            accMax_       = std::max(accMax_, m);
            if (++accFrames_ == kColumnFrames) {
                envMin_[envW_] = accMin_;
                envMax_[envW_] = accMax_;
                envW_          = (envW_ + 1) % kEnvelope;
                accMin_ = 1, accMax_ = -1, accFrames_ = 0;
            }
        }
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lk(m_);
        std::fill(mono_.begin(), mono_.end(), 0.0f);
        std::fill(envMin_.begin(), envMin_.end(), 0.0f);
        std::fill(envMax_.begin(), envMax_.end(), 0.0f);
        accMin_ = 1, accMax_ = -1, accFrames_ = 0;
    }

    // Neueste n Mono-Samples, aelteste zuerst.
    void CopyRecent(float* out, int n) const
    {
        std::lock_guard<std::mutex> lk(m_);
        n = std::min(n, kSamples);
        for (int i = 0; i < n; ++i) out[i] = mono_[(w_ + kSamples - n + i) % kSamples];
    }

    // Neueste n Huellkurven-Spalten, aelteste zuerst.
    void CopyEnvelope(float* mn, float* mx, int n) const
    {
        std::lock_guard<std::mutex> lk(m_);
        n = std::min(n, kEnvelope);
        for (int i = 0; i < n; ++i) {
            const int k = (envW_ + kEnvelope - n + i) % kEnvelope;
            mn[i]       = envMin_[k];
            mx[i]       = envMax_[k];
        }
    }

private:
    mutable std::mutex m_;
    std::vector<float> mono_, envMin_, envMax_;
    int                w_ = 0, envW_ = 0, accFrames_ = 0;
    float              accMin_ = 1, accMax_ = -1;
};
