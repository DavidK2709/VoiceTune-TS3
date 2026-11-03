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
 
#include "board_file.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <sstream>

namespace board {
namespace {

std::wstring Normalize(std::wstring s)
{
    std::replace(s.begin(), s.end(), L'/', L'\\');
    return s;
}

std::vector<std::wstring> Split(const std::wstring& s)
{
    std::vector<std::wstring> out;
    std::wstring              cur;
    for (wchar_t c : s) {
        if (c == L'\\') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool IsAbsolute(const std::wstring& s) { return s.size() >= 3 && s[1] == L':' && s[2] == L'\\'; }

bool EqualNoCase(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return L"";
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

} // namespace

std::wstring RelativePath(const std::wstring& baseDir, const std::wstring& target)
{
    const std::wstring b = Normalize(baseDir), t = Normalize(target);
    if (!IsAbsolute(b) || !IsAbsolute(t)) return target;
    const auto bp = Split(b), tp = Split(t);
    if (bp.empty() || tp.empty() || !EqualNoCase(bp[0], tp[0])) return target; // anderes Laufwerk
    size_t common = 0;
    while (common < bp.size() && common < tp.size() - 1 && EqualNoCase(bp[common], tp[common])) ++common;
    std::wstring out;
    for (size_t i = common; i < bp.size(); ++i) out += L"..\\";
    for (size_t i = common; i < tp.size(); ++i) out += (i > common ? L"\\" : L"") + tp[i];
    return out;
}

std::wstring ResolvePath(const std::wstring& baseDir, const std::wstring& rel)
{
    const std::wstring r = Normalize(rel);
    if (IsAbsolute(r) || (r.size() >= 2 && r[0] == L'\\' && r[1] == L'\\')) return r;
    std::vector<std::wstring> parts = Split(Normalize(baseDir));
    for (const auto& p : Split(r)) {
        if (p == L".") continue;
        if (p == L"..") {
            if (parts.size() > 1) parts.pop_back();
        } else {
            parts.push_back(p);
        }
    }
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? L"\\" : L"") + parts[i];
    return out;
}

std::string FormatBoard(const Data& d, const std::wstring& fileDir)
{
    std::string out = "[VoiceTuneBoard]\r\nVersion=1\r\nRows=" + std::to_string(d.rows) + "\r\nCols=" + std::to_string(d.cols) + "\r\n";
    for (size_t i = 0; i < d.slots.size(); ++i) {
        const SoundSlot& s = d.slots[i];
        if (s.path.empty()) continue;
        char head[40];
        std::snprintf(head, sizeof(head), "%06X|%d|", static_cast<unsigned>(s.color & 0xFFFFFF), s.volPct);
        out += "Slot" + std::to_string(i + 1) + "=" + head + ToUtf8(RelativePath(fileDir, s.path)) + "|" + ToUtf8(s.name) + "\r\n";
    }
    return out;
}

bool ParseBoard(const std::string& utf8, const std::wstring& fileDir, Data* out)
{
    if (!out) return false;
    std::istringstream in(utf8);
    std::string        line;
    bool               header = false;
    Data               d;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3); // BOM
        if (line == "[VoiceTuneBoard]") {
            header = true;
            continue;
        }
        if (!header) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "Rows") d.rows = std::min(kSoundMaxRows, std::max(1, std::atoi(val.c_str())));
        else if (key == "Cols") d.cols = std::min(kSoundMaxCols, std::max(1, std::atoi(val.c_str())));
        else if (key.rfind("Slot", 0) == 0) {
            const int idx = std::atoi(key.c_str() + 4);
            if (idx < 1 || idx > kSoundSlots) continue;
            const size_t a = val.find('|'), b = a == std::string::npos ? a : val.find('|', a + 1), c = b == std::string::npos ? b : val.find('|', b + 1);
            if (c == std::string::npos) continue;
            SoundSlot& s = d.slots[idx - 1];
            s.color      = static_cast<uint32_t>(std::strtoul(val.substr(0, a).c_str(), nullptr, 16)) & 0xFFFFFF;
            s.volPct     = std::min(200, std::max(0, std::atoi(val.substr(a + 1, b - a - 1).c_str())));
            s.path       = ResolvePath(fileDir, FromUtf8(val.substr(b + 1, c - b - 1)));
            s.name       = FromUtf8(val.substr(c + 1));
        }
    }
    if (!header) return false;
    while (d.rows * d.cols > kSoundSlots) (d.rows > d.cols ? d.rows : d.cols)--;
    *out = std::move(d);
    return true;
}

} // namespace board
