
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
// Stimmen des Voicechangers: die Klang-Parameter, die mitgelieferten Stimmen und die eigenen, benannten Stimmen. Reine Logik ohne Windows-Abhaengigkeit, damit sie testbar ist.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "presets.h"

struct VoiceParams {
    float pitch   = 0.0f;  // Tonhoehe in Halbtoenen, -12..+12 (Formanten bleiben dabei stehen)
    float formant = 0.0f;  // Stimmcharakter in Halbtoenen, -12..+12 (Tonhoehe bleibt dabei stehen)
    float gainDb  = 0.0f;  // Lautstaerke des Ergebnisses, -24..+24 dB
    int   wetPct  = 100;   // Dry/Wet: 0 = nur Originalstimme, 100 = nur Effekt
    bool  mirror  = false; // Stimme spiegeln: tiefe Frequenzen werden hoch und umgekehrt
    bool  invert  = false; // Polaritaet des Effekt-Anteils umdrehen (hoerbar nur im Mix mit der Originalstimme)
};

namespace voicepresets {

constexpr size_t kMaxUser = 50;

inline VoiceParams Clamped(VoiceParams p)
{
    p.pitch   = std::clamp(p.pitch, -12.0f, 12.0f);
    p.formant = std::clamp(p.formant, -12.0f, 12.0f);
    p.gainDb  = std::clamp(p.gainDb, -24.0f, 24.0f);
    p.wetPct  = std::clamp(p.wetPct, 0, 100);
    return p;
}

inline bool Same(const VoiceParams& a, const VoiceParams& b)
{
    return std::fabs(a.pitch - b.pitch) < 0.05f && std::fabs(a.formant - b.formant) < 0.05f && std::fabs(a.gainDb - b.gainDb) < 0.05f && a.wetPct == b.wetPct && a.mirror == b.mirror &&
           a.invert == b.invert;
}

struct Voice {
    std::wstring name;
    VoiceParams  params;
};

// Mitgelieferte Stimmen; die deutschen Namen sind zugleich die Schluessel der Uebersetzungstabelle.
inline const std::vector<Voice>& Builtins()
{
    static const std::vector<Voice> list = {
        {L"Unverändert", {}},
        {L"Tiefe Stimme", {-4.0f, -2.0f, 0.0f, 100, false, false}},
        {L"Helle Stimme", {4.0f, 2.0f, 0.0f, 100, false, false}},
        {L"Monster", {-9.0f, -5.0f, 2.0f, 100, false, false}},
        {L"Kobold", {7.0f, 6.0f, -2.0f, 100, false, false}},
        {L"Spiegel", {0.0f, 0.0f, 0.0f, 100, true, false}},
    };
    return list;
}

inline const Voice* FindBuiltin(const std::wstring& name)
{
    for (const auto& v : Builtins())
        if (presets::EqualNoCase(v.name, name)) return &v;
    return nullptr;
}

class Book {
public:
    std::vector<Voice> items;

    const Voice* Find(const std::wstring& name) const
    {
        for (const auto& v : items)
            if (presets::EqualNoCase(v.name, name)) return &v;
        return nullptr;
    }

    // Neue Stimme anlegen; gleicher Name = vorhandene aktualisieren. Namen der mitgelieferten Stimmen sind reserviert.
    PresetResult Save(const std::wstring& rawName, const VoiceParams& params)
    {
        const std::wstring name = presets::Sanitize(rawName);
        if (name.empty()) return PresetResult::Empty;
        if (FindBuiltin(name)) return PresetResult::Reserved;
        for (auto& v : items)
            if (presets::EqualNoCase(v.name, name)) {
                v.name   = name; // Gross-/Kleinschreibung uebernehmen
                v.params = Clamped(params);
                return PresetResult::Updated;
            }
        if (items.size() >= kMaxUser) return PresetResult::Full;
        items.push_back({name, Clamped(params)});
        return PresetResult::Ok;
    }

    PresetResult Rename(const std::wstring& from, const std::wstring& rawTo)
    {
        const std::wstring to = presets::Sanitize(rawTo);
        if (to.empty()) return PresetResult::Empty;
        if (FindBuiltin(to)) return PresetResult::Reserved;
        Voice* src = nullptr;
        for (auto& v : items)
            if (presets::EqualNoCase(v.name, from)) src = &v;
        if (!src) return PresetResult::NotFound;
        for (const auto& v : items)
            if (&v != src && presets::EqualNoCase(v.name, to)) return PresetResult::Exists;
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

    // "Stimme 1", "Stimme 2", ... (kleinste freie Nummer)
    std::wstring SuggestName() const
    {
        for (int n = 1;; ++n) {
            const std::wstring name = L"Stimme " + std::to_wstring(n);
            if (!Find(name)) return name;
        }
    }
};

} // namespace voicepresets
