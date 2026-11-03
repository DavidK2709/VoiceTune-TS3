
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
 
#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <vector>

namespace {
const wchar_t* kSection = L"VoiceTune";

int Clamp200(int v) { return std::min(200, std::max(0, v)); }

// Die INI-Funktionen von Windows lesen hoechstens 32767 Zeichen je Wert: Wiedergabelisten werden darauf begrenzt (ganze Pfade, der Rest entfaellt).
constexpr size_t kMaxIniLine = 32767;

std::wstring JoinCapped(const std::vector<std::wstring>& paths)
{
    std::wstring out;
    for (const auto& path : paths) {
        if (path.empty()) continue;
        if (out.size() + path.size() + 1 >= kMaxIniLine) break;
        if (!out.empty()) out += L'|';
        out += path;
    }
    return out;
}

// WritePrivateProfileStringW schreibt in eine ANSI-Datei, wenn die INI nicht schon als UTF-16 (mit BOM) vorliegt; Pfade mit Zeichen ausserhalb der
// Windows-Codepage (z.B. japanische Dateinamen) wuerden dann verstuemmelt. Daher: neue INI als UTF-16 anlegen, eine vorhandene ANSI-INI einmalig umwandeln.
void EnsureUnicodeIni(const std::wstring& path)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::string bytes;
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(f, &size) || size.QuadPart > 16 * 1024 * 1024) {
            CloseHandle(f);
            return;
        }
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        const BOOL ok = bytes.empty() || ReadFile(f, &bytes[0], static_cast<DWORD>(bytes.size()), &got, nullptr);
        CloseHandle(f);
        if (!ok || got != bytes.size()) return;
        if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) return; // schon UTF-16
    }
    std::wstring text;
    if (!bytes.empty()) {
        const bool utf8Bom = bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF;
        const char* src  = bytes.data() + (utf8Bom ? 3 : 0);
        const int   len  = static_cast<int>(bytes.size() - (utf8Bom ? 3 : 0));
        const UINT  cp   = utf8Bom ? CP_UTF8 : CP_ACP;
        const int   n    = MultiByteToWideChar(cp, 0, src, len, nullptr, 0);
        if (n <= 0) return;
        text.resize(static_cast<size_t>(n));
        MultiByteToWideChar(cp, 0, src, len, &text[0], n);
    }
    const std::wstring tmp = path + L".tmp";
    HANDLE             o   = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (o == INVALID_HANDLE_VALUE) return;
    const wchar_t bom = 0xFEFF;
    DWORD         put = 0;
    BOOL          ok  = WriteFile(o, &bom, sizeof(bom), &put, nullptr);
    if (ok && !text.empty()) ok = WriteFile(o, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &put, nullptr);
    CloseHandle(o);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp.c_str());
}
bool ValidBands(int n) { return n == 10 || n == 31 || n == 61; }

std::wstring gainsText(const wchar_t* ini, const wchar_t* section, const wchar_t* key)
{
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 1024, ini);
    return buf;
}

// "0.0,3.5,-2.0,..." -> n Werte (fehlende = 0), auf +-12 dB begrenzt
std::vector<float> ParseGains(const std::wstring& text, int n)
{
    std::vector<float> g(n, 0.0f);
    const wchar_t* cur = text.c_str();
    for (int i = 0; i < n && *cur; ++i) {
        wchar_t* end = nullptr;
        double   v   = wcstod(cur, &end);
        if (end == cur) break;
        g[i] = static_cast<float>(std::min(12.0, std::max(-12.0, v)));
        cur  = (*end == L',') ? end + 1 : end;
    }
    return g;
}

std::wstring FormatGains(const std::vector<float>& gains)
{
    std::wstring out;
    for (size_t i = 0; i < gains.size(); ++i) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%.1f", std::fabs(gains[i]) < 0.05f ? 0.0f : gains[i]); // kein "-0.0"
        out += (i ? L"," : L"");
        out += buf;
    }
    return out;
}
} // namespace

Settings LoadSettings(const std::wstring& iniPath, const wchar_t* section)
{
    Settings s;
    const wchar_t* p = iniPath.c_str();
    s.enabled        = GetPrivateProfileIntW(section, L"Enabled", s.enabled, p) != 0;
    s.musicVolPct    = Clamp200(GetPrivateProfileIntW(section, L"MusicVolume", s.musicVolPct, p));
    s.micVolPct      = Clamp200(GetPrivateProfileIntW(section, L"MicVolume", s.micVolPct, p));
    s.alwaysSend     = GetPrivateProfileIntW(section, L"AlwaysSend", s.alwaysSend, p) != 0;
    s.monitor        = GetPrivateProfileIntW(section, L"Monitor", s.monitor, p) != 0;
    s.monitorDelayMs = std::min(2000, std::max(0, static_cast<int>(GetPrivateProfileIntW(section, L"MonitorDelayMs", s.monitorDelayMs, p))));
    s.eqEnabled      = GetPrivateProfileIntW(section, L"EqEnabled", s.eqEnabled, p) != 0;
    s.quality        = std::min(2, std::max(0, static_cast<int>(GetPrivateProfileIntW(section, L"Quality", s.quality, p))));
    s.mono           = GetPrivateProfileIntW(section, L"Mono", s.mono, p) != 0;
    s.musicMode      = GetPrivateProfileIntW(section, L"MusicMode", s.musicMode, p) != 0;
    s.optimizeForOpus = GetPrivateProfileIntW(section, L"OptimizeForOpus", s.optimizeForOpus, p) != 0;
    s.opusPreview     = GetPrivateProfileIntW(section, L"OpusPreview", s.opusPreview, p) != 0;
    s.gateDb         = std::min(-25, std::max(-70, static_cast<int>(GetPrivateProfileIntW(section, L"GateDb", s.gateDb, p))));
    wchar_t exe[260] = {};
    GetPrivateProfileStringW(section, L"TargetExe", L"", exe, 260, p);
    s.targetExe = exe;

    int bands = GetPrivateProfileIntW(section, L"EqBands", s.eqBands, p);
    s.eqBands = ValidBands(bands) ? bands : 31;
    s.eqGains = ParseGains(gainsText(p, section, L"EqGains"), s.eqBands);
    wchar_t sel[128] = {};
    GetPrivateProfileStringW(section, L"EqPreset", L"", sel, 128, p);
    s.eqPresetName = sel;

    wchar_t hidden[2048] = {};
    GetPrivateProfileStringW(section, L"HiddenApps", L"", hidden, 2048, p);
    for (wchar_t* ctx = nullptr, *tok = wcstok(hidden, L"|", &ctx); tok; tok = wcstok(nullptr, L"|", &ctx))
        if (*tok) s.hiddenApps.push_back(tok);

    // Eigene Presets: [Presets] Count, Name1/Bands1/Gains1, ...
    const int count = std::min<int>(presets::kMaxUser, std::max<int>(0, GetPrivateProfileIntW(L"Presets", L"Count", 0, p)));
    for (int i = 1; i <= count; ++i) {
        wchar_t key[32], name[128] = {};
        swprintf(key, 32, L"Name%d", i);
        GetPrivateProfileStringW(L"Presets", key, L"", name, 128, p);
        swprintf(key, 32, L"Bands%d", i);
        const int bands = GetPrivateProfileIntW(L"Presets", key, 31, p);
        if (!ValidBands(bands)) continue;
        swprintf(key, 32, L"Gains%d", i);
        s.userPresets.Save(name, bands, ParseGains(gainsText(p, L"Presets", key), bands));
    }

    // Profile: [Profiles] Count, Name1/Exe1/MusicVol1/..., plus das gewaehlte in [VoiceTune] Profile
    {
        wchar_t cur[128] = {};
        GetPrivateProfileStringW(section, L"Profile", L"", cur, 128, p);
        s.profileName = cur;
        const int n = std::min<int>(static_cast<int>(profiles::kMaxProfiles), std::max<int>(0, GetPrivateProfileIntW(L"Profiles", L"Count", 0, p)));
        for (int i = 1; i <= n; ++i) {
            auto key = [i](const wchar_t* base) { return std::wstring(base) + std::to_wstring(i); };
            auto str = [&](const wchar_t* base) {
                wchar_t buf[260] = {};
                GetPrivateProfileStringW(L"Profiles", key(base).c_str(), L"", buf, 260, p);
                return std::wstring(buf);
            };
            auto num = [&](const wchar_t* base, int def) { return static_cast<int>(GetPrivateProfileIntW(L"Profiles", key(base).c_str(), def, p)); };
            Profile pr;
            pr.targetExe      = str(L"Exe");
            pr.musicVolPct    = Clamp200(num(L"MusicVol", 100));
            pr.micVolPct      = Clamp200(num(L"MicVol", 100));
            pr.monitor        = num(L"Monitor", 0) != 0;
            pr.monitorDelayMs = std::min(2000, std::max(0, num(L"Delay", 250)));
            pr.eqEnabled      = num(L"EqOn", 1) != 0;
            pr.eqBands        = ValidBands(num(L"Bands", 31)) ? num(L"Bands", 31) : 31;
            pr.eqGains        = ParseGains(str(L"Gains"), pr.eqBands);
            pr.eqPresetName   = str(L"EqPreset");
            pr.quality        = std::min(2, std::max(0, num(L"Quality", 1)));
            pr.mono           = num(L"Mono", 0) != 0;
            pr.optimizeForOpus = num(L"Optimize", 1) != 0;
            pr.opusPreview    = num(L"Opus", 1) != 0;
            pr.soundRows      = std::min(kSoundMaxRows, std::max(1, num(L"SndRows", 2)));
            pr.soundCols      = std::min(kSoundMaxCols, std::max(1, num(L"SndCols", 4)));
            pr.soundVolPct    = Clamp200(num(L"SndVol", 100));
            pr.voicePreset    = str(L"Voice");
            pr.autoServerUid  = str(L"AutoUid");
            pr.autoServerName = str(L"AutoName");
            pr.autoChannel    = str(L"AutoChannel");
            s.profiles.Save(str(L"Name"), pr);
        }
    }

    // Soundboard
    s.soundVolPct = Clamp200(GetPrivateProfileIntW(section, L"SoundVolume", s.soundVolPct, p));
    s.soundCols     = std::min(kSoundMaxCols, std::max(1, static_cast<int>(GetPrivateProfileIntW(section, L"SoundCols", s.soundCols, p))));
    s.soundRows     = std::min(kSoundMaxRows, std::max(1, static_cast<int>(GetPrivateProfileIntW(section, L"SoundRows", s.soundRows, p))));
    s.soundShowKeys = GetPrivateProfileIntW(section, L"SoundShowKeys", s.soundShowKeys, p) != 0;
    s.soundPinned   = GetPrivateProfileIntW(section, L"SoundPinned", s.soundPinned, p) != 0;
    s.soundMonitor  = GetPrivateProfileIntW(section, L"SoundMonitor", s.soundMonitor, p) != 0;
    // Datei-Player: Selbst hoeren, letzte Liste ([Playlist] Paths = Pfade mit '|'), benannte Listen ([Playlists] Count, Name<n>, Paths<n>)
    s.fileMonitor = GetPrivateProfileIntW(section, L"FileMonitor", s.fileMonitor, p) != 0;
    {
        std::vector<wchar_t> big(kMaxIniLine);
        GetPrivateProfileStringW(L"Playlist", L"Paths", L"", big.data(), static_cast<DWORD>(big.size()), p);
        s.lastPlaylist = playlists::Split(big.data());
        s.lastRepeat   = std::min(2, std::max(0, static_cast<int>(GetPrivateProfileIntW(L"Playlist", L"Repeat", 0, p))));
        wchar_t cur[128] = {};
        GetPrivateProfileStringW(L"Playlist", L"Name", L"", cur, 128, p);
        s.playlistName = cur;
        const int n = std::min<int>(static_cast<int>(playlists::kMaxLists), std::max<int>(0, GetPrivateProfileIntW(L"Playlists", L"Count", 0, p)));
        for (int i = 1; i <= n; ++i) {
            wchar_t key[32], name[128] = {};
            swprintf(key, 32, L"Name%d", i);
            GetPrivateProfileStringW(L"Playlists", key, L"", name, 128, p);
            swprintf(key, 32, L"Paths%d", i);
            GetPrivateProfileStringW(L"Playlists", key, L"", big.data(), static_cast<DWORD>(big.size()), p);
            s.playlists.Save(name, playlists::Split(big.data()));
        }
    }
    {
        // Ein Schluessel je Knopf: Slot<n>=RRGGBB|Lautstaerke|Pfad|Name (Pfade enthalten kein '|', der Name steht zuletzt). SoundCount = hoechster belegter
        // Knopf, damit nicht alle 400 Schluessel abgefragt werden. Aeltere Versionen: SoundPath<n>/SoundName<n> (n <= 8).
        const int count = std::min(kSoundSlots, std::max(0, static_cast<int>(GetPrivateProfileIntW(L"Sounds", L"SoundCount", 0, p))));
        wchar_t   key[32];
        std::vector<wchar_t> buf(2048);
        for (int i = 0; i < count; ++i) {
            swprintf(key, 32, L"Slot%d", i + 1);
            GetPrivateProfileStringW(L"Sounds", key, L"", buf.data(), static_cast<DWORD>(buf.size()), p);
            const std::wstring v = buf.data();
            const size_t       a = v.find(L'|'), b = a == std::wstring::npos ? a : v.find(L'|', a + 1), c = b == std::wstring::npos ? b : v.find(L'|', b + 1);
            if (c == std::wstring::npos) continue;
            SoundSlot& d = s.sounds[i];
            d.color      = static_cast<uint32_t>(wcstoul(v.substr(0, a).c_str(), nullptr, 16)) & 0xFFFFFF;
            d.volPct     = Clamp200(static_cast<int>(wcstol(v.substr(a + 1, b - a - 1).c_str(), nullptr, 10)));
            d.path       = v.substr(b + 1, c - b - 1);
            d.name       = v.substr(c + 1);
        }
        for (int i = 0; i < 8 && i < kSoundSlots; ++i) {
            if (!s.sounds[i].path.empty()) continue;
            swprintf(key, 32, L"SoundPath%d", i + 1);
            GetPrivateProfileStringW(L"Sounds", key, L"", buf.data(), static_cast<DWORD>(buf.size()), p);
            s.sounds[i].path = buf.data();
            swprintf(key, 32, L"SoundName%d", i + 1);
            GetPrivateProfileStringW(L"Sounds", key, L"", buf.data(), static_cast<DWORD>(buf.size()), p);
            s.sounds[i].name = buf.data();
        }
    }

    s.nowPlayingChat    = GetPrivateProfileIntW(section, L"NowPlayingChat", s.nowPlayingChat, p) != 0;
    s.nowPlayingChannel = GetPrivateProfileIntW(section, L"NowPlayingChannel", s.nowPlayingChannel, p) != 0;
    s.nowPlayingMeta    = GetPrivateProfileIntW(section, L"NowPlayingMeta", s.nowPlayingMeta, p) != 0;
    s.language          = std::min(2, std::max(0, static_cast<int>(GetPrivateProfileIntW(section, L"Language", s.language, p))));
    s.advancedOpen      = GetPrivateProfileIntW(section, L"AdvancedOpen", s.advancedOpen, p) != 0;

    {   // Fenster, Update, Ducking, Angleichen, Automatik
        auto geti = [&](const wchar_t* sec, const wchar_t* key, int def) { return static_cast<int>(GetPrivateProfileIntW(sec, key, def, p)); };
        auto pos  = [&](const wchar_t* key) { // gespeichert als Text, damit kNoPos (INT_MIN) und negative Werte (zweiter Monitor links) stimmen
            wchar_t buf[32] = {};
            GetPrivateProfileStringW(L"Window", key, L"", buf, 32, p);
            if (!buf[0]) return static_cast<int>(Settings::kNoPos);
            return static_cast<int>(std::min<long>(30000, std::max<long>(-30000, wcstol(buf, nullptr, 10))));
        };
        s.winX = pos(L"MainX");  s.winY = pos(L"MainY");
        s.sbX  = pos(L"SoundX"); s.sbY  = pos(L"SoundY");
        s.sbW  = std::max(0, std::min(20000, geti(L"Window", L"SoundW", 0)));
        s.sbH  = std::max(0, std::min(20000, geti(L"Window", L"SoundH", 0)));
        s.miniX = pos(L"MiniX"); s.miniY = pos(L"MiniY");

        s.updateCheck     = geti(L"Update", L"Check", 1) != 0;
        s.updateCheckedAt = static_cast<long long>(GetPrivateProfileIntW(L"Update", L"CheckedAt", 0, p));
        {
            wchar_t buf[64] = {};
            GetPrivateProfileStringW(L"Update", L"Latest", L"", buf, 64, p);
            s.updateLatest = buf;
        }
        s.firstRunDone = geti(L"Update", L"FirstRunDone", 0) != 0;

        s.duckSoundOn = geti(L"Ducking", L"SoundOn", 1) != 0;
        s.duckSoundDb = std::min(30, std::max(0, geti(L"Ducking", L"SoundDb", 12)));
        s.duckMicOn   = geti(L"Ducking", L"MicOn", 0) != 0;
        s.duckMicDb   = std::min(30, std::max(0, geti(L"Ducking", L"MicDb", 10)));
        s.duckMicThresholdDb = std::min(-20, std::max(-70, geti(L"Ducking", L"MicThresholdDb", -45)));

        s.normSounds   = geti(L"Loudness", L"Sounds", 0) != 0;
        s.normTracks   = geti(L"Loudness", L"Tracks", 0) != 0;
        s.normTargetDb = std::min(-6, std::max(-40, geti(L"Loudness", L"TargetDb", -18)));
        {   // [TrackGain]: Gain<n>=<dB*10>|<pfad>
            const int n = std::min(2000, std::max(0, geti(L"TrackGain", L"Count", 0)));
            std::vector<wchar_t> buf(2048);
            for (int i = 1; i <= n; ++i) {
                wchar_t key[32];
                swprintf(key, 32, L"Gain%d", i);
                GetPrivateProfileStringW(L"TrackGain", key, L"", buf.data(), static_cast<DWORD>(buf.size()), p);
                const std::wstring v = buf.data();
                const size_t       bar = v.find(L'|');
                if (bar == std::wstring::npos || bar + 1 >= v.size()) continue;
                s.trackGain.emplace_back(v.substr(bar + 1), std::min(0.0f, std::max(-120.0f, static_cast<float>(wcstol(v.substr(0, bar).c_str(), nullptr, 10)) / 10.0f)));
            }
        }
        s.autoProfile = geti(L"Automation", L"AutoProfile", 1) != 0;
        s.trayIcon    = geti(L"Automation", L"TrayIcon", 0) != 0;
    }
    return s;
}

void SaveSettings(const std::wstring& iniPath, const Settings& s)
{
    EnsureUnicodeIni(iniPath);
    const wchar_t* p = iniPath.c_str();
    WritePrivateProfileStringW(kSection, L"Enabled", s.enabled ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"MusicVolume", std::to_wstring(s.musicVolPct).c_str(), p);
    WritePrivateProfileStringW(kSection, L"MicVolume", std::to_wstring(s.micVolPct).c_str(), p);
    WritePrivateProfileStringW(kSection, L"AlwaysSend", s.alwaysSend ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"Monitor", s.monitor ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"MonitorDelayMs", std::to_wstring(s.monitorDelayMs).c_str(), p);
    WritePrivateProfileStringW(kSection, L"TargetExe", s.targetExe.c_str(), p);
    WritePrivateProfileStringW(kSection, L"EqEnabled", s.eqEnabled ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"EqBands", std::to_wstring(s.eqBands).c_str(), p);
    WritePrivateProfileStringW(kSection, L"Quality", std::to_wstring(s.quality).c_str(), p);
    WritePrivateProfileStringW(kSection, L"Mono", s.mono ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"MusicMode", s.musicMode ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"OptimizeForOpus", s.optimizeForOpus ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"OpusPreview", s.opusPreview ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"GateDb", std::to_wstring(s.gateDb).c_str(), p);

    WritePrivateProfileStringW(kSection, L"EqGains", FormatGains(s.eqGains).c_str(), p);
    WritePrivateProfileStringW(kSection, L"EqPreset", s.eqPresetName.c_str(), p);
    {
        std::wstring joined;
        for (const auto& a : s.hiddenApps) joined += (joined.empty() ? L"" : L"|") + a;
        WritePrivateProfileStringW(kSection, L"HiddenApps", joined.c_str(), p);
    }

    WritePrivateProfileStringW(L"Presets", nullptr, nullptr, p); // Abschnitt leeren, damit geloeschte Presets verschwinden
    WritePrivateProfileStringW(L"Presets", L"Count", std::to_wstring(s.userPresets.items.size()).c_str(), p);
    for (size_t i = 0; i < s.userPresets.items.size(); ++i) {
        const auto& pr = s.userPresets.items[i];
        wchar_t     key[32];
        swprintf(key, 32, L"Name%zu", i + 1);
        WritePrivateProfileStringW(L"Presets", key, pr.name.c_str(), p);
        swprintf(key, 32, L"Bands%zu", i + 1);
        WritePrivateProfileStringW(L"Presets", key, std::to_wstring(pr.bands).c_str(), p);
        swprintf(key, 32, L"Gains%zu", i + 1);
        WritePrivateProfileStringW(L"Presets", key, FormatGains(pr.gains).c_str(), p);
    }

    WritePrivateProfileStringW(kSection, L"Profile", s.profileName.c_str(), p);
    WritePrivateProfileStringW(L"Profiles", nullptr, nullptr, p);
    WritePrivateProfileStringW(L"Profiles", L"Count", std::to_wstring(s.profiles.items.size()).c_str(), p);
    for (size_t i = 0; i < s.profiles.items.size(); ++i) {
        const Profile& pr  = s.profiles.items[i];
        auto           put = [&](const wchar_t* base, const std::wstring& v) { WritePrivateProfileStringW(L"Profiles", (std::wstring(base) + std::to_wstring(i + 1)).c_str(), v.c_str(), p); };
        put(L"Name", pr.name);
        put(L"Exe", pr.targetExe);
        put(L"MusicVol", std::to_wstring(pr.musicVolPct));
        put(L"MicVol", std::to_wstring(pr.micVolPct));
        put(L"Monitor", pr.monitor ? L"1" : L"0");
        put(L"Delay", std::to_wstring(pr.monitorDelayMs));
        put(L"EqOn", pr.eqEnabled ? L"1" : L"0");
        put(L"Bands", std::to_wstring(pr.eqBands));
        put(L"Gains", FormatGains(pr.eqGains));
        put(L"EqPreset", pr.eqPresetName);
        put(L"Quality", std::to_wstring(pr.quality));
        put(L"Mono", pr.mono ? L"1" : L"0");
        put(L"Optimize", pr.optimizeForOpus ? L"1" : L"0");
        put(L"Opus", pr.opusPreview ? L"1" : L"0");
        put(L"SndRows", std::to_wstring(pr.soundRows));
        put(L"SndCols", std::to_wstring(pr.soundCols));
        put(L"SndVol", std::to_wstring(pr.soundVolPct));
        if (!pr.voicePreset.empty()) put(L"Voice", pr.voicePreset);
        if (!pr.autoServerUid.empty()) {
            put(L"AutoUid", pr.autoServerUid);
            put(L"AutoName", pr.autoServerName);
            if (!pr.autoChannel.empty()) put(L"AutoChannel", pr.autoChannel);
        }
    }

    WritePrivateProfileStringW(kSection, L"SoundVolume", std::to_wstring(s.soundVolPct).c_str(), p);
    WritePrivateProfileStringW(kSection, L"SoundRows", std::to_wstring(s.soundRows).c_str(), p);
    WritePrivateProfileStringW(kSection, L"SoundCols", std::to_wstring(s.soundCols).c_str(), p);
    WritePrivateProfileStringW(kSection, L"SoundShowKeys", s.soundShowKeys ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"SoundPinned", s.soundPinned ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"SoundMonitor", s.soundMonitor ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"FileMonitor", s.fileMonitor ? L"1" : L"0", p);
    WritePrivateProfileStringW(L"Playlist", L"Paths", JoinCapped(s.lastPlaylist).c_str(), p);
    WritePrivateProfileStringW(L"Playlist", L"Repeat", std::to_wstring(s.lastRepeat).c_str(), p);
    WritePrivateProfileStringW(L"Playlist", L"Name", s.playlistName.c_str(), p);
    WritePrivateProfileStringW(L"Playlists", nullptr, nullptr, p); // Abschnitt leeren, damit geloeschte Listen verschwinden
    WritePrivateProfileStringW(L"Playlists", L"Count", std::to_wstring(s.playlists.items.size()).c_str(), p);
    for (size_t i = 0; i < s.playlists.items.size(); ++i) {
        wchar_t key[32];
        swprintf(key, 32, L"Name%zu", i + 1);
        WritePrivateProfileStringW(L"Playlists", key, s.playlists.items[i].name.c_str(), p);
        swprintf(key, 32, L"Paths%zu", i + 1);
        WritePrivateProfileStringW(L"Playlists", key, JoinCapped(s.playlists.items[i].paths).c_str(), p);
    }
    {
        WritePrivateProfileStringW(L"Sounds", nullptr, nullptr, p);
        int last = 0;
        for (int i = 0; i < kSoundSlots; ++i) {
            const SoundSlot& b = s.sounds[i];
            if (b.path.empty()) continue;
            last = i + 1;
            wchar_t key[32], head[32];
            swprintf(key, 32, L"Slot%d", i + 1);
            swprintf(head, 32, L"%06X|%d|", static_cast<unsigned>(b.color & 0xFFFFFF), b.volPct);
            WritePrivateProfileStringW(L"Sounds", key, (std::wstring(head) + b.path + L"|" + b.name).c_str(), p);
        }
        if (last) WritePrivateProfileStringW(L"Sounds", L"SoundCount", std::to_wstring(last).c_str(), p);
    }

    WritePrivateProfileStringW(kSection, L"NowPlayingChat", s.nowPlayingChat ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"NowPlayingChannel", s.nowPlayingChannel ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"NowPlayingMeta", s.nowPlayingMeta ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"Language", std::to_wstring(s.language).c_str(), p);
    WritePrivateProfileStringW(kSection, L"AdvancedOpen", s.advancedOpen ? L"1" : L"0", p);

    {
        auto puti = [&](const wchar_t* sec, const wchar_t* key, long long v) { WritePrivateProfileStringW(sec, key, std::to_wstring(v).c_str(), p); };
        auto pos  = [&](const wchar_t* key, int v) { WritePrivateProfileStringW(L"Window", key, v == Settings::kNoPos ? nullptr : std::to_wstring(v).c_str(), p); };
        pos(L"MainX", s.winX);   pos(L"MainY", s.winY);
        pos(L"SoundX", s.sbX);   pos(L"SoundY", s.sbY);
        puti(L"Window", L"SoundW", s.sbW);
        puti(L"Window", L"SoundH", s.sbH);
        pos(L"MiniX", s.miniX);  pos(L"MiniY", s.miniY);

        puti(L"Update", L"Check", s.updateCheck ? 1 : 0);
        puti(L"Update", L"CheckedAt", s.updateCheckedAt);
        WritePrivateProfileStringW(L"Update", L"Latest", s.updateLatest.c_str(), p);
        puti(L"Update", L"FirstRunDone", s.firstRunDone ? 1 : 0);

        puti(L"Ducking", L"SoundOn", s.duckSoundOn ? 1 : 0);
        puti(L"Ducking", L"SoundDb", s.duckSoundDb);
        puti(L"Ducking", L"MicOn", s.duckMicOn ? 1 : 0);
        puti(L"Ducking", L"MicDb", s.duckMicDb);
        puti(L"Ducking", L"MicThresholdDb", s.duckMicThresholdDb);

        puti(L"Loudness", L"Sounds", s.normSounds ? 1 : 0);
        puti(L"Loudness", L"Tracks", s.normTracks ? 1 : 0);
        puti(L"Loudness", L"TargetDb", s.normTargetDb);
        WritePrivateProfileStringW(L"TrackGain", nullptr, nullptr, p);
        if (!s.trackGain.empty()) {
            puti(L"TrackGain", L"Count", static_cast<long long>(s.trackGain.size()));
            for (size_t i = 0; i < s.trackGain.size(); ++i) {
                wchar_t key[32];
                swprintf(key, 32, L"Gain%zu", i + 1);
                WritePrivateProfileStringW(L"TrackGain", key, (std::to_wstring(static_cast<long>(std::lround(s.trackGain[i].second * 10.0f))) + L"|" + s.trackGain[i].first).c_str(), p);
            }
        }
        puti(L"Automation", L"AutoProfile", s.autoProfile ? 1 : 0);
        puti(L"Automation", L"TrayIcon", s.trayIcon ? 1 : 0);
    }
}
