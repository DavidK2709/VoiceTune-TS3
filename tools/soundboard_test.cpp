// Selbsttest des Soundboards: Mischen, Lautstaerke, mehrere Stimmen, Saettigung, Neustart, Stopp, Stimmen-Diebstahl, Laden aus Dateien (inkl. Kuerzung auf 30 s).
// Aufruf: soundboard_test.exe [ordner-mit-tone.wav-und-tone.mp3]
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "soundboard.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static std::vector<int16_t> Sine(double seconds, double freq, int amp)
{
    std::vector<int16_t> v(static_cast<size_t>(seconds * 48000) * 2);
    for (size_t i = 0; i < v.size() / 2; ++i) v[2 * i] = v[2 * i + 1] = static_cast<int16_t>(amp * std::sin(2 * M_PI * freq * i / 48000.0));
    return v;
}
static std::vector<int16_t> Constant(double seconds, int value)
{
    return std::vector<int16_t>(static_cast<size_t>(seconds * 48000) * 2, static_cast<int16_t>(value));
}

// Mischt `frames` Frames in Bloecken zu 960 in einen stillen Puffer und liefert ihn
static std::vector<int16_t> Render(Soundboard& sb, int frames, float gain = 1.0f)
{
    std::vector<int16_t> out(static_cast<size_t>(frames) * 2, 0);
    for (int done = 0; done < frames; done += 960) sb.Mix(out.data() + static_cast<size_t>(done) * 2, std::min(960, frames - done), gain);
    return out;
}
static int Peak(const std::vector<int16_t>& v, size_t fromFrame = 0, size_t frames = SIZE_MAX)
{
    int p = 0;
    for (size_t i = fromFrame * 2; i < v.size() && i < (fromFrame + std::min(frames, v.size())) * 2; ++i) p = std::max(p, std::abs(static_cast<int>(v[i])));
    return p;
}
static size_t NonSilentFrames(const std::vector<int16_t>& v)
{
    size_t n = 0;
    for (size_t i = 0; i < v.size() / 2; ++i)
        if (v[2 * i] != 0 || v[2 * i + 1] != 0) ++n;
    return n;
}

static bool WaitFor(Soundboard& sb, int slot, Soundboard::State want, int ms = 8000)
{
    for (int t = 0; t < ms; t += 20) {
        if (sb.GetState(slot) == want) return true;
        Sleep(20);
    }
    return sb.GetState(slot) == want;
}

static void WriteWav(const std::wstring& path, const std::vector<int16_t>& pcm)
{
    struct { char riff[4] = {'R', 'I', 'F', 'F'}; uint32_t size; char wave[4] = {'W', 'A', 'V', 'E'}; char fmt[4] = {'f', 'm', 't', ' '}; uint32_t fmtSize = 16; uint16_t tag = 1, ch = 2;
             uint32_t rate = 48000, bytesPerSec = 48000 * 4; uint16_t align = 4, bits = 16; char data[4] = {'d', 'a', 't', 'a'}; uint32_t dataSize; } h;
    h.dataSize = static_cast<uint32_t>(pcm.size() * 2);
    h.size     = h.dataSize + 36;
    FILE* f    = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    std::fwrite(&h, sizeof(h), 1, f);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
}

