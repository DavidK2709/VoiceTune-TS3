#pragma once
// Ducking: eine Huellkurve, die die Musik absenkt, solange etwas anderes laeuft (Soundboard-Clip) bzw. du sprichst. Reine Logik ohne Windows-Abhaengigkeit.
// Aufruf im Audio-Thread einmal pro Block: Step() liefert Start- und Endverstaerkung des Blocks, der Aufrufer rampt linear dazwischen (kein Knacken).
#include <algorithm>
#include <cmath>
#include <cstdint>

class Ducker {
public:
    // depthDb: Absenkung in dB (positiv), attackMs: wie schnell abgesenkt wird, releaseMs: wie schnell die Musik zurueckkommt
    void Configure(float depthDb, float attackMs, float releaseMs)
    {
        target_    = std::pow(10.0f, -std::max(0.0f, depthDb) / 20.0f);
        attackMs_  = std::max(1.0f, attackMs);
        releaseMs_ = std::max(1.0f, releaseMs);
    }

    // duck = jetzt absenken? frames = Laenge des Blocks bei 48 kHz. g0/g1 = Verstaerkung am Anfang/Ende des Blocks.
    void Step(bool duck, int frames, float* g0, float* g1)
    {
        *g0                = gain_;
        const float goal   = duck ? target_ : 1.0f;
        const float tauMs  = duck ? attackMs_ : releaseMs_;
        const float coef   = 1.0f - std::exp(-static_cast<float>(frames) / (48.0f * tauMs));
        gain_             += (goal - gain_) * coef;
        if (std::fabs(goal - gain_) < 0.0005f) gain_ = goal;
        *g1 = gain_;
    }

    float Gain() const { return gain_; }
    void  Reset() { gain_ = 1.0f; }

private:
    float gain_      = 1.0f;
    float target_    = 0.25f;
    float attackMs_  = 30.0f;
    float releaseMs_ = 600.0f;
};

// Verstaerkung linear von g0 nach g1 ueber `frames` Frames (interleaved, `channels` Kanaele) auf float-Daten anwenden
inline void ApplyRamp(float* data, int frames, int channels, float g0, float g1)
{
    if (g0 == 1.0f && g1 == 1.0f) return;
    for (int i = 0; i < frames; ++i) {
        const float g = g0 + (g1 - g0) * (static_cast<float>(i) + 1.0f) / static_cast<float>(frames);
        for (int c = 0; c < channels; ++c) data[i * channels + c] *= g;
    }
}

// Sprach-Erkennung am rohen Mikrofonblock: Pegel (RMS, dBFS) ueber der Schwelle = "spricht"; haelt danach holdMs nach.
class SpeechDetector {
public:
    // true = Sprache (jetzt oder innerhalb der Haltezeit)
    bool Process(const int16_t* samples, int frames, int channels, float thresholdDb, int holdMs = 400)
    {
        double sum = 0;
        const long n = static_cast<long>(frames) * channels;
        for (long i = 0; i < n; ++i) sum += static_cast<double>(samples[i]) * samples[i];
        const float db = n > 0 ? 10.0f * std::log10(static_cast<float>(sum / static_cast<double>(n)) / (32768.0f * 32768.0f) + 1e-12f) : -120.0f;
        lastDb_        = db;
        if (db > thresholdDb) holdLeft_ = holdMs * 48; // in Frames
        else holdLeft_ = std::max(0, holdLeft_ - frames);
        return holdLeft_ > 0;
    }
    float LevelDb() const { return lastDb_; }
    void  Reset() { holdLeft_ = 0; lastDb_ = -120.0f; }

private:
    int   holdLeft_ = 0;
    float lastDb_   = -120.0f;
};
