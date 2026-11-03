
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
// Eigene Equalizer-Presets (Name + Kurve). Reine Logik ohne Windows-Abhaengigkeit, damit sie testbar ist.
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "equalizer.h"

struct EqPreset {
    std::wstring       name;
    int                bands = 31;
    std::vector<float> gains;
};

enum class PresetResult { Ok, Updated, Empty, Reserved, Exists, NotFound, Full };

namespace presets {

constexpr size_t kMaxUser = 50;
constexpr size_t kMaxName = 28;

// Namen putzen: Steuerzeichen raus, Anfuehrungszeichen entschaerfen (die INI wuerde sie abschneiden), trimmen, auf kMaxName kuerzen.
inline std::wstring Sanitize(const std::wstring& in)
{
    std::wstring s;
    for (wchar_t c : in) {
        if (c < 32 || c == 127) continue;
        s += (c == L'"') ? L'\'' : c;
    }
    const size_t a = s.find_first_not_of(L' ');
    if (a == std::wstring::npos) return L"";
    s = s.substr(a, s.find_last_not_of(L' ') - a + 1);
    if (s.size() > kMaxName) {
        s.resize(kMaxName);
        while (!s.empty() && s.back() == L' ') s.pop_back();
    }
    return s;
}

inline bool EqualNoCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}

// Namen der mitgelieferten Presets sind reserviert.
inline bool IsReserved(const std::wstring& name)
{
    for (int i = 0; i < eq::kPresetCount; ++i)
        if (EqualNoCase(name, eq::PresetName(i))) return true;
    return name.empty();
}

class PresetBook {
public:
    std::vector<EqPreset> items;

    const EqPreset* Find(const std::wstring& name) const
    {
        for (const auto& p : items)
            if (EqualNoCase(p.name, name)) return &p;
        return nullptr;
    }

    // Neues Preset anlegen; gleicher Name = vorhandenes aktualisieren.
    PresetResult Save(const std::wstring& rawName, int bands, const std::vector<float>& gains)
    {
        const std::wstring name = Sanitize(rawName);
        if (name.empty()) return PresetResult::Empty;
        if (IsReserved(name)) return PresetResult::Reserved;
        for (auto& p : items)
            if (EqualNoCase(p.name, name)) {
                p.name  = name; // Gross-/Kleinschreibung uebernehmen
                p.bands = bands;
                p.gains = gains;
                return PresetResult::Updated;
            }
        if (items.size() >= kMaxUser) return PresetResult::Full;
        items.push_back({name, bands, gains});
        return PresetResult::Ok;
    }

    PresetResult Rename(const std::wstring& from, const std::wstring& rawTo)
    {
        const std::wstring to = Sanitize(rawTo);
        if (to.empty()) return PresetResult::Empty;
        if (IsReserved(to)) return PresetResult::Reserved;
        EqPreset* src = nullptr;
        for (auto& p : items)
            if (EqualNoCase(p.name, from)) src = &p;
        if (!src) return PresetResult::NotFound;
        for (const auto& p : items)
            if (&p != src && EqualNoCase(p.name, to)) return PresetResult::Exists;
        src->name = to;
        return PresetResult::Ok;
    }

    bool Delete(const std::wstring& name)
    {
        for (size_t i = 0; i < items.size(); ++i)
            if (EqualNoCase(items[i].name, name)) {
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        return false;
    }

    // "Eigener Filter 1", "Eigener Filter 2", ... (kleinste freie Nummer)
    std::wstring SuggestName() const
    {
        for (int n = 1;; ++n) {
            const std::wstring name = L"Eigener Filter " + std::to_wstring(n);
            if (!Find(name)) return name;
        }
    }
};

// Kurve eines Presets auf das aktuelle Raster bringen.
inline std::vector<float> CurveFor(const EqPreset& p, int bands)
{
    std::vector<float> g = p.gains;
    g.resize(p.bands, 0.0f);
    return p.bands == bands ? g : eq::ResampleCurve(p.bands, g.data(), bands);
}

inline bool CurvesMatch(const std::vector<float>& a, const std::vector<float>& b, float tolDb)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::fabs(a[i] - b[i]) > tolDb) return false;
    return true;
}

} // namespace presets
