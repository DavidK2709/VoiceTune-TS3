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
        sb.Play(Soundboard::kSlots);
        sb.LoadMemory(Soundboard::kSlots, Constant(0.1, 1));
        sb.Clear(-3);
        CHECK(!sb.Active() && sb.GetState(0) == Soundboard::State::Empty && sb.GetState(Soundboard::kSlots) == Soundboard::State::Empty, "leere/ungueltige Slots tun nichts");
        sb.LoadMemory(0, std::vector<int16_t>());
        CHECK(sb.GetState(0) == Soundboard::State::Failed, "leerer Clip = Failed");
        sb.LoadMemory(0, Constant(0.1, 1000));
        sb.Clear(0);
        sb.Play(0);
        CHECK(!sb.Active() && sb.GetState(0) == Soundboard::State::Empty, "geleerter Slot spielt nicht");
    }
    // 7b. 400 Knoepfe, Lautstaerke je Knopf, bis zu 8 gleichzeitige Stimmen
    {
        Soundboard sb;
        static_assert(Soundboard::kSlots == 400 && Soundboard::kVoices == 8, "Soundboard-Groessen");
        sb.LoadMemory(399, Constant(0.2, 8000));
        sb.SetVolume(399, 50);
        sb.Play(399);
        CHECK(sb.GetState(399) == Soundboard::State::Ready && Render(sb, 960)[10] == 4000, "Knopf 400 mit 50 %% Lautstaerke");
        sb.SetVolume(399, 500); // wird auf 200 % begrenzt
        sb.Play(399);
        CHECK(Render(sb, 960)[10] == 16000, "Lautstaerke auf 200 %% begrenzt");
        for (int i = 0; i < 8; ++i) {
            sb.LoadMemory(i, Constant(1.0, 1000));
            sb.Play(i);
        }
        CHECK(Render(sb, 960)[10] == 8000, "8 Stimmen addieren sich: %d", Render(sb, 960)[10]);
    }
    // 7c. Speicherbudget: aeltester Clip wird verworfen; ohne Pfad (Speicher-Clip) ist er danach leer
    {
        Soundboard sb;
        const size_t clipBytes = 48000 * 2 * sizeof(int16_t); // 1 s
        sb.SetBudget(clipBytes * 2 + 100);
        sb.LoadMemory(0, Constant(1.0, 100));
        sb.LoadMemory(1, Constant(1.0, 200));
        sb.Play(0); // Slot 0 ist juenger als Slot 1
        Render(sb, 960);
        sb.LoadMemory(2, Constant(1.0, 300));
        CHECK(sb.GetState(1) == Soundboard::State::Empty && sb.GetState(0) == Soundboard::State::Ready && sb.GetState(2) == Soundboard::State::Ready,
              "aeltester (Slot 2) verworfen: %d %d %d", static_cast<int>(sb.GetState(0)), static_cast<int>(sb.GetState(1)), static_cast<int>(sb.GetState(2)));
        CHECK(sb.CachedBytes() <= clipBytes * 2 + 100, "Budget eingehalten: %zu", sb.CachedBytes());
    }
    // 8. Aus Dateien laden
    if (argc > 1) {
        const std::string  dir = argv[1];
        auto               wide = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };
        Soundboard         sb;
        sb.Load(0, wide(dir + "/tone.wav"));
        sb.Load(1, wide(dir + "/tone.mp3"));
        sb.Load(2, wide(dir + "\\gibt-es-nicht.wav"));
        CHECK(sb.GetState(0) == Soundboard::State::Loading || sb.GetState(0) == Soundboard::State::Ready, "Laden laeuft im Hintergrund");
        CHECK(WaitFor(sb, 0, Soundboard::State::Ready) && std::fabs(sb.Seconds(0) - 6.0) < 0.1, "wav geladen: %.2f s", sb.Seconds(0));
        CHECK(WaitFor(sb, 1, Soundboard::State::Ready) && std::fabs(sb.Seconds(1) - 6.0) < 0.3, "mp3 geladen: %.2f s", sb.Seconds(1));
        CHECK(WaitFor(sb, 2, Soundboard::State::Failed), "fehlende Datei = Failed");
        sb.Play(0);
        const auto out = Render(sb, 9600);
        CHECK(Peak(out, 100, 2000) > 300, "Datei klingt (Spitze %d)", Peak(out, 100, 2000));

        // Neu belegen waehrend geladen wird: nur das Letzte zaehlt; Leeren waehrend des Ladens bleibt leer
        sb.Load(3, wide(dir + "/tone.mp3"));
        sb.Load(3, wide(dir + "/tone.wav"));
        CHECK(WaitFor(sb, 3, Soundboard::State::Ready), "neu belegt");
        sb.Load(4, wide(dir + "/tone.mp3"));
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

        // Register: Pfad merken ohne zu laden; Play laedt im Hintergrund und spielt dann von selbst
        Soundboard reg;
        reg.Register(6, wide(dir + "/tone.wav"));
        CHECK(reg.GetState(6) == Soundboard::State::Unloaded && !reg.Active(), "registriert, nicht geladen");
        reg.Play(6);
        CHECK(reg.Active() || reg.GetState(6) == Soundboard::State::Loading || reg.GetState(6) == Soundboard::State::Ready, "Play auf verworfenem Clip");
        CHECK(WaitFor(reg, 6, Soundboard::State::Ready), "nachgeladen");
        Sleep(100);
        const auto late = Render(reg, 9600);
        CHECK(Peak(late, 100, 2000) > 300, "nachgeladener Clip spielt von selbst (Spitze %d)", Peak(late, 100, 2000));
        reg.Register(6, L"");
        CHECK(reg.GetState(6) == Soundboard::State::Empty, "Register mit leerem Pfad leert");

        // Budget mit Dateien: zwei 6-s-Clips (je 1,1 MB) bei Budget fuer einen -> der aeltere wird verworfen und beim Klick neu geladen
        Soundboard small;
        small.SetBudget(48000 * 2 * sizeof(int16_t) * 7);
        small.Load(0, wide(dir + "/tone.wav"));
        CHECK(WaitFor(small, 0, Soundboard::State::Ready), "Budget-Test: Clip 1");
        small.Load(1, wide(dir + "/tone.wav"));
        CHECK(WaitFor(small, 1, Soundboard::State::Ready), "Budget-Test: Clip 2");
        CHECK(small.GetState(0) == Soundboard::State::Unloaded && small.Seconds(0) > 5.5, "aelterer Clip verworfen (Unloaded, Dauer bleibt bekannt)");
        small.Play(0);
        CHECK(WaitFor(small, 0, Soundboard::State::Ready), "wieder geladen");
        Sleep(100);
        CHECK(Peak(Render(small, 9600), 100, 2000) > 300 && small.GetState(1) == Soundboard::State::Unloaded, "spielt, der andere wurde verworfen");
    } else {
        std::printf("(Dateitests uebersprungen: Ordner mit tone.wav/tone.mp3 als Argument angeben)\n");
    }

    std::printf(g_fail ? "%d FEHLER\n" : "soundboard_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
