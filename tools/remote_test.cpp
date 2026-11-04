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

// Selbsttest der Stream-Deck-Schnittstelle ueber echtes HTTP: feste Knopf-IDs in /sounds und /state, soundid-Befehl, Vergabe der IDs fuer eine aeltere INI.
// Laeuft auf einem eigenen Port, stoert also ein laufendes TeamSpeak nicht.
#include <winsock2.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "engine.h"
#include "remote.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

static constexpr unsigned short kPort = 19471;
static bool g_unavailable = false;

struct Resp {
    int         status = 0;
    std::string body;
};

static Resp Http(const std::string& method, const std::string& path, bool header = true)
{
    Resp   r;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return r;
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(kPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        closesocket(s);
        return r;
    }
    const std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" + (header ? "X-VoiceTune: 1\r\n" : "") + "\r\n";
    send(s, req.data(), static_cast<int>(req.size()), 0);
    std::string all;
    char        buf[4096];
    for (int n; (n = recv(s, buf, sizeof buf, 0)) > 0;) all.append(buf, n);
    closesocket(s);
    const size_t sp = all.find(' '), end = all.find("\r\n\r\n");
    if (sp != std::string::npos) r.status = std::atoi(all.c_str() + sp + 1);
    if (end != std::string::npos) r.body = all.substr(end + 4);
    return r;
}

static std::string Narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }
static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Wert von "soundsRev" aus /state
static std::string Rev()
{
    const std::string b   = Http("GET", "/state").body;
    const std::string key = "\"soundsRev\":";
    const size_t      p   = b.find(key);
    if (p == std::string::npos) return "";
    const size_t e = b.find(',', p);
    return b.substr(p + key.size(), e - p - key.size());
}

static void WriteTone(const std::wstring& path)
{
    const int          rate = 48000, frames = rate * 3;
    std::vector<short> pcm(frames);
    for (int i = 0; i < frames; ++i) pcm[i] = static_cast<short>(8000 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / rate));
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(short)), riff = 36 + dataBytes, fmtLen = 16, byteRate = rate * 2, sr = rate;
    const uint16_t pcmFmt = 1, channels = 1, align = 2, bits = 16;
    std::ofstream  f(path.c_str(), std::ios::binary);
    f.write("RIFF", 4); f.write(reinterpret_cast<const char*>(&riff), 4); f.write("WAVEfmt ", 8);
    f.write(reinterpret_cast<const char*>(&fmtLen), 4); f.write(reinterpret_cast<const char*>(&pcmFmt), 2); f.write(reinterpret_cast<const char*>(&channels), 2);
    f.write(reinterpret_cast<const char*>(&sr), 4); f.write(reinterpret_cast<const char*>(&byteRate), 4); f.write(reinterpret_cast<const char*>(&align), 2); f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4); f.write(reinterpret_cast<const char*>(&dataBytes), 4); f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
}

int main()
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring cfg = std::wstring(tmp) + L"aam_remote_test", ini = cfg + L"\\voicetune.ini", wav = cfg + L"\\tone.wav";
    CreateDirectoryW(cfg.c_str(), nullptr);
    DeleteFileW(ini.c_str());
    WriteTone(wav);

    // INI einer aelteren Version: Knoepfe ohne ID
    WritePrivateProfileStringW(L"Sounds", L"SoundCount", L"2", ini.c_str());
    WritePrivateProfileStringW(L"Sounds", L"Slot1", (L"C0392B|100|" + wav + L"|Alt").c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Sounds", L"Slot2", (L"000000|100|" + wav + L"|Zwei").c_str(), ini.c_str());

    Engine& en = Engine::Get();
    en.SetLogger([](const char*) {});
    en.Init(cfg);

    const std::wstring idA = en.GetSound(0).id, idB = en.GetSound(1).id;
    CHECK(IsSoundId(idA) && IsSoundId(idB) && idA != idB, "aeltere INI bekommt IDs: %ls, %ls", idA.c_str(), idB.c_str());
    wchar_t stored[32] = L"";
    GetPrivateProfileStringW(L"Sounds", L"SlotId1", L"", stored, 32, ini.c_str());
    CHECK(idA == stored, "IDs sind gleich gespeichert: '%ls'", stored);
    wchar_t slot1[1024] = L"";
    GetPrivateProfileStringW(L"Sounds", L"Slot1", L"", slot1, 1024, ini.c_str());
    CHECK(std::wstring(slot1) == L"C0392B|100|" + wav + L"|Alt", "Slot-Zeile bleibt im alten Format: '%ls'", slot1);

    RemoteSetUnavailableCallback([](unsigned short) { g_unavailable = true; });
    RemoteStart(kPort);
    if (g_unavailable) {
        std::printf("Port %u ist belegt, Test nicht moeglich\n", kPort);
        en.Shutdown();
        return 1;
    }

    const std::string a = Narrow(idA), b = Narrow(idB);

    // /sounds
    {
        const Resp r = Http("GET", "/sounds");
        CHECK(r.status == 200, "Status %d", r.status);
        CHECK(Has(r.body, "{\"slot\":1,\"id\":\"" + a + "\",\"used\":true,\"name\":\"Alt\",\"color\":\"C0392B\",\"ready\":true}"), "Knopf 1:\n%s", r.body.c_str());
        CHECK(Has(r.body, "{\"slot\":2,\"id\":\"" + b + "\",\"used\":true,\"name\":\"Zwei\",\"color\":\"\",\"ready\":true}"), "Knopf 2 (Standardfarbe):\n%s", r.body.c_str());
        CHECK(Has(r.body, "{\"slot\":3,\"id\":\"\",\"used\":false,\"name\":\"\",\"color\":\"\",\"ready\":false}"), "nie belegter Knopf:\n%s", r.body.c_str());
        CHECK(Has(r.body, "\"slot\":8,") && !Has(r.body, "\"slot\":9,") && Has(r.body, "\"rows\":2,\"cols\":4"), "mindestens 8 Eintraege, Raster:\n%s", r.body.c_str());
    }

    // soundid: Header, unbekannte ID, Antwort
    {
        CHECK(Http("POST", "/cmd/soundid/" + a, false).status == 403, "ohne Header");
        const Resp none = Http("POST", "/cmd/soundid/ffffffff");
        CHECK(none.status == 404 && Has(none.body, "unknown command"), "unbekannte ID: %d %s", none.status, none.body.c_str());
        CHECK(Http("POST", "/cmd/soundid").status == 404, "ohne ID");
    }

    // Abspielen ueber die ID: /state meldet Platz und ID
    {
        for (int i = 0; i < 400 && en.GetSound(0).state != Soundboard::State::Ready; ++i) Sleep(20);
        CHECK(en.GetSound(0).state == Soundboard::State::Ready, "Clip geladen");
        const Resp r = Http("POST", "/cmd/soundid/" + a);
        CHECK(r.status == 200 && Has(r.body, "\"soundsRev\":"), "soundid: %d %s", r.status, r.body.c_str());
        for (int i = 0; i < 15; ++i) {
            short mic[960] = {};
            int   ed       = 0;
            en.Mix(1, mic, 960, 1, &ed);
            Sleep(20);
        }
        const std::string s = Http("GET", "/state").body;
        CHECK(Has(s, "\"soundsActive\":[1]") && Has(s, "\"soundsActiveIds\":[\"" + a + "\"]"), "laufender Knopf:\n%s", s.c_str());
        CHECK(Http("POST", "/cmd/soundstop").status == 200, "soundstop");
        for (int i = 0; i < 15; ++i) {
            short mic[960] = {};
            int   ed       = 0;
            en.Mix(1, mic, 960, 1, &ed);
            Sleep(20);
        }
        const std::string q = Http("GET", "/state").body;
        CHECK(Has(q, "\"soundsActive\":[]") && Has(q, "\"soundsActiveIds\":[]"), "nichts laeuft mehr:\n%s", q.c_str());
    }

    // soundsRev: stabil ohne Aenderung, neu bei jeder Aenderung an der Liste
    {
        const std::string r0 = Rev();
        CHECK(!r0.empty() && r0 == Rev(), "stabil: %s", r0.c_str());
        en.SoundRename(0, L"Neu");
        const std::string r1 = Rev();
        CHECK(r1 != r0, "Umbenennen");
        en.SoundSwap(0, 5);
        const std::string r2 = Rev();
        CHECK(r2 != r1, "Verschieben");
        const std::string sounds = Http("GET", "/sounds").body;
        CHECK(Has(sounds, "{\"slot\":6,\"id\":\"" + a + "\",\"used\":true,\"name\":\"Neu\"") && Has(sounds, "{\"slot\":1,\"id\":\"\",\"used\":false"), "ID wandert mit:\n%s", sounds.c_str());
        CHECK(Http("POST", "/cmd/soundid/" + a).status == 200, "soundid nach dem Verschieben");
        en.SetSoundGrid(3, 4);
        CHECK(Rev() != r2, "anderes Raster");
    }

    // Geleerter Knopf hinter dem letzten belegten bleibt in der Liste
    {
        en.SoundSetSlot(19, wav);
        const std::string late = Narrow(en.GetSound(19).id);
        CHECK(IsSoundId(en.GetSound(19).id), "ID fuer Knopf 20");
        en.SoundSetSlot(19, L"");
        const std::string sounds = Http("GET", "/sounds").body;
        CHECK(Has(sounds, "{\"slot\":20,\"id\":\"" + late + "\",\"used\":false,\"name\":\"\""), "geleert, aber auffindbar:\n%s", sounds.c_str());
        CHECK(Http("POST", "/cmd/soundid/" + late).status == 200, "geleerter Knopf: kein 404");
    }

    RemoteStop();
    en.Shutdown();
    DeleteFileW(ini.c_str());
    DeleteFileW(wav.c_str());
    RemoveDirectoryW(cfg.c_str());
    std::printf(g_fail ? "%d FEHLER\n" : "remote_test: alle Tests ok\n", g_fail);
    return g_fail ? 1 : 0;
}
