
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
 
// Selbsttest des Datei-Players: dekodiert Testdateien (3 s 440 Hz, danach 3 s 880 Hz) in mehreren Formaten und prueft Laenge, Frequenz, Springen, Pause und Stop.
// Aufruf: player_test.exe <ordner-mit-tone.*>   (Dateien erzeugt scripts/make_test_audio.sh bzw. ffmpeg)
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "file_player.h"
#include "playlist.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

// Frequenz aus Nulldurchgaengen (linker Kanal)
static double Freq(const std::vector<int16_t>& pcm, size_t fromFrame, size_t frames)
{
    if (fromFrame + frames > pcm.size() / 2) return -1;
    int crossings = 0;
    for (size_t i = fromFrame + 1; i < fromFrame + frames; ++i)
        if (pcm[2 * (i - 1)] < 0 && pcm[2 * i] >= 0) ++crossings;
    return crossings * 48000.0 / frames;
}

// Spielt bis zum Ende (oder Zeitlimit) und liefert alles, was im Ring ankam.
static std::vector<int16_t> DecodeAll(const std::wstring& path, double* duration, bool* failed)
{
    FrameRing   ring(48000);
    FilePlayer  p(ring);
    p.Start(path);
    std::vector<int16_t> pcm, block(4800 * 2);
    DWORD                start = GetTickCount();
    while (GetTickCount() - start < 20000) {
        const size_t n = ring.ReadUpTo(block.data(), 4800);
        pcm.insert(pcm.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n * 2));
        if (!n) {
            if (p.Ended()) break;
            Sleep(2);
        }
    }
    *duration = p.Duration();
    *failed   = p.Failed();
    p.Stop();
    return pcm;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    auto wide = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };

    struct F { const char* ext; bool mfDuration; };
    for (const F& f : {F{"wav", true}, F{"mp3", true}, F{"flac", true}, F{"m4a", true}, F{"ogg", false}, F{"opus", false}}) {
        const std::wstring path = wide(dir + "\\tone." + f.ext);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::printf("%-5s uebersprungen (Datei fehlt)\n", f.ext);
            continue;
        }
        double dur;
        bool   failed;
        auto   pcm = DecodeAll(path, &dur, &failed);
        const double secs = pcm.size() / 2 / 48000.0;
        const double f1 = Freq(pcm, 24000, 24000), f2 = Freq(pcm, 48000 * 4, 24000);
        std::printf("%-5s %5.2f s dekodiert, Laenge laut Datei %5.2f s, Frequenz Anfang %.0f Hz / Ende %.0f Hz%s\n", f.ext, secs, dur, f1, f2, failed ? "  FEHLER" : "");
        CHECK(!failed, "%s: Fehler", f.ext);
        CHECK(std::fabs(secs - 6.0) < 0.35, "%s: Laenge %.2f s", f.ext, secs);
        CHECK(std::fabs(f1 - 440) < 15, "%s: Anfang %.0f Hz", f.ext, f1);
        CHECK(std::fabs(f2 - 880) < 30, "%s: Ende %.0f Hz", f.ext, f2);
        if (f.mfDuration) CHECK(std::fabs(dur - 6.0) < 0.35, "%s: Dauer %.2f s", f.ext, dur);
        CHECK(std::fabs(FilePlayer::ProbeDuration(path) - (f.mfDuration ? 6.0 : -1.0)) < (f.mfDuration ? 0.35 : 0.01), "%s: ProbeDuration", f.ext);
    }

    // Springen, Pause, Stop (mit mp3)
    for (const char* ext : {"mp3", "ogg"}) {
        const std::wstring path = wide(dir + "\\tone." + ext);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        FrameRing  ring(48000);
        FilePlayer p(ring);
        p.Start(path);
        std::vector<int16_t> pcm, block(4800 * 2);
        // Verbrauch in Echtzeit wie im Plugin (Mikrofon-Callback): nie schneller als 48 000 Frames pro Sekunde
        long long consumed = 0;
        auto drain = [&](DWORD ms) {
            const DWORD t0 = GetTickCount();
            long long   budget0 = consumed;
            while (GetTickCount() - t0 < ms) {
                const long long allowed = budget0 + static_cast<long long>(GetTickCount() - t0) * 48 - consumed;
                if (allowed > 0) {
                    const size_t n = ring.ReadUpTo(block.data(), static_cast<size_t>(std::min<long long>(allowed, 4800)));
                    pcm.insert(pcm.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n * 2));
                    consumed += static_cast<long long>(n);
                }
                Sleep(2);
            }
        };
        drain(500);
        const double f0 = Freq(pcm, 4800, 12000);
        CHECK(std::fabs(f0 - 440) < 15, "%s: vor dem Springen %.0f Hz", ext, f0);

        p.SetPaused(true);
        Sleep(200);
        ring.Clear();
        Sleep(400);
        CHECK(ring.Size() == 0, "%s: im Pausezustand darf nichts nachkommen (%zu)", ext, ring.Size());
        p.SetPaused(false);
        pcm.clear();
        drain(400);
        std::printf("%-5s nach Pause: %zu Samples, Ring %zu, Position %.2f s, Running=%d\n", ext, pcm.size(), ring.Size(), p.Position(), p.Running());
        CHECK(pcm.size() > 2 * 12000 && ring.Size() < 20000, "%s: nach Pause geht es weiter", ext);


        p.Seek(4.0); // in den 880-Hz-Teil
        ring.Clear();
        pcm.clear();
        drain(1200);
        const double f1 = pcm.size() > 2 * 24000 ? Freq(pcm, 12000, 12000) : -1;
        const double pos = p.Position();
        std::printf("%-5s nach Sprung auf 4,0 s: %.0f Hz, Position %.2f s\n", ext, f1, pos);
        CHECK(std::fabs(f1 - 880) < 30, "%s: nach dem Springen %.0f Hz", ext, f1);
        CHECK(pos > 3.9 && pos < 5.2, "%s: Position %.2f", ext, pos);

        const DWORD t0 = GetTickCount();
        p.Stop();
        CHECK(GetTickCount() - t0 < 1000, "%s: Stop dauerte %lu ms", ext, GetTickCount() - t0);
    }

    // Fehlerfall: keine Audiodatei
    {
        FrameRing  ring(48000);
        FilePlayer p(ring);
        bool       ended = false;
        p.SetOnEnd([&] { ended = true; });
        p.Start(wide(dir + "\\gibt-es-nicht.mp3"));
        for (int i = 0; i < 100 && !p.Ended(); ++i) Sleep(20);
        CHECK(p.Ended() && p.Failed() && !p.Error().empty() && ended, "fehlende Datei: Ended=%d Failed=%d", p.Ended(), p.Failed());
        p.Stop();
    }

    std::printf(g_fail ? "%d FEHLER\n" : "player_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