int main(int argc, char** argv)
{
    // 1. Ein Clip: Dauer, Pegel, Ende; Active() schon nach Play() (vor dem naechsten Mix), danach nicht mehr
    {
        Soundboard sb;
        sb.LoadMemory(0, Sine(0.5, 1000, 10000));
        CHECK(sb.GetState(0) == Soundboard::State::Ready && std::fabs(sb.Seconds(0) - 0.5) < 0.001, "Clip bereit");
        CHECK(!sb.Active(), "vor Play nichts aktiv");
        sb.Play(0);
        CHECK(sb.Active(), "nach Play aktiv (Ausloeser offen)");
        const auto out = Render(sb, 48000);
        CHECK(Peak(out, 0, 4800) > 9000 && Peak(out, 0, 4800) <= 10000, "Pegel %d", Peak(out, 0, 4800));
        // 1-kHz-Sinus: 1000 der 24000 Frames sind exakt 0
        CHECK(NonSilentFrames(out) >= 22900 && NonSilentFrames(out) <= 24000, "gemischte Frames %zu", NonSilentFrames(out));
        CHECK(Peak(out, 24100, 20000) == 0, "danach Stille");
        CHECK(!sb.Active(), "nach dem Ende nicht mehr aktiv");
    }
    // 2. Verstaerkung
    {
        Soundboard sb;
        sb.LoadMemory(0, Constant(0.2, 8000));
        sb.Play(0);
        const auto half = Render(sb, 4800, 0.5f);
        CHECK(half[100] == 4000 && half[101] == 4000, "Gain 0,5: %d", half[100]);
        sb.Play(0);
        const auto dbl = Render(sb, 4800, 2.0f);
        CHECK(dbl[100] == 16000, "Gain 2,0: %d", dbl[100]);
    }
    // 3. Zwei Stimmen addieren sich, bei Uebersteuerung wird gesaettigt (kein Ueberlauf)
    {
        Soundboard sb;
        sb.LoadMemory(0, Constant(0.2, 9000));
        sb.LoadMemory(1, Constant(0.2, 12000));
        sb.Play(0);
        sb.Play(1);
        auto out = Render(sb, 4800);
        CHECK(out[10] == 21000, "Summe %d", out[10]);
        sb.LoadMemory(2, Constant(0.2, 30000));
        sb.LoadMemory(3, Constant(0.2, 30000));
        sb.Play(2);
        sb.Play(3);
        out = Render(sb, 4800);
        CHECK(out[10] == 32767, "Saettigung %d", out[10]);
        std::vector<int16_t> loud(960 * 2, 20000); // schon vorhandenes Signal (Musik) + Clip
        sb.Play(2);
        sb.Mix(loud.data(), 960, 1.0f);
        CHECK(loud[0] == 32767 && loud[1919] == 32767, "auf vorhandenes Signal gemischt und begrenzt: %d", loud[0]);
    }
    // 4. Neustart desselben Slots: Clip beginnt von vorn
    {
        Soundboard sb;
        std::vector<int16_t> ramp(48000 * 2);
        for (size_t i = 0; i < 48000; ++i) ramp[2 * i] = ramp[2 * i + 1] = static_cast<int16_t>(i / 2); // steigt 0..23999
        sb.LoadMemory(0, ramp);
        sb.Play(0);
        Render(sb, 9600);
        sb.Play(0);
        std::vector<int16_t> out(960 * 2, 0);
        sb.Mix(out.data(), 960, 1.0f);
        CHECK(out[0] == 0 && out[2] == 0 && out[2 * 959] == 479, "Neustart von vorn: %d %d %d", out[0], out[2], out[2 * 959]);
    }
    // 5. StopAll: laufende und noch offene Ausloeser verschwinden
    {
        Soundboard sb;
        sb.LoadMemory(0, Constant(2.0, 5000));
        sb.Play(0);
        Render(sb, 960);
        CHECK(sb.Active(), "laeuft");
        sb.Play(0);
        sb.StopAll();
        const auto out = Render(sb, 4800);
        CHECK(Peak(out) == 0 && !sb.Active(), "nach StopAll still (%d)", Peak(out));
        sb.Play(0); // danach geht es wieder
        CHECK(Peak(Render(sb, 960)) == 5000, "nach StopAll wieder spielbar");
    }
    // 6. Mehr Clips als Stimmen: der am weitesten fortgeschrittene wird ersetzt, nichts bricht
    {
        Soundboard sb;
        for (int i = 0; i < 6; ++i) sb.LoadMemory(i, Constant(1.0, 1000 * (i + 1)));
        sb.Play(0);
        Render(sb, 4800);
        for (int i = 1; i < 6; ++i) {
            sb.Play(i);
            Render(sb, 480);
        }
        const auto out = Render(sb, 960);
        CHECK(Peak(out) > 0 && Peak(out) <= 32767 && sb.Active(), "5+ Clips: weiter hoerbar (%d)", Peak(out));
    }
    // 7. Ungueltige Slots / leere Slots / ungueltige Nummern
    {
        Soundboard sb;
        sb.Play(0);
        sb.Play(-1);
        sb.Play(99);
        sb.LoadMemory(99, Constant(0.1, 1));
        sb.Clear(-3);
        CHECK(!sb.Active() && sb.GetState(0) == Soundboard::State::Empty && sb.GetState(99) == Soundboard::State::Empty, "leere/ungueltige Slots tun nichts");
        sb.LoadMemory(0, std::vector<int16_t>());
        CHECK(sb.GetState(0) == Soundboard::State::Failed, "leerer Clip = Failed");
        sb.LoadMemory(0, Constant(0.1, 1000));
        sb.Clear(0);
        sb.Play(0);
        CHECK(!sb.Active() && sb.GetState(0) == Soundboard::State::Empty, "geleerter Slot spielt nicht");
    }
    // 8. Aus Dateien laden
    if (argc > 1) {
        const std::string  dir = argv[1];
        auto               wide = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };
        Soundboard         sb;
        sb.Load(0, wide(dir + "\\tone.wav"));
        sb.Load(1, wide(dir + "\\tone.mp3"));
        sb.Load(2, wide(dir + "\\gibt-es-nicht.wav"));
        CHECK(sb.GetState(0) == Soundboard::State::Loading || sb.GetState(0) == Soundboard::State::Ready, "Laden laeuft im Hintergrund");
        CHECK(WaitFor(sb, 0, Soundboard::State::Ready) && std::fabs(sb.Seconds(0) - 6.0) < 0.1, "wav geladen: %.2f s", sb.Seconds(0));
        CHECK(WaitFor(sb, 1, Soundboard::State::Ready) && std::fabs(sb.Seconds(1) - 6.0) < 0.3, "mp3 geladen: %.2f s", sb.Seconds(1));
        CHECK(WaitFor(sb, 2, Soundboard::State::Failed), "fehlende Datei = Failed");
        sb.Play(0);
        const auto out = Render(sb, 9600);
        CHECK(Peak(out, 100, 2000) > 300, "Datei klingt (Spitze %d)", Peak(out, 100, 2000));

        // Neu belegen waehrend geladen wird: nur das Letzte zaehlt; Leeren waehrend des Ladens bleibt leer
        sb.Load(3, wide(dir + "\\tone.mp3"));
        sb.Load(3, wide(dir + "\\tone.wav"));
        CHECK(WaitFor(sb, 3, Soundboard::State::Ready), "neu belegt");
        sb.Load(4, wide(dir + "\\tone.mp3"));
        sb.Clear(4);
        Sleep(1500);
        CHECK(sb.GetState(4) == Soundboard::State::Empty && sb.Seconds(4) == 0, "Leeren waehrend des Ladens bleibt leer");

        // Lange Datei (40 s) wird auf 30 s gekuerzt
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::wstring longWav = std::wstring(tmp) + L"aam_sound_long.wav";
        WriteWav(longWav, Sine(40, 440, 5000));
        sb.Load(5, longWav);
        CHECK(WaitFor(sb, 5, Soundboard::State::Ready) && std::fabs(sb.Seconds(5) - Soundboard::kMaxSeconds) < 0.05, "40 s Datei auf 30 s gekuerzt: %.2f s", sb.Seconds(5));
        DeleteFileW(longWav.c_str());
    } else {
        std::printf("(Dateitests uebersprungen: Ordner mit tone.wav/tone.mp3 als Argument angeben)\n");
    }

    std::printf(g_fail ? "%d FEHLER\n" : "soundboard_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
