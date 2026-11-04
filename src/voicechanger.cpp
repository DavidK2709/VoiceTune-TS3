
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

#include "voicechanger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "mixer.h"

namespace {
constexpr float kPi        = 3.14159265358979f;
constexpr float kTwoPi     = 2.0f * kPi;
constexpr float kOlaNorm   = 1.0f / 1.5f;  // Hann als Analyse- und Synthesefenster bei Hop N/4: Summe der Quadrate = 1,5
constexpr int   kLifter    = 96;           // Cepstrum, in Samples: Huelle bis 2 ms Quefrenz (kuerzer als die Periode auch hoher Stimmen: 500 Hz), unabhaengig von der Fenstergroesse
constexpr int   kEnvelopePasses = 3;
constexpr float kPeakFloor = 1e-3f;        // Spitzen unter -60 dB relativ zur lautesten gelten als Rauschen des Fensters
constexpr float kMaxEnvGain = 16.0f;       // Huellen-Korrektur hoechstens +-24 dB, sonst wird Rauschen in Luecken hochgezogen
constexpr float kSmooth    = 1.0f / 480.0f; // Regler-Glaettung, Zeitkonstante 10 ms
constexpr float kMixStep   = 1.0f / 480.0f; // Ueberblendung Original <-> Effekt in 10 ms
constexpr float kOnset     = 4.0f;          // Energiesprung um 6 dB gilt als Einsatz: Phasen neu ausrichten (haelt Konsonanten knackig)

inline float WrapPhase(float a) { return a - kTwoPi * std::round(a / kTwoPi); }
} // namespace

VoiceChanger::VoiceChanger()
{
    for (int i = 0; i < kMaxFft / 2; ++i) {
        twRe_[i] = std::cos(kTwoPi * static_cast<float>(i) / kMaxFft);
        twIm_[i] = -std::sin(kTwoPi * static_cast<float>(i) / kMaxFft);
    }
    Configure(kFftShort);
    Reset(false);
}

void VoiceChanger::Configure(int fftSize)
{
    fft_       = fftSize;
    hop_       = fftSize / 4;
    bins_      = fftSize / 2 + 1;
    mask_      = fftSize - 1;
    mirrorBin_ = fftSize == kFftLong ? 169 : 85; // knapp 4 kHz; 1 mod 4 haelt die Phasenfolge beim Spiegeln einfach
    int bits = 0;
    while ((1 << bits) < fft_) ++bits;
    for (int i = 0; i < fft_; ++i) {
        window_[i] = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(fft_)); // periodisches Hann-Fenster
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev_[i] = r;
    }
    const int cut = kLifter;
    for (int n = 0; n < bins_; ++n) // weich auslaufend, sonst schwingt die Huelle
        liftWindow_[n] = n < cut ? (0.5f + 0.5f * std::cos(kPi * static_cast<float>(n) / static_cast<float>(cut))) / static_cast<float>(fft_) : 0.0f;
}

void VoiceChanger::Reset(bool active)
{
    std::memset(in_, 0, sizeof(in_));
    std::memset(ola_, 0, sizeof(ola_));
    std::memset(prevPhase_, 0, sizeof(prevPhase_));
    std::memset(synPhase_, 0, sizeof(synPhase_));
    pos_ = hopCount_ = 0;
    frameIndex_  = 0;
    phaseValid_  = false;
    prevEnergy_  = 0;
    mix_         = active ? 1.0f : 0.0f;
    smoothReady_ = false;
}

// Radix-2 in place (Vorwaerts-Transformation, ohne Normierung)
void VoiceChanger::Fft(float* re, float* im) const
{
    for (int i = 0; i < fft_; ++i) {
        const int j = bitrev_[i];
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= fft_; len <<= 1) {
        const int half = len / 2, step = kMaxFft / len;
        for (int i = 0; i < fft_; i += len) {
            for (int k = 0; k < half; ++k) {
                const float wr = twRe_[k * step], wi = twIm_[k * step];
                const int   a = i + k, b = a + half;
                const float vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr;
                im[b] = im[a] - vi;
                re[a] += vr;
                im[a] += vi;
            }
        }
    }
}

