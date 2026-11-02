// Deterministische Simulation (virtuelle Uhr, kein Sleep): Capture liefert 10-ms-Pakete einer 440-Hz-Sinus-Quelle mit Jitter und
// abweichendem Takt, das "Mikrofon" ruft alle 20 ms (mit Jitter und gelegentlichen Haengern) den Reader auf.
// Vergleicht das alte Verhalten (ReadExact + TrimTo) mit dem JitterReader.  Exit-Code != 0, wenn der neue Reader Aussetzer hat.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "jitter_reader.h"
#include "ring_buffer.h"

struct Result {
    int      gaps = 0;          // Bloecke ohne Audio nach dem Start
    int      jumps = 0;         // Sprung zwischen zwei Samples > Sinus-Steigung
    uint32_t underruns = 0, overruns = 0;
    size_t   minFill = 0, maxFill = 0;
};

struct Scenario {
    const char* name;
    double      producerRate; // effektive Abtastrate der Quelle (Hz)
    double      consumerRate;
    double      prodJitterMs, consJitterMs;
    bool        stalls;
};

static Result Run(const Scenario& sc, bool legacy, double seconds)
{
    constexpr int    kPacket = 480, kBlock = 960;
    constexpr double kAmp = 8000, kFreq = 440, kJumpLimit = 600;
    FrameRing        ring(48000);
    JitterReader     reader(ring);
    std::mt19937     rng(1234);
    std::uniform_real_distribution<double> u(-1.0, 1.0);

    std::vector<int16_t> pkt(kPacket * 2), block(kBlock * 2);
    uint64_t             srcIndex = 0;
    double               tp = 0, tc = 0, lastP = 0, lastC = 0, nextStall = 2.0;
    long                 pk = 0, bk = 0;
    Result               r;
    bool                 started = false;
    int                  prev = 0;
    bool                 havePrev = false;

    auto nextP = [&] { double t = pk * kPacket / sc.producerRate + u(rng) * sc.prodJitterMs / 1000; t = std::max(t, lastP); lastP = t; return t; };
    auto nextC = [&] {
        double t = bk * kBlock / sc.consumerRate + u(rng) * sc.consJitterMs / 1000;
        if (sc.stalls && t > nextStall) { t += 0.012; nextStall += 2.0; } // 12 ms Haenger
        t = std::max(t, lastC);
        lastC = t;
        return t;
    };
    tp = nextP();
    tc = nextC();

    while (tc < seconds) {
        if (tp <= tc) {
            for (int i = 0; i < kPacket; ++i, ++srcIndex) {
                int16_t s = static_cast<int16_t>(kAmp * std::sin(2 * M_PI * kFreq * static_cast<double>(srcIndex) / 48000.0));
                pkt[2 * i] = pkt[2 * i + 1] = s;
            }
            ring.Write(pkt.data(), kPacket);
            ++pk;
            tp = nextP();
        } else {
            bool real;
            if (legacy) {
                ring.TrimTo(std::max<size_t>(5760, kBlock * 3));
                real = ring.ReadExact(block.data(), kBlock) == static_cast<size_t>(kBlock);
                if (!real) std::fill(block.begin(), block.end(), 0);
            } else {
                real = reader.Read(block.data(), kBlock);
            }
            if (tc > 0.5) { // Anlaufphase ignorieren
                if (!real && started) ++r.gaps;
                if (real) started = true;
                if (started)
                    for (int i = 0; i < kBlock; ++i) {
                        int s = block[2 * i];
                        if (havePrev && std::abs(s - prev) > kJumpLimit) ++r.jumps;
                        prev = s;
                        havePrev = true;
                    }
            }
            ++bk;
            tc = nextC();
        }
    }
    r.underruns = reader.underruns;
    r.overruns  = reader.overruns;
    r.minFill   = reader.minFill;
    r.maxFill   = reader.maxFill;
    return r;
}

// Verzoegerungs-Test (Mithoeren): Saegezahn-Quelle mit bekanntem Frame-Index; beim Lesen wird aus dem Wert der Index zurueckgerechnet
// und daraus die tatsaechliche Verzoegerung bestimmt. Optional wird nach `retargetAt` s auf ein neues Ziel umgestellt.
struct DelayResult {
    double meanMs = 0, minMs = 1e9, maxMs = -1e9;
    int    gaps = 0, samples = 0;
};

