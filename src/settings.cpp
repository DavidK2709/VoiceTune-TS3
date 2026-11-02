#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <vector>

namespace {
const wchar_t* kSection = L"VoiceTune";

int Clamp200(int v) { return std::min(200, std::max(0, v)); }
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
            s.profiles.Save(str(L"Name"), pr);
        }
    }

    // Soundboard
    s.soundVolPct = Clamp200(GetPrivateProfileIntW(section, L"SoundVolume", s.soundVolPct, p));
    for (int i = 0; i < kSoundSlots; ++i) {
        wchar_t key[32], buf[520] = {};
        swprintf(key, 32, L"SoundPath%d", i + 1);
        GetPrivateProfileStringW(L"Sounds", key, L"", buf, 520, p);
        s.sounds[i].path = buf;
        swprintf(key, 32, L"SoundName%d", i + 1);
        GetPrivateProfileStringW(L"Sounds", key, L"", buf, 520, p);
        s.sounds[i].name = buf;
    }

    s.nowPlayingChat    = GetPrivateProfileIntW(section, L"NowPlayingChat", s.nowPlayingChat, p) != 0;
    s.nowPlayingChannel = GetPrivateProfileIntW(section, L"NowPlayingChannel", s.nowPlayingChannel, p) != 0;
    s.nowPlayingMeta    = GetPrivateProfileIntW(section, L"NowPlayingMeta", s.nowPlayingMeta, p) != 0;
    s.language          = std::min(2, std::max(0, static_cast<int>(GetPrivateProfileIntW(section, L"Language", s.language, p))));
    return s;
}

void SaveSettings(const std::wstring& iniPath, const Settings& s)
{
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
    }

    WritePrivateProfileStringW(kSection, L"SoundVolume", std::to_wstring(s.soundVolPct).c_str(), p);
    WritePrivateProfileStringW(L"Sounds", nullptr, nullptr, p);
    for (int i = 0; i < kSoundSlots; ++i) {
        if (s.sounds[i].path.empty()) continue;
        wchar_t key[32];
        swprintf(key, 32, L"SoundPath%d", i + 1);
        WritePrivateProfileStringW(L"Sounds", key, s.sounds[i].path.c_str(), p);
        swprintf(key, 32, L"SoundName%d", i + 1);
        WritePrivateProfileStringW(L"Sounds", key, s.sounds[i].name.c_str(), p);
    }

    WritePrivateProfileStringW(kSection, L"NowPlayingChat", s.nowPlayingChat ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"NowPlayingChannel", s.nowPlayingChannel ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"NowPlayingMeta", s.nowPlayingMeta ? L"1" : L"0", p);
    WritePrivateProfileStringW(kSection, L"Language", std::to_wstring(s.language).c_str(), p);
}
