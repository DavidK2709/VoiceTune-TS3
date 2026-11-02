// Selbsttest der Wiedergabeliste (Logik ohne Windows-Abhaengigkeit).
#include <cstdio>

#include "playlist.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

int main()
{
    using namespace playlist;
    CHECK(IsAudioFile(L"C:\\Musik\\Song.MP3") && IsAudioFile(L"a.flac") && IsAudioFile(L"x.Opus") && IsAudioFile(L"/tmp/a.b/c.wav"), "Endungen");
    CHECK(!IsAudioFile(L"C:\\a.dir\\readme") && !IsAudioFile(L"bild.jpg") && !IsAudioFile(L"datei.") && !IsAudioFile(L"nur.mp3x"), "keine Audiodatei");
    CHECK(TitleFromPath(L"C:\\Musik\\Mein Lied.v2.mp3") == L"Mein Lied.v2" && TitleFromPath(L"x.wav") == L"x" && TitleFromPath(L"a/b/c") == L"c", "Titel");
    CHECK(FormatTime(-1) == L"--:--" && FormatTime(0) == L"0:00" && FormatTime(187.4) == L"3:07" && FormatTime(3729) == L"1:02:09", "Zeitformat");

    Playlist p;
    CHECK(p.Add(L"a.mp3") == 0 && p.Add(L"b.wav") == 1 && p.Add(L"c.flac") == 2 && p.Add(L"d.txt") == -1 && p.items.size() == 3, "Add");

    // Wiedergabe bis zum Ende
    p.current = 0;
    CHECK(p.AfterEnd() == 1 && p.Next() == 1 && p.Prev() == 0, "Mitte");
    p.current = 2;
    CHECK(p.AfterEnd() == -1 && p.Next() == -1 && p.Prev() == 1, "Ende ohne Wiederholen");
    p.repeat = RepeatMode::All;
    CHECK(p.AfterEnd() == 0 && p.Next() == 0, "Alle wiederholen: Ende -> Anfang");
    p.current = 0;
    CHECK(p.Prev() == 2, "Alle wiederholen: Anfang -> Ende");
    p.repeat  = RepeatMode::One;
    p.current = 1;
    CHECK(p.AfterEnd() == 1 && p.Next() == 2, "Eins wiederholen: Ende wiederholt, Weiter springt");
    p.repeat = RepeatMode::Off;

    // Entfernen
    p.current = 1;
    CHECK(p.Remove(0) && p.current == 0 && p.items[0].title == L"b", "vor dem aktuellen entfernen");
    CHECK(p.Remove(0) && p.current == -1 && p.items.size() == 1 && p.AfterEnd() == 0, "aktuellen entfernen: naechster rueckt nach");
    CHECK(!p.Remove(5) && !p.Remove(-1), "ungueltiger Index");
    p.Clear();
    CHECK(p.items.empty() && p.current == -1 && p.AfterEnd() == -1 && p.Next() == -1 && p.Prev() == -1, "leer");

    // Obergrenze
    for (size_t i = 0; i < Playlist::kMax + 10; ++i) p.Add(L"x" + std::to_wstring(i) + L".mp3");
    CHECK(p.items.size() == Playlist::kMax, "Maximum %zu", p.items.size());

    std::printf(g_fail ? "%d FEHLER\n" : "playlist_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
