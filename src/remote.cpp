#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include "engine.h"
#include "remote.h"

namespace {
SOCKET            g_listen = INVALID_SOCKET;
std::thread       g_thread;
std::atomic<bool> g_quit{false};

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

std::string StateJson() {
    Engine&        e = Engine::Get();
    Settings       s = e.GetSettings();
    PlayerSnapshot p = e.GetPlayer();
    return std::string("{\"enabled\":") + (s.enabled ? "true" : "false") +
           ",\"micMuted\":" + (e.IsMicMuted() ? "true" : "false") +
           ",\"musicVol\":" + std::to_string(s.musicVolPct) +
           ",\"micVol\":" + std::to_string(s.micVolPct) +
           ",\"playing\":" + ((p.active && !p.paused) ? "true" : "false") +
           ",\"source\":\"" + JsonEsc(WideToUtf8(e.SourceLabel())) + "\"}";
}

bool RunCommand(const std::string& c) {
    Engine&  e = Engine::Get();
    Settings s = e.GetSettings();
    if (c == "toggle")        e.SetEnabled(!s.enabled);
    else if (c == "on")       e.SetEnabled(true);
    else if (c == "off")      e.SetEnabled(false);
    else if (c == "micmute")  e.SetMicMuted(!e.IsMicMuted());
    else if (c == "musicvol_up")   e.SetMusicVolume(std::min(200, s.musicVolPct + 10));
    else if (c == "musicvol_down") e.SetMusicVolume(std::max(0, s.musicVolPct - 10));
    else if (c == "next")     e.PlayerNext();
    else if (c == "prev")     e.PlayerPrev();
    else if (c == "stop")     e.PlayerStop();
    else if (c == "playpause") {
        if (e.GetPlayer().active) e.PlayerTogglePause(); else e.PlayerPlay(-1);
    }
    else return false;
    return true;
}

std::string Handle(const std::string& req) {
    const size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return Reply(400, "Bad Request", "{}");
    const std::string method = req.substr(0, sp1), path = req.substr(sp1 + 1, sp2 - sp1 - 1);

    if (method == "GET" && path == "/state") return Reply(200, "OK", StateJson());

    if (method == "POST" && path.rfind("/cmd/", 0) == 0) {
        // Schutz gegen fremde Webseiten: ein eigener Header loest im Browser einen Preflight aus, den wir nie beantworten
        std::string low = req;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char ch) { return (char)tolower(ch); });
        if (low.find("\r\nx-voicetune:") == std::string::npos) return Reply(403, "Forbidden", "{}");
        if (!RunCommand(path.substr(5))) return Reply(404, "Not Found", "{\"error\":\"unknown command\"}");
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
} // namespace

void RemoteStart(unsigned short port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) return;
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // nur localhost, nie von aussen erreichbar
    if (bind(g_listen, (sockaddr*)&a, sizeof a) != 0 || listen(g_listen, 4) != 0) {
        closesocket(g_listen); g_listen = INVALID_SOCKET; return;
    }
    g_quit = false;
    g_thread = std::thread(Serve);
}

void RemoteStop() {
    g_quit = true;
    if (g_thread.joinable()) g_thread.join();
    if (g_listen != INVALID_SOCKET) { closesocket(g_listen); g_listen = INVALID_SOCKET; }
    WSACleanup();
}