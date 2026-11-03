
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
 
// Selbsttest fuer mixer.h und ring_buffer.h (keine Abhaengigkeiten).
#include <cstdio>
#include <cstdlib>

#include <vector>

#include "gate.h"
#include "mixer.h"
#include "ring_buffer.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main()
{
    // Ring: Reihenfolge, Wrap-Around, Ueberlauf verwirft Aeltestes, ReadExact liest nur volle Bloecke
    FrameRing ring(8);
    int16_t   in[24], out[24];
    for (int i = 0; i < 24; ++i) in[i] = static_cast<int16_t>(i);
    ring.Write(in, 6);
    CHECK(ring.Size() == 6);
    CHECK(ring.ReadExact(out, 7) == 0);
    CHECK(ring.ReadExact(out, 4) == 4 && out[0] == 0 && out[7] == 7);
    ring.Write(in + 12, 6); // 2 + 6 = 8 Frames, umgebrochen
    CHECK(ring.Size() == 8);
    CHECK(ring.ReadExact(out, 8) == 8 && out[0] == 8 && out[3] == 11 && out[4] == 12 && out[15] == 23);
    ring.Write(in, 6);
    ring.Write(in + 12, 6); // Ueberlauf: 12 > 8 -> die 4 aeltesten Frames fliegen raus
    CHECK(ring.Size() == 8);
    CHECK(ring.ReadExact(out, 8) == 8 && out[0] == 8);
    ring.Write(nullptr, 3);
    CHECK(ring.ReadExact(out, 3) == 3 && out[0] == 0 && out[5] == 0);
    ring.Write(in, 8);
    ring.TrimTo(2);
    CHECK(ring.Size() == 2);

    // Mixer: Mono-Downmix, Stereo, Gains, Soft-Clip ohne Wrap-Around
    int16_t music[4] = {1000, 3000, -1000, -3000}; // 2 Stereo-Frames
    int16_t mono[2]  = {100, 200};
    int     peak     = mixer::MixInto(mono, 2, 1, music, 2, 1.0f, 1.0f);
    CHECK(mono[0] == 100 + 2000 && mono[1] == 200 - 2000);
    CHECK(peak == 3000);

    int16_t stereo[4] = {10, 20, 30, 40};
    mixer::MixInto(stereo, 2, 2, music, 2, 0.5f, 2.0f);
    CHECK(stereo[0] == 5 + 2000 && stereo[1] == 10 + 6000 && stereo[2] == 15 - 2000 && stereo[3] == 20 - 6000);

    int16_t loud[2]      = {30000, -30000};
    int16_t loudMusic[4] = {30000, 30000, -30000, -30000};
    mixer::MixInto(loud, 2, 1, loudMusic, 2, 1.0f, 1.0f);
    CHECK(loud[0] > 30000 && loud[0] <= 32767);   // begrenzt, kein Wrap in negative Werte
    CHECK(loud[1] < -30000 && loud[1] >= -32767);

    int16_t onlyMic[2] = {1000, -1000};
    mixer::MixInto(onlyMic, 2, 1, nullptr, 0, 0.5f, 1.0f);
    CHECK(onlyMic[0] == 500 && onlyMic[1] == -500);

    // Mikro-Gate: Rauschen (-60 dBFS) wird zugedreht, Sprache (-25 dBFS) kommt unveraendert durch, Nachlauf haelt Wortenden
    {
        auto block = [](int amp) {
            std::vector<int16_t> b(960);
            for (int i = 0; i < 960; ++i) b[i] = static_cast<int16_t>(amp * std::sin(i * 0.3));
            return b;
        };
        MicGate g;
        g.Configure(-50.0f);
        auto noise = block(30); // ~ -63 dBFS
        for (int k = 0; k < 10; ++k) g.Process(noise.data(), 960, 1);
        CHECK(g.Gain() == 0.0f);
        auto n2 = block(30);
        g.Process(n2.data(), 960, 1);
        CHECK(n2[500] == 0 && n2[900] == 0);

        auto voice = block(1800); // ~ -25 dBFS
        g.Process(voice.data(), 960, 1);
        CHECK(g.Gain() == 1.0f); // in 5 ms voll offen
        auto v2 = block(1800);
        auto ref = v2;
        g.Process(v2.data(), 960, 1);
        CHECK(v2 == ref);

        for (int k = 0; k < 10; ++k) { auto q = block(30); g.Process(q.data(), 960, 1); } // 200 ms danach: Nachlauf haelt noch offen
        CHECK(g.Gain() == 1.0f);
        for (int k = 0; k < 10; ++k) { auto q = block(30); g.Process(q.data(), 960, 1); }
        CHECK(g.Gain() == 0.0f);

        MicGate off;
        off.Configure(MicGate::kOffDb);
        auto raw = block(30), cp = raw;
        off.Process(cp.data(), 960, 1);
        CHECK(cp == raw);
    }

    std::printf(g_fail ? "%d FEHLER\n" : "alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
