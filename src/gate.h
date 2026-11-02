#pragma once
// Einfaches Noise-Gate fuers Mikrofon. Ersetzt die Sprachaktivierung von TS3, solange das Plugin sie fuer die Musik umgeht:
// Mikro unterhalb der Schwelle wird (weich) stummgeschaltet, mit Nachlauf, damit Wortenden nicht abgeschnitten werden.
#include <algorithm>
#include <cmath>
#include <cstdint>

class MicGate {
public:
    static constexpr float kOffDb = -70.0f; // Schwelle <= kOffDb = Gate aus

    void Configure(float thresholdDb) { threshold_ = thresholdDb; }
    bool Enabled() const { return threshold_ > kOffDb; }
    float Gain() const { return gain_; }

    // Interleaved int16, in place. Pegel = RMS des Blocks in dBFS.
    void Process(int16_t* samples, int frames, int channels, double sampleRate = 48000.0)
    {
        if (!Enabled() || frames <= 0 || channels <= 0) return;
        double sum = 0;
        const int n = frames * channels;
        for (int i = 0; i < n; ++i) sum += static_cast<double>(samples[i]) * samples[i];
        const double db = 10.0 * std::log10(std::max(sum / n, 1.0) / (32768.0 * 32768.0));
        const int hold  = static_cast<int>(0.30 * sampleRate); // 300 ms Nachlauf
        if (db > threshold_) hold_ = hold;
        else hold_ = std::max(0, hold_ - frames);
        const float target  = hold_ > 0 ? 1.0f : 0.0f;
        const float attack  = 1.0f / static_cast<float>(0.005 * sampleRate); // 5 ms auf
        const float release = 1.0f / static_cast<float>(0.060 * sampleRate); // 60 ms zu
        for (int i = 0; i < frames; ++i) {
            if (gain_ < target) gain_ = std::min(target, gain_ + attack);
            else if (gain_ > target) gain_ = std::max(target, gain_ - release);
            if (gain_ >= 1.0f) continue;
            for (int c = 0; c < channels; ++c) samples[i * channels + c] = static_cast<int16_t>(samples[i * channels + c] * gain_);
        }
    }

private:
    float threshold_ = -50.0f;
    float gain_      = 1.0f;
    int   hold_      = 0;
};
