
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
// Update-Hinweis, reine Logik (ohne Netz): Versionen vergleichen und den neuesten Tag aus der Antwort der GitHub-Releases-API lesen.
#include <cstdlib>
#include <string>
#include <vector>

namespace updatelogic {

// "v0.8", "0.8.1", "V1.2-beta" -> {0,8} / {0,8,1} / {1,2}; ohne Ziffern: leer
inline std::vector<int> ParseVersion(const std::wstring& s)
{
    std::vector<int> out;
    size_t           i = 0;
    while (i < s.size() && !(s[i] >= L'0' && s[i] <= L'9')) ++i;
    while (i < s.size()) {
        if (s[i] >= L'0' && s[i] <= L'9') {
            int v = 0;
            while (i < s.size() && s[i] >= L'0' && s[i] <= L'9') v = v * 10 + (s[i++] - L'0');
            out.push_back(v);
            if (i < s.size() && s[i] == L'.') {
                ++i;
                continue;
            }
        }
        break;
    }
    return out;
}

// true, wenn `latest` neuer ist als `current`
inline bool IsNewer(const std::wstring& latest, const std::wstring& current)
{
    const auto a = ParseVersion(latest), b = ParseVersion(current);
    if (a.empty()) return false;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

// Erster "tag_name" einer Release-Liste (die API liefert den neuesten zuerst); leer, wenn keiner da ist
inline std::wstring FirstTag(const std::string& json)
{
    const std::string key = "\"tag_name\"";
    size_t            p   = json.find(key);
    if (p == std::string::npos) return L"";
    p = json.find('"', p + key.size() + 1); // Anfang des Wertes (nach dem Doppelpunkt)
    if (p == std::string::npos) return L"";
    const size_t e = json.find('"', p + 1);
    if (e == std::string::npos || e - p > 40) return L"";
    std::wstring tag;
    for (size_t i = p + 1; i < e; ++i) {
        const unsigned char c = static_cast<unsigned char>(json[i]);
        if (c < 32 || c >= 127) return L"";
        tag.push_back(static_cast<wchar_t>(c));
    }
    return tag;
}

} // namespace updatelogic