// Spektralhuelle aus mag_ nach env_, "True Envelope" (Imai/Roebel): das Log-Spektrum wird ueber das Cepstrum geglaettet und mehrmals von unten an die Spitzen herangezogen.
// Eine einfache Glaettung laege zwischen Harmonischen und Taeler zu tief und wuerde nah beieinander liegende Formanten verschmelzen.
void VoiceChanger::ComputeEnvelope()
{
    for (int k = 0; k < bins_; ++k) logMag_[k] = std::log(mag_[k] + 1e-6f);
    const float norm = 1.0f / static_cast<float>(fft_);
    for (int pass = 0; pass < kEnvelopePasses; ++pass) {
        std::memcpy(cepRe_, logMag_, sizeof(logMag_));
        for (int k = 1; k < fft_ / 2; ++k) cepRe_[fft_ - k] = cepRe_[k];
        std::memset(cepIm_, 0, sizeof(cepIm_));
        Fft(cepRe_, cepIm_);
        cepRe_[0] *= norm;
        for (int n = 1; n <= fft_ / 2; ++n) cepRe_[n] *= liftWindow_[n];
        for (int n = 1; n < fft_ / 2; ++n) cepRe_[fft_ - n] = cepRe_[n];
        std::memset(cepIm_, 0, sizeof(cepIm_));
        Fft(cepRe_, cepIm_);
        if (pass + 1 < kEnvelopePasses)
            for (int k = 0; k < bins_; ++k) logMag_[k] = std::max(logMag_[k], cepRe_[k]);
    }
    for (int k = 0; k < bins_; ++k) env_[k] = std::exp(std::min(cepRe_[k], 30.0f));
}

// Ein Frame: die letzten fft_ Eingangs-Samples (aeltestes bei pos_) ins Spektrum, veraendern, zurueck und in den Ausgang addieren.
void VoiceChanger::ProcessFrame(const VoiceParams& p)
{
    const bool  shiftPitch = std::fabs(p.pitch) > 0.01f, shiftFormant = std::fabs(p.formant) > 0.01f;
    const unsigned frame   = frameIndex_++;

    float energy = 0;
    for (int i = 0; i < fft_; ++i) {
        re_[i] = in_[(pos_ + i) & mask_] * window_[i];
        energy += re_[i] * re_[i];
    }

    // Neutral: Fenster zweimal angewendet ergibt exakt das Original, die Transformation hin und zurueck waere nur Rechenaufwand
    if (!shiftPitch && !shiftFormant && !p.mirror) {
        for (int i = 0; i < fft_; ++i) ola_[(pos_ + i) & mask_] += re_[i] * window_[i] * kOlaNorm;
        phaseValid_ = false;
        prevEnergy_ = energy;
        return;
    }
    if (energy < 1e-10f) { // Stille: nichts zu tun, danach beginnen die Phasen neu
        phaseValid_ = false;
        prevEnergy_ = energy;
        return;
    }
    if (energy > prevEnergy_ * kOnset) phaseValid_ = false;
    prevEnergy_ = energy;

    std::memset(im_, 0, sizeof(im_));
    Fft(re_, im_);
    float maxMag = 0;
    for (int k = 0; k < bins_; ++k) {
        mag_[k]   = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]);
        phase_[k] = std::atan2(im_[k], re_[k]);
        maxMag    = std::max(maxMag, mag_[k]);
    }

    const float pitchRatio   = std::pow(2.0f, p.pitch / 12.0f);
    const float formantRatio = std::pow(2.0f, p.formant / 12.0f);
    if (shiftPitch || shiftFormant) ComputeEnvelope();
    // Verstaerkung, damit der Inhalt von Bin `src` auf Bin `dst` die Huelle bekommt, die dort hingehoert: die um den Formant-Faktor verschobene Huelle des Originals
    auto envGain = [&](int src, int dst) {
        const float pos = static_cast<float>(dst) / formantRatio;
        const int   i   = std::min(bins_ - 2, static_cast<int>(pos));
        const float t   = std::min(1.0f, pos - static_cast<float>(i));
        const float target = env_[i] + (env_[i + 1] - env_[i]) * t;
        return std::clamp(target / env_[src], 1.0f / kMaxEnvGain, kMaxEnvGain);
    };

    std::memset(yRe_, 0, sizeof(yRe_));
    std::memset(yIm_, 0, sizeof(yIm_));
    if (!shiftPitch) {
        // Nur Formant oder Spiegeln: die Phasen bleiben, wie sie sind, also auch keine Vocoder-Artefakte
        for (int k = 1; k < bins_ - 1; ++k) {
            const float g = shiftFormant ? envGain(k, k) : 1.0f;
            yRe_[k]       = re_[k] * g;
            yIm_[k]       = im_[k] * g;
        }
        std::memcpy(synPhase_, phase_, sizeof(synPhase_));
    } else {
        int nPeaks = 0;
        for (int k = 1; k < bins_ - 1; ++k)
            if (mag_[k] > mag_[k - 1] && mag_[k] >= mag_[k + 1] && mag_[k] > maxMag * kPeakFloor) peaks_[nPeaks++] = k;
        const float binToRad = kTwoPi / static_cast<float>(fft_);
        for (int i = 0; i < nPeaks; ++i) {
            const int peak = peaks_[i];
            // Wahre Frequenz der Spitze aus dem Phasenfortschritt (rad/Sample), ohne Vorgaenger die Bin-Mitte
            float omega = static_cast<float>(peak) * binToRad;
            if (phaseValid_) omega += WrapPhase(phase_[peak] - prevPhase_[peak] - omega * static_cast<float>(hop_)) / static_cast<float>(hop_);
            const int target = static_cast<int>(std::lround(omega * pitchRatio / binToRad));
            if (target < 1 || target >= bins_ - 1) continue;
            // Die ganze Umgebung der Spitze dreht um denselben Winkel mit ("Phasenkopplung"), das haelt die Form des Fenster-Spektrums intakt
            const float rotation = phaseValid_ ? synPhase_[target] + omega * pitchRatio * static_cast<float>(hop_) - phase_[peak] : 0.0f;
            const float cr = std::cos(rotation), ci = std::sin(rotation);
            const int   lo = i == 0 ? 1 : (peaks_[i - 1] + peak + 1) / 2;
            const int   hi = i == nPeaks - 1 ? bins_ - 2 : (peak + peaks_[i + 1]) / 2;
            const int   offset = target - peak;
            for (int k = lo; k <= hi; ++k) {
                const int dst = k + offset;
                if (dst < 1 || dst >= bins_ - 1) continue;
                const float g = envGain(k, dst);
                yRe_[dst] += (re_[k] * cr - im_[k] * ci) * g;
                yIm_[dst] += (re_[k] * ci + im_[k] * cr) * g;
            }
        }
        for (int k = 0; k < bins_; ++k) synPhase_[k] = std::atan2(yIm_[k], yRe_[k]);
    }
    std::memcpy(prevPhase_, phase_, sizeof(prevPhase_));
    phaseValid_ = true;

    if (p.mirror) {
        // Y(k) = e^(j*theta) * konj(Y(K - k)), Inhalt oberhalb von K faellt weg. theta = 2*pi*K*Hop*Frame/N haelt die Frames phasenrichtig zueinander; bei K = 1 mod 4
        // und Hop = N/4 ist das einfach eine Vierteldrehung je Frame.
        static const float kRotRe[4] = {1.0f, 0.0f, -1.0f, 0.0f}, kRotIm[4] = {0.0f, 1.0f, 0.0f, -1.0f};
        const float        tr = kRotRe[frame & 3], ti = kRotIm[frame & 3];
        std::memset(re_, 0, sizeof(re_));
        std::memset(im_, 0, sizeof(im_));
        for (int k = 1; k < mirrorBin_; ++k) {
            const float sr = yRe_[mirrorBin_ - k], si = -yIm_[mirrorBin_ - k];
            re_[k]         = sr * tr - si * ti;
            im_[k]         = sr * ti + si * tr;
        }
        std::memcpy(yRe_, re_, sizeof(yRe_));
        std::memcpy(yIm_, im_, sizeof(yIm_));
    }

    // Zurueck in den Zeitbereich: konjugiert transformieren, Spektrum dabei hermitesch ergaenzen
    for (int k = 0; k < bins_; ++k) {
        re_[k] = yRe_[k];
        im_[k] = -yIm_[k];
    }
    for (int k = 1; k < fft_ / 2; ++k) {
        re_[fft_ - k] = yRe_[k];
        im_[fft_ - k] = yIm_[k];
    }
    Fft(re_, im_);
    const float norm = kOlaNorm / static_cast<float>(fft_);
    for (int i = 0; i < fft_; ++i) ola_[(pos_ + i) & mask_] += re_[i] * window_[i] * norm;
}

bool VoiceChanger::Process(int16_t* io, int frames, int channels, const VoiceParams& params, bool enabled, bool longWindow)
{
    if (frames <= 0 || channels <= 0) return false;
    if (!enabled && mix_ <= 0.0f) return false;
    if ((fft_ == kFftLong) != longWindow) { // Umschalten: der Puffer passt nicht mehr zum neuen Fenster, also kurz neu fuellen
        const float keepMix = mix_;
        Configure(longWindow ? kFftLong : kFftShort);
        Reset(false);
        mix_ = keepMix;
    }

    const VoiceParams p        = voicepresets::Clamped(params);
    const float       wetGoal  = static_cast<float>(p.wetPct) / 100.0f;
    const float       gainGoal = std::pow(10.0f, p.gainDb / 20.0f);
    const float       polGoal  = p.invert ? -1.0f : 1.0f;
    if (!smoothReady_) { // erster Block nach Reset: direkt auf die Zielwerte
        wet_ = wetGoal;
        gain_ = gainGoal;
        polarity_ = polGoal;
        smoothReady_ = true;
    }
    const float mixGoal = enabled ? 1.0f : 0.0f;

    for (int i = 0; i < frames; ++i) {
        int16_t* frame = io + static_cast<size_t>(i) * channels;
        float    mono  = 0;
        for (int c = 0; c < channels; ++c) mono += frame[c];
        mono /= static_cast<float>(channels) * 32768.0f;

        const float dry = in_[pos_]; // Sample von vor genau einer Fensterlaenge
        const float wet = ola_[pos_];
        in_[pos_]  = mono;
        ola_[pos_] = 0;
        pos_       = (pos_ + 1) & mask_;
        if (++hopCount_ == hop_) {
            hopCount_ = 0;
            ProcessFrame(p);
        }

        wet_ += (wetGoal - wet_) * kSmooth;
        gain_ += (gainGoal - gain_) * kSmooth;
        polarity_ += (polGoal - polarity_) * kSmooth;
        mix_ = mix_ < mixGoal ? std::min(mixGoal, mix_ + kMixStep) : std::max(mixGoal, mix_ - kMixStep);

        const float out = (dry * (1.0f - wet_) + wet * polarity_ * wet_) * gain_ * 32768.0f;
        for (int c = 0; c < channels; ++c) frame[c] = mixer::SoftClip(frame[c] * (1.0f - mix_) + out * mix_);
    }
    if (!enabled && mix_ <= 0.0f) Reset(false); // ganz aus: beim naechsten Einschalten nichts Altes abspielen
    return true;
}
