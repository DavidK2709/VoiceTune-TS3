
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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace mixer {

// Weicher Begrenzer: linear bis kThreshold, danach tanh-Kurve in Richtung 32767 (kein Wrap-Around).
inline int16_t SoftClip(float x)
{
    constexpr float kThreshold = 24000.0f;
    constexpr float kRange     = 32767.0f - kThreshold;
    float           a          = std::fabs(x);
    if (a > kThreshold) a = kThreshold + kRange * std::tanh((a - kThreshold) / kRange);
    return static_cast<int16_t>(std::lround(x < 0 ? -a : a));
}

// Spitzenpegel (Betrag) je Kanal eines interleaved Stereo-Blocks.
inline void PeakStereo(const int16_t* stereo, int frames, int* left, int* right)
{
    int l = 0, r = 0;
    for (int i = 0; i < frames; ++i) {
        l = std::max(l, std::abs(static_cast<int>(stereo[2 * i])));
        r = std::max(r, std::abs(static_cast<int>(stereo[2 * i + 1])));
    }
    *left  = l;
    *right = r;
}

// Mischt Stereo-`music` (musicFrames Frames) in das interleaved Mikrofon-Signal `mic` (frames Frames, channels Kanaele).
// Kanal 0 = links, Kanal 1 = rechts, weitere Kanaele bzw. Mono = (L+R)/2. Frames ohne Musik behalten nur die Mikro-Lautstaerke.
// Rueckgabe: Spitzenpegel der (gewichteten) Musik, 0..32767.
inline int MixInto(int16_t* mic, int frames, int channels, const int16_t* music, int musicFrames, float micGain, float musicGain)
{
    int peak = 0;
    for (int i = 0; i < frames; ++i) {
        float l = 0, r = 0;
        if (music && i < musicFrames) {
            l = music[2 * i] * musicGain;
            r = music[2 * i + 1] * musicGain;
            peak = std::max(peak, static_cast<int>(std::max(std::fabs(l), std::fabs(r))));
        }
        for (int c = 0; c < channels; ++c) {
            float m   = mic[i * channels + c] * micGain;
            float add = channels == 1 ? 0.5f * (l + r) : (c == 0 ? l : c == 1 ? r : 0.5f * (l + r));
            mic[i * channels + c] = SoftClip(m + add);
        }
    }
    return std::min(peak, 32767);
}

} // namespace mixer
