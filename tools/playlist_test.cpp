
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
 
// Selbsttest der Wiedergabeliste (Logik ohne Windows-Abhaengigkeit).
#include <cstdio>

#include "playlist.h"
#include "playlists.h"

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

    // Benannte Wiedergabelisten (PlaylistBook) und das INI-Format (Pfade mit '|')
    {
        using playlists::PlaylistBook;
        PlaylistBook b;
        CHECK(b.Save(L"  Party  ", {L"C:\\a.mp3", L"C:\\b.mp3"}) == PresetResult::Ok && b.items.size() == 1 && b.items[0].name == L"Party", "Speichern (Name getrimmt)");
        CHECK(b.Save(L"party", {L"C:\\c.mp3"}) == PresetResult::Updated && b.items.size() == 1 && b.items[0].paths.size() == 1 && b.items[0].name == L"party", "gleicher Name = aktualisieren");
        CHECK(b.Save(L"   ", {}) == PresetResult::Empty, "leerer Name");
        CHECK(b.Save(L"Ruhig", {}) == PresetResult::Ok && b.Find(L"RUHIG") != nullptr, "leere Liste, Suche ohne Gross/klein");
        CHECK(b.Rename(L"ruhig", L"Party") == PresetResult::Exists && b.Rename(L"nix", L"x") == PresetResult::NotFound, "Umbenennen: Konflikt / unbekannt");
        CHECK(b.Rename(L"ruhig", L"Abend") == PresetResult::Ok && b.Find(L"Abend") && !b.Find(L"Ruhig"), "Umbenennen");
        CHECK(b.Delete(L"ABEND") && !b.Delete(L"Abend") && b.items.size() == 1, "Loeschen");
        CHECK(b.SuggestName(L"Liste") == L"Liste 1", "Namensvorschlag");
        for (size_t i = 0; i < playlists::kMaxLists + 5; ++i) b.Save(L"L" + std::to_wstring(i), {});
        CHECK(b.items.size() == playlists::kMaxLists && b.Save(L"zuviel", {}) == PresetResult::Full, "hoechstens %zu Listen", playlists::kMaxLists);
        std::vector<std::wstring> big(playlists::kMaxPaths + 3, L"x.mp3");
        CHECK(b.Save(L"L0", big) == PresetResult::Updated && b.Find(L"L0")->paths.size() == playlists::kMaxPaths, "hoechstens %zu Pfade", playlists::kMaxPaths);

        const std::vector<std::wstring> paths = {L"C:\\Musik\\Ä b.mp3", L"D:\\x\\y.flac"};
        CHECK(playlists::Join(paths) == L"C:\\Musik\\Ä b.mp3|D:\\x\\y.flac" && playlists::Split(playlists::Join(paths)) == paths, "Join/Split");
        CHECK(playlists::Split(L"").empty() && playlists::Split(L"|a.mp3||").size() == 1, "Split: leere Teile ignorieren");
    }

    std::printf(g_fail ? "%d FEHLER\n" : "playlist_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
