#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "commands.h"
#include "engine.h"
#include "remote.h"

namespace {
SOCKET            g_listen = INVALID_SOCKET;
std::thread       g_thread;
std::atomic<bool> g_quit{false};
unsigned short    g_port = 19470;

std::string WideToUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::string JsonEsc(const std::string& in) {
    std::string o;
    for (unsigned char c : in) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) o += ' ';
        else o += (char)c;
    }
    return o;
}
std::string Reply(int code, const char* text, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(code) + " " + text +
           "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
}

std::string JsonStrArray(const std::vector<std::wstring>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) o += (i ? ",\"" : "\"") + JsonEsc(WideToUtf8(v[i])) + "\"";
    return o + "]";
}

std::string StateJson() {
    Engine&        e = Engine::Get();
    Settings       s = e.GetSettings();
    PlayerSnapshot p = e.GetPlayer();
    return std::string("{\"enabled\":") + (s.enabled ? "true" : "false") +
           ",\"micMuted\":" + (e.IsMicMuted() ? "true" : "false") +
           ",\"musicVol\":" + std::to_string(s.musicVolPct) +
           ",\"micVol\":" + std::to_string(s.micVolPct) +
           ",\"playing\":" + ((p.active && !p.paused) ? "true" : "false") +
           ",\"monitor\":" + (s.monitor ? "true" : "false") +
           ",\"soundMonitor\":" + (s.soundMonitor ? "true" : "false") +
           ",\"profile\":\"" + JsonEsc(WideToUtf8(s.profileName)) + "\"" +
           ",\"preset\":\"" + JsonEsc(WideToUtf8(s.eqPresetName)) + "\"" +
           ",\"source\":\"" + JsonEsc(WideToUtf8(e.SourceLabel())) + "\"}";
}

// Namen aller EQ-Presets (mitgelieferte zuerst, dann eigene)
std::string PresetsJson() {
    std::vector<std::wstring> names;
    for (int i = 0; i < eq::kPresetCount; ++i) names.push_back(eq::PresetName(i));
    const Settings s = Engine::Get().GetSettings();
    for (const auto& p : s.userPresets.items) names.push_back(p.name);
    return "{\"presets\":" + JsonStrArray(names) + ",\"current\":\"" + JsonEsc(WideToUtf8(s.eqPresetName)) + "\"}";
}

std::string ProfilesJson() {
    return "{\"profiles\":" + JsonStrArray(Engine::Get().ProfileNames()) + ",\"current\":\"" + JsonEsc(WideToUtf8(Engine::Get().GetSettings().profileName)) + "\"}";
}

std::string SoundsJson() {
    const auto  sounds = Engine::Get().GetSounds();
    const auto  st     = Engine::Get().GetSettings();
    size_t      count  = 8; // wie bisher mindestens die ersten 8 Knoepfe, danach bis zum letzten belegten
    for (size_t i = 0; i < sounds.size(); ++i)
        if (!sounds[i].path.empty()) count = std::max(count, i + 1);
    std::string o = "{\"sounds\":[";
    for (size_t i = 0; i < count; ++i) {
        const bool ready = sounds[i].state == Soundboard::State::Ready || sounds[i].state == Soundboard::State::Unloaded || sounds[i].state == Soundboard::State::Loading;
        o += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i + 1) + ",\"name\":\"" + JsonEsc(WideToUtf8(sounds[i].name)) + "\",\"ready\":" + (ready ? "true" : "false") + "}";
    }
    return o + "],\"rows\":" + std::to_string(st.soundRows) + ",\"cols\":" + std::to_string(st.soundCols) + "}";
}

std::string Handle(const std::string& req) {
    const size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return Reply(400, "Bad Request", "{}");
    const std::string method = req.substr(0, sp1), path = req.substr(sp1 + 1, sp2 - sp1 - 1);

    if (method == "GET" && path == "/state") return Reply(200, "OK", StateJson());
    if (method == "GET" && path == "/presets") return Reply(200, "OK", PresetsJson());
    if (method == "GET" && path == "/profiles") return Reply(200, "OK", ProfilesJson());
    if (method == "GET" && path == "/sounds") return Reply(200, "OK", SoundsJson());

    if (method == "POST" && path.rfind("/cmd/", 0) == 0) {
        // Schutz gegen fremde Webseiten: ein eigener Header loest im Browser einen Preflight aus, den wir nie beantworten
        std::string low = req;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char ch) { return (char)tolower(ch); });
        if (low.find("\r\nx-voicetune:") == std::string::npos) return Reply(403, "Forbidden", "{}");
        if (!RunCommandPath(path.substr(5))) return Reply(404, "Not Found", "{\"error\":\"unknown command\"}");
        return Reply(200, "OK", StateJson());
    }
    return Reply(404, "Not Found", "{}");
}

void Serve() {
    while (!g_quit) {
        fd_set fds; FD_ZERO(&fds); FD_SET(g_listen, &fds);
        timeval tv{0, 200000};
        if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET c = accept(g_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD to = 2000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
        char buf[2048];
        int  n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            std::string resp = Handle(buf);
            send(c, resp.data(), (int)resp.size(), 0);
        }
        shutdown(c, SD_SEND);
        closesocket(c);
    }
}

// Schnittstelle kann nicht starten (meist: Port belegt, weil schon ein zweites TeamSpeak mit VoiceTune laeuft): das Plugin zeigt dem Nutzer
// einen Hinweis im TeamSpeak-Client (Rueckruf, siehe RemoteSetUnavailableCallback).
void (*g_onUnavailable)(unsigned short) = nullptr;

void NotifyUnavailable(unsigned short port) {
    if (g_onUnavailable) g_onUnavailable(port);
}
} // namespace

void RemoteSetUnavailableCallback(void (*fn)(unsigned short port)) { g_onUnavailable = fn; }

void RemoteStart(unsigned short port) {
    g_port = port;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { NotifyUnavailable(port); return; }
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) { NotifyUnavailable(port); return; }
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // nur localhost, nie von aussen erreichbar
    if (bind(g_listen, (sockaddr*)&a, sizeof a) != 0 || listen(g_listen, 4) != 0) {
        closesocket(g_listen); g_listen = INVALID_SOCKET; NotifyUnavailable(port); return;
    }
    g_quit = false;
    g_thread = std::thread(Serve);
}

bool RemoteIsRunning() { return g_listen != INVALID_SOCKET && g_thread.joinable() && !g_quit; }
unsigned short RemotePort() { return g_port; }

void RemoteStop() {
    g_quit = true;
    if (g_thread.joinable()) g_thread.join();
    if (g_listen != INVALID_SOCKET) { closesocket(g_listen); g_listen = INVALID_SOCKET; }
    WSACleanup();
}