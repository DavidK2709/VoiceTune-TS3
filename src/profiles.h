#pragma once
// Profile / Szenen: ein benannter Schnappschuss der Klang- und Quelleneinstellungen (App, Lautstaerken, Mithoeren, Equalizer, Qualitaet, Opus).
// Nicht enthalten: Stumm, "Musik einspielen", Musikmodus/Gate. Reine Logik ohne Windows-Abhaengigkeit, damit sie testbar ist.
#include <string>
#include <vector>

#include "presets.h"

struct Profile {
    std::wstring       name;
    std::wstring       targetExe;
    int                musicVolPct    = 100;
    int                micVolPct      = 100;
    bool               monitor        = false;
    int                monitorDelayMs = 250;
    bool               eqEnabled      = true;
    int                eqBands        = 31;
    std::vector<float> eqGains;
    std::wstring       eqPresetName;
    int                quality         = 1;
    bool               mono            = false;
    bool               optimizeForOpus = true;
    bool               opusPreview     = true;
};

namespace profiles {

constexpr size_t kMaxProfiles = 30;

class ProfileBook {
public:
    std::vector<Profile> items;

    const Profile* Find(const std::wstring& name) const
    {
        for (const auto& p : items)
            if (presets::EqualNoCase(p.name, name)) return &p;
        return nullptr;
    }

    // Neues Profil anlegen; gleicher Name = vorhandenes ueberschreiben. Der Name im Profil wird bereinigt (Sanitize).
    PresetResult Save(const std::wstring& rawName, Profile p)
    {
        const std::wstring name = presets::Sanitize(rawName);
        if (name.empty()) return PresetResult::Empty;
        p.name = name;
        for (auto& it : items)
            if (presets::EqualNoCase(it.name, name)) {
                it = std::move(p); // Gross-/Kleinschreibung uebernehmen
                return PresetResult::Updated;
            }
        if (items.size() >= kMaxProfiles) return PresetResult::Full;
        items.push_back(std::move(p));
        return PresetResult::Ok;
    }

    PresetResult Rename(const std::wstring& from, const std::wstring& rawTo)
    {
        const std::wstring to = presets::Sanitize(rawTo);
        if (to.empty()) return PresetResult::Empty;
        Profile* src = nullptr;
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

    // "Profil 1", "Profil 2", ... (kleinste freie Nummer)
    std::wstring SuggestName() const
    {
        for (int n = 1;; ++n) {
            const std::wstring name = L"Profil " + std::to_wstring(n);
            if (!Find(name)) return name;
        }
    }

    // Name des Profils nach `current` (reihum), fuer den Hotkey "naechstes Profil". Leer, wenn es keine Profile gibt.
    std::wstring NextAfter(const std::wstring& current) const
    {
        if (items.empty()) return L"";
        for (size_t i = 0; i < items.size(); ++i)
            if (presets::EqualNoCase(items[i].name, current)) return items[(i + 1) % items.size()].name;
        return items.front().name;
    }
};

} // namespace profiles
