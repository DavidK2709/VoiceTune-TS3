
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
// Wiedergabeliste fuer Audio-Dateien (reine Logik ohne Windows-Abhaengigkeit, damit sie testbar ist).
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

struct Track {
    std::wstring path;
    std::wstring title;
    double       duration = -1; // Sekunden, -1 = unbekannt
};

enum class RepeatMode { Off, All, One };

namespace playlist {

inline std::wstring ToLowerCopy(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

inline std::wstring Extension(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot   = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    return ToLowerCopy(path.substr(dot + 1));
}

// Dateiendungen, die wir annehmen (Windows-Decoder: mp3, wav, flac, m4a/aac, wma; ffmpeg als Reserve fuer ogg/opus u.a.)
inline bool IsAudioFile(const std::wstring& path)
{
    static const wchar_t* exts[] = {L"mp3", L"wav", L"flac", L"m4a", L"aac", L"wma", L"ogg", L"oga", L"opus", L"mp4", L"mka", L"aiff", L"aif", L"webm", L"mp2", L"wv", L"ac3"};
    const std::wstring    e = Extension(path);
    for (const wchar_t* x : exts)
        if (e == x) return true;
    return false;
}

inline std::wstring TitleFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name  = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot   = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

// "3:07", "1:02:09", "--:--" bei unbekannter Laenge
inline std::wstring FormatTime(double seconds)
{
    if (seconds < 0) return L"--:--";
    const long long t = static_cast<long long>(seconds);
    const int       h = static_cast<int>(t / 3600), m = static_cast<int>(t / 60 % 60), s = static_cast<int>(t % 60);
    wchar_t         buf[32];
    if (h > 0) swprintf(buf, 32, L"%d:%02d:%02d", h, m, s);
    else swprintf(buf, 32, L"%d:%02d", m, s);
    return buf;
}

class Playlist {
public:
    std::vector<Track> items;
    int                current = -1; // Index des laufenden/zuletzt gespielten Titels, -1 = keiner
    RepeatMode         repeat  = RepeatMode::Off;
    static constexpr size_t kMax = 500;

    // Haengt hinten an. Rueckgabe: Index des neuen Eintrags oder -1 (voll / keine Audiodatei).
    int Add(const std::wstring& path)
    {
        if (items.size() >= kMax || !IsAudioFile(path)) return -1;
        items.push_back({path, TitleFromPath(path), -1});
        return static_cast<int>(items.size()) - 1;
    }

    bool Remove(int index)
    {
        if (index < 0 || index >= static_cast<int>(items.size())) return false;
        items.erase(items.begin() + index);
        if (index < current) --current;
        else if (index == current) current = index - 1; // "weiter" landet dann auf dem Titel, der nachgerueckt ist
        return true;
    }

    void Clear()
    {
        items.clear();
        current = -1;
    }

    // Titel nach dem Ende des aktuellen (automatisch). -1 = Ende der Liste.
    int AfterEnd() const
    {
        const int n = static_cast<int>(items.size());
        if (n == 0) return -1;
        if (repeat == RepeatMode::One && current >= 0 && current < n) return current;
        if (current + 1 < n) return current + 1;
        return repeat == RepeatMode::All ? 0 : -1;
    }

    // Knopf "Weiter": wie AfterEnd, aber "Eins wiederholen" springt trotzdem zum naechsten.
    int Next() const
    {
        const int n = static_cast<int>(items.size());
        if (n == 0) return -1;
        if (current + 1 < n) return current + 1;
        return repeat == RepeatMode::All ? 0 : -1;
    }

    // Knopf "Zurueck": zum Anfang des Titels waere Sache des Players; hier der vorherige Titel.
    int Prev() const
    {
        const int n = static_cast<int>(items.size());
        if (n == 0) return -1;
        if (current > 0) return current - 1;
        return repeat == RepeatMode::All ? n - 1 : 0;
    }
};

} // namespace playlist