static DelayResult RunDelay(double targetMs, double newTargetMs, double producerRate, double seconds)
{
    constexpr int kPacket = 480, kBlock = 960, kPeriod = 30000;
    FrameRing     ring(48000 * 3);
    JitterReader  reader(ring, static_cast<size_t>(targetMs * 48));
    std::mt19937  rng(99);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<int16_t> pkt(kPacket * 2), block(kBlock * 2);
    uint64_t             written = 0;
    long                 pk = 0, bk = 0;
    double               lastP = 0, lastC = 0, sum = 0;
    DelayResult          r;
    const double         retargetAt = seconds / 2, measureFrom = seconds - 4;
    bool                 retargeted = false, started = false;

    auto nextP = [&] { double t = std::max(lastP, pk * kPacket / producerRate + u(rng) * 0.003); lastP = t; return t; };
    auto nextC = [&] { double t = std::max(lastC, bk * kBlock / 48000.0 + u(rng) * 0.002); lastC = t; return t; };
    double tp = nextP(), tc = nextC();
    while (tc < seconds) {
        if (tp <= tc) {
            for (int i = 0; i < kPacket; ++i) pkt[2 * i] = pkt[2 * i + 1] = static_cast<int16_t>((written + i) % kPeriod - kPeriod / 2);
            ring.Write(pkt.data(), kPacket);
            written += kPacket;
            ++pk;
            tp = nextP();
        } else {
            if (newTargetMs > 0 && !retargeted && tc > retargetAt) {
                reader.SetTarget(static_cast<size_t>(newTargetMs * 48));
                retargeted = true;
            }
            const bool real = reader.Read(block.data(), kBlock);
            if (real) started = true;
            if (tc > measureFrom) {
                if (!real && started) ++r.gaps;
                const int v = block[0];
                if (real && v > -kPeriod / 2 + 300 && v < kPeriod / 2 - 300) { // nicht an der Zahnflanke (Interpolation)
                    const double wantFrames = (newTargetMs > 0 ? newTargetMs : targetMs) * 48;
                    const long   vmod = v + kPeriod / 2;
                    const long   k    = std::lround((static_cast<double>(written) - wantFrames - vmod) / kPeriod);
                    const double ms   = (static_cast<double>(written) - (vmod + k * kPeriod)) / 48.0;
                    sum += ms;
                    ++r.samples;
                    r.minMs = std::min(r.minMs, ms);
                    r.maxMs = std::max(r.maxMs, ms);
                }
            }
            ++bk;
            tc = nextC();
        }
    }
    r.meanMs = r.samples ? sum / r.samples : 0;
    return r;
}

int main()
{
    int delayFail = 0;
    std::printf("\n%-34s | %-9s | %-26s | %s\n", "Mithoeren: Verzoegerung", "Ziel", "gemessen (Mittel / min-max)", "Aussetzer");
    struct D { const char* name; double target, retarget, rate; } delays[] = {
        {"fest 100 ms", 100, 0, 48000},
        {"fest 500 ms", 500, 0, 48000},
        {"fest 2000 ms", 2000, 0, 48000},
        {"500 ms, Quelle 0,3 % schneller", 500, 0, 48144},
        {"250 -> 1000 ms umgestellt", 250, 1000, 48000},
        {"1500 -> 150 ms umgestellt", 1500, 150, 47856},
    };
    for (const auto& d : delays) {
        DelayResult r = RunDelay(d.target, d.retarget, d.rate, 40);
        const double want = d.retarget > 0 ? d.retarget : d.target;
        std::printf("%-34s | %6.0f ms | %7.1f / %7.1f - %-7.1f ms | %d\n", d.name, want, r.meanMs, r.minMs, r.maxMs, r.gaps);
        if (r.samples < 50 || std::fabs(r.meanMs - want) > 30 || r.gaps) ++delayFail;
    }
    std::printf(delayFail ? "FEHLER: Verzoegerung weicht ab\n" : "ok: Verzoegerung stimmt (+-30 ms), keine Aussetzer\n\n");

    const Scenario scenarios[] = {
        {"Takt gleich, Jitter +-3/+-2 ms", 48000, 48000, 3, 2, false},
        {"Quelle 0,3 % schneller", 48144, 48000, 3, 2, false},
        {"Quelle 0,3 % langsamer", 47856, 48000, 3, 2, false},
        {"Jitter + Haenger (12 ms) alle 2 s", 48000, 48000, 4, 3, true},
    };
    int fail = 0;
    std::printf("%-36s | %-26s | %-34s\n", "Szenario (60 s)", "alt: Aussetzer / Spruenge", "neu: Aussetzer / Spruenge / Unter / Ueber / Fill");
    for (const auto& sc : scenarios) {
        Result a = Run(sc, true, 60), b = Run(sc, false, 60);
        std::printf("%-36s | %10d / %-12d | %6d / %6d / %5u / %5u / %zu-%zu\n", sc.name, a.gaps, a.jumps, b.gaps, b.jumps, b.underruns, b.overruns, b.minFill, b.maxFill);
        if (b.gaps || b.jumps || b.underruns || b.overruns) ++fail;
    }
    std::printf(fail ? "FEHLER: neuer Reader hat Aussetzer\n" : "ok: neuer Reader ohne Aussetzer/Spruenge\n");
    return (fail || delayFail) ? 1 : 0;
}
