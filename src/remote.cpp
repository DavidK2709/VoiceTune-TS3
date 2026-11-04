
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
 
#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include "commands.h"
#include "engine.h"
#include "remote.h"
#include "update_check.h"

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

bool SoundReady(const Engine::SoundInfo& s) {
    return s.state == Soundboard::State::Ready || s.state == Soundboard::State::Unloaded || s.state == Soundboard::State::Loading;
}

// Pruefsumme (FNV-1a) ueber alles, was /sounds liefert: Aendert sie sich, muss ein Client die Liste neu holen. Berechnet statt mitgezaehlt, so entgeht ihr keine Aenderung
// (Umsortieren, Import, Profilwechsel mit anderem Raster, fehlgeschlagenes Laden).
uint32_t SoundsRev(const std::vector<Engine::SoundInfo>& all, const Settings& s) {
    uint32_t h   = 2166136261u;
    auto     add = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    add(static_cast<uint32_t>(s.soundRows));
    add(static_cast<uint32_t>(s.soundCols));
    for (const auto& b : all) {
        for (wchar_t c : b.id) add(static_cast<uint32_t>(c));
        add(0);
        for (wchar_t c : b.name) add(static_cast<uint32_t>(c));
        add(0);
        add(b.color);
        add(b.path.empty() ? 0u : 1u);
        add(SoundReady(b) ? 1u : 0u);
    }
    return h;
}

// Zusatzfelder von /state (additiv): laufende Sounds, Profil-Position, Datei-Player-Fortschritt, Ducking
std::string StateExtraJson(Engine& e, const Settings& s, const PlayerSnapshot& p) {
    std::string    sounds, soundIds;
    int            playing = 0;
    const auto     all     = e.GetSounds();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].position >= 0) {
            sounds += std::string(playing ? "," : "") + std::to_string(i + 1);
            soundIds += std::string(playing ? ",\"" : "\"") + JsonEsc(WideToUtf8(all[i].id)) + "\"";
            ++playing;
        }
    const auto names = e.ProfileNames();
    int        index = 0;
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == s.profileName) index = static_cast<int>(i) + 1;
    std::string title;
    if (p.current >= 0 && p.current < static_cast<int>(p.items.size())) title = WideToUtf8(p.items[p.current].title);
    char pos[96];
    std::snprintf(pos, sizeof(pos), "\"position\":%.1f,\"duration\":%.1f", p.position, p.duration);
    return ",\"soundPlaying\":" + std::string(playing > 0 ? "true" : "false") + ",\"soundsActive\":[" + sounds + "]" +
           ",\"soundsActiveIds\":[" + soundIds + "],\"soundsRev\":" + std::to_string(SoundsRev(all, s)) +
           ",\"profileIndex\":" + std::to_string(index) + ",\"profileCount\":" + std::to_string(names.size()) +
           ",\"file\":{\"title\":\"" + JsonEsc(title) + "\"," + pos + "}" +
           ",\"ducking\":" + (e.DuckGain() < 0.95f ? "true" : "false") +
           ",\"update\":" + (updatecheck::NewerAvailable() ? "true" : "false");
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
           ",\"fileMonitor\":" + (s.fileMonitor ? "true" : "false") +
           ",\"playlist\":\"" + JsonEsc(WideToUtf8(s.playlistName)) + "\"" +
           ",\"profile\":\"" + JsonEsc(WideToUtf8(s.profileName)) + "\"" +
           ",\"preset\":\"" + JsonEsc(WideToUtf8(s.eqPresetName)) + "\"" +
           ",\"source\":\"" + JsonEsc(WideToUtf8(e.SourceLabel())) + "\"" + StateExtraJson(e, s, p) + "}";
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

std::string PlaylistsJson() {
    return "{\"playlists\":" + JsonStrArray(Engine::Get().PlaylistNames()) + ",\"current\":\"" + JsonEsc(WideToUtf8(Engine::Get().GetSettings().playlistName)) + "\"}";
}

std::string SoundsJson() {
    const auto  sounds = Engine::Get().GetSounds();
    const auto  st     = Engine::Get().GetSettings();
    size_t      count  = 8; // wie bisher mindestens die ersten 8 Knoepfe, danach bis zum letzten mit Inhalt oder ID (auch ein geleerter Knopf muss fuer eine Taste auffindbar bleiben)
    for (size_t i = 0; i < sounds.size(); ++i)
        if (!sounds[i].path.empty() || !sounds[i].id.empty()) count = std::max(count, i + 1);
    std::string o = "{\"sounds\":[";
    for (size_t i = 0; i < count; ++i) {
        char color[8] = "";
        if (sounds[i].color) std::snprintf(color, sizeof(color), "%06X", static_cast<unsigned>(sounds[i].color & 0xFFFFFF));
        o += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i + 1) + ",\"id\":\"" + JsonEsc(WideToUtf8(sounds[i].id)) + "\",\"used\":" + (sounds[i].path.empty() ? "false" : "true") +
             ",\"name\":\"" + JsonEsc(WideToUtf8(sounds[i].name)) + "\",\"color\":\"" + color + "\",\"ready\":" + (SoundReady(sounds[i]) ? "true" : "false") + "}";
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
    if (method == "GET" && path == "/playlists") return Reply(200, "OK", PlaylistsJson());

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