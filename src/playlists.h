#pragma once
// Benannte Wiedergabelisten des Datei-Players (Name + Dateipfade). Reine Logik ohne Windows-Abhaengigkeit, damit sie testbar ist.
#include <string>
#include <vector>

#include "presets.h"

struct SavedPlaylist {
    std::wstring              name;
    std::vector<std::wstring> paths;
};

namespace playlists {

constexpr size_t kMaxLists = 50;
constexpr size_t kMaxPaths = 500; // = playlist::Playlist::kMax

class PlaylistBook {
public:
    std::vector<SavedPlaylist> items;

    const SavedPlaylist* Find(const std::wstring& name) const
    {
        for (const auto& p : items)
            if (presets::EqualNoCase(p.name, name)) return &p;
        return nullptr;
    }

    // Neue Liste anlegen; gleicher Name = vorhandene aktualisieren. Eine leere Liste darf gespeichert werden (z.B. als Vorlage).
    PresetResult Save(const std::wstring& rawName, const std::vector<std::wstring>& paths)
    {
        const std::wstring name = presets::Sanitize(rawName);
        if (name.empty()) return PresetResult::Empty;
        std::vector<std::wstring> p = paths;
        if (p.size() > kMaxPaths) p.resize(kMaxPaths);
        for (auto& it : items)
            if (presets::EqualNoCase(it.name, name)) {
                it.name  = name;
                it.paths = std::move(p);
                return PresetResult::Updated;
            }
        if (items.size() >= kMaxLists) return PresetResult::Full;
        items.push_back({name, std::move(p)});
        return PresetResult::Ok;
    }

    PresetResult Rename(const std::wstring& from, const std::wstring& rawTo)
    {
        const std::wstring to = presets::Sanitize(rawTo);
        if (to.empty()) return PresetResult::Empty;
        SavedPlaylist* src = nullptr;
        for (auto& p : items)
            if (presets::EqualNoCase(p.name, from)) src = &p;
        if (!src) return PresetResult::NotFound;
        for (const auto& p : items)
            if (&p != src && presets::EqualNoCase(p.name, to)) return PresetResult::Exists;
        src->name = to;
        return PresetResult::Ok;
    }

    bool Delete(const std::wstring& name)
    {
        for (size_t i = 0; i < items.size(); ++i)
            if (presets::EqualNoCase(items[i].name, name)) {
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        return false;
    }

    // "Liste 1", "Liste 2", ... (kleinste freie Nummer)
    std::wstring SuggestName(const std::wstring& base = L"Liste") const
    {
        for (int n = 1;; ++n) {
            const std::wstring name = base + L" " + std::to_wstring(n);
            if (!Find(name)) return name;
        }
    }
};

// Pfade fuer die INI zu einer Zeile verbinden ('|' kommt in Windows-Pfaden nicht vor) bzw. wieder trennen.
inline std::wstring Join(const std::vector<std::wstring>& paths)
{
    std::wstring out;
    for (const auto& p : paths) {
        if (p.empty()) continue;
        if (!out.empty()) out += L'|';
        out += p;
    }
    return out;
}

inline std::vector<std::wstring> Split(const std::wstring& line)
{
    std::vector<std::wstring> out;
    size_t                    start = 0;
    while (start <= line.size() && out.size() < kMaxPaths) {
        const size_t bar = line.find(L'|', start);
        const std::wstring part = line.substr(start, bar == std::wstring::npos ? std::wstring::npos : bar - start);
        if (!part.empty()) out.push_back(part);
        if (bar == std::wstring::npos) break;
        start = bar + 1;
    }
    return out;
}

} // namespace playlists
