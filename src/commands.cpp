#include "commands.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>

#include "app_discovery.h"
#include "engine.h"

namespace {

void (*g_notify)(const char*, bool) = nullptr;

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// Ganze Zahl in [lo, hi]; sonst false
bool ParseInt(const std::string& s, int lo, int hi, int* out)
{
    if (s.empty() || s.size() > 6) return false;
    size_t i = 0;
    if (s[0] == '-') i = 1;
    if (i >= s.size()) return false;
    for (size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    const int v = std::atoi(s.c_str());
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

} // namespace

std::string UrlDecode(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            out += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

void SetCommandNotifier(void (*fn)(const char*, bool)) { g_notify = fn; }

bool RunCommand(const std::string& name, const std::string& arg)
{
    Engine&        e = Engine::Get();
    const Settings s = e.GetSettings();
    int            n = 0;

    if (name == "toggle") e.SetEnabled(!s.enabled);
    else if (name == "on") e.SetEnabled(true);
    else if (name == "off") e.SetEnabled(false);
    else if (name == "micmute") {
        e.SetMicMuted(!e.IsMicMuted());
        if (g_notify) g_notify("micmute", e.IsMicMuted());
    }
    else if (name == "musicvol_up") e.SetMusicVolume(std::min(200, s.musicVolPct + 10));
    else if (name == "musicvol_down") e.SetMusicVolume(std::max(0, s.musicVolPct - 10));
    else if (name == "musicvol") {
        if (!ParseInt(arg, 0, 200, &n)) return false;
        e.SetMusicVolume(n);
    }
    else if (name == "micvol") {
        if (!ParseInt(arg, 0, 200, &n)) return false;
        e.SetMicVolume(n);
    }
    else if (name == "monitor") e.SetMonitor(!s.monitor);
    else if (name == "next") e.PlayerNext();
    else if (name == "prev") e.PlayerPrev();
    else if (name == "stop") e.PlayerStop();
    else if (name == "playpause") {
        if (e.GetPlayer().active) e.PlayerTogglePause();
        else e.PlayerPlay(-1);
    }
    else if (name == "preset") {
        const std::wstring want = Utf8ToWide(arg);
        if (want.empty()) return false;
        for (int i = 0; i < eq::kPresetCount; ++i)
            if (presets::EqualNoCase(want, eq::PresetName(i))) {
                e.ApplyBuiltinPreset(i);
                return true;
            }
        if (!s.userPresets.Find(want)) return false;
        e.ApplyUserPreset(want);
    }
    else if (name == "profile") {
        if (!e.ApplyProfile(Utf8ToWide(arg))) return false;
    }
    else if (name == "profile_next") {
        if (!e.ApplyNextProfile()) return false;
    }
    else if (name == "source") {
        const std::wstring exe = ToLower(Utf8ToWide(arg));
        if (exe.empty()) return false;
        e.SetTarget(exe);
    }
    else if (name == "sound") {
        if (!ParseInt(arg, 1, kSoundSlots, &n)) return false;
        e.SoundPlay(n - 1);
    }
    else if (name == "soundstop") e.SoundStop();
    else if (name == "profilenum") {
        if (!ParseInt(arg, 1, kProfileHotkeys, &n)) return false;
        e.ApplyProfileNumber(n); // gibt es das n-te Profil nicht, passiert nichts (wie ein leerer Sound-Knopf)
    }
    else if (name == "soundmonitor") e.SetSoundMonitor(!s.soundMonitor);
    else if (name == "filemonitor") e.SetFileMonitor(!s.fileMonitor);
    else if (name == "playlist") {
        if (!e.PlaylistLoad(Utf8ToWide(arg))) return false;
    }
    else return false;
    return true;
}

bool RunCommandPath(const std::string& path)
{
    const size_t slash = path.find('/');
    const std::string name = slash == std::string::npos ? path : path.substr(0, slash);
    const std::string arg  = slash == std::string::npos ? std::string() : UrlDecode(path.substr(slash + 1));
    return RunCommand(name, arg);
}

bool RunHotkey(const std::string& keyword)
{
    if (keyword.rfind("sound", 0) == 0 && keyword.size() > 5 && keyword != "soundstop") return RunCommand("sound", keyword.substr(5));
    if (keyword.rfind("profile", 0) == 0 && keyword.size() > 7 && keyword[7] >= '0' && keyword[7] <= '9') return RunCommand("profilenum", keyword.substr(7)); // profile1..profile10
    return RunCommand(keyword);
}

const std::vector<HotkeyDef>& HotkeyList()
{
    static const std::vector<HotkeyDef> list = [] {
      std::vector<HotkeyDef> v = {
        {"toggle", "VoiceTune: Musik einspielen an/aus"},
        {"micmute", "VoiceTune: Mikrofon stumm/laut"},
        {"musicvol_up", "VoiceTune: Musik lauter"},
        {"musicvol_down", "VoiceTune: Musik leiser"},
        {"playpause", "VoiceTune: Datei-Player Play/Pause"},
        {"next", "VoiceTune: Datei-Player n\xC3\xA4" "chster Titel"},
        {"prev", "VoiceTune: Datei-Player vorheriger Titel"},
        {"monitor", "VoiceTune: Mith\xC3\xB6ren an/aus"},
        {"profile_next", "VoiceTune: n\xC3\xA4" "chstes Profil"},
        {"soundstop", "VoiceTune: Alle Sounds stoppen"},
      };
      static std::deque<std::string> text; // stabile Zeiger fuer die Sound-Knoepfe 1..kSoundHotkeys
      for (int i = 1; i <= kSoundHotkeys; ++i) {
          text.push_back("sound" + std::to_string(i));
          v.push_back({text.back().c_str(), "VoiceTune: Sound %d abspielen", i});
      }
      for (int i = 1; i <= kProfileHotkeys; ++i) {
          text.push_back("profile" + std::to_string(i));
          v.push_back({text.back().c_str(), "VoiceTune: Profil %d anwenden", i});
      }
      return v;
    }();
    return list;
}
