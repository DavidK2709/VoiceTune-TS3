
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
 
#include "update_check.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#include "engine.h"
#include "update_logic.h"
#include "version.h"

namespace updatecheck {
namespace {

std::mutex        g_mutex;
std::thread       g_thread;
std::atomic<bool> g_running{false};

// GET https://api.github.com/repos/<repo>/releases?per_page=1 (Redirect wird gefolgt); leer bei jedem Fehler
std::string Fetch()
{
    std::string  body;
    HINTERNET    session = WinHttpOpen(L"VoiceTune-UpdateCheck", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return body;
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 8000);
    HINTERNET conn = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req  = conn ? WinHttpOpenRequest(conn, L"GET", L"/repos/DavidK2709/VoiceTune-TS3/releases?per_page=1", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE)
                           : nullptr;
    if (req) {
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
        if (WinHttpSendRequest(req, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status == 200) {
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                    std::string chunk(avail, '\0');
                    DWORD       got = 0;
                    if (!WinHttpReadData(req, &chunk[0], avail, &got) || got == 0) break;
                    body.append(chunk, 0, got);
                    if (body.size() > 200000) break; // mehr als genug fuer das erste Release
                }
            }
        }
        WinHttpCloseHandle(req);
    }
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return body;
}

} // namespace

void Start(bool force)
{
    const Settings st = Engine::Get().GetSettings();
    if (!st.updateCheck && !force) return;
    const long long now = static_cast<long long>(std::time(nullptr));
    if (!force && st.updateCheckedAt > 0 && now - st.updateCheckedAt < 12 * 3600) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_running.exchange(true)) return;
    if (g_thread.joinable()) g_thread.join();
    g_thread = std::thread([] {
        const std::wstring tag = updatelogic::FirstTag(Fetch());
        if (!tag.empty()) Engine::Get().SetUpdateInfo(tag, static_cast<long long>(std::time(nullptr)));
        g_running = false;
    });
}

void Stop()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_thread.joinable()) g_thread.join(); // hoechstens ein paar Sekunden (Timeouts)
}

bool NewerAvailable(std::wstring* latestOut)
{
    const Settings st = Engine::Get().GetSettings();
    if (!st.updateCheck || !updatelogic::IsNewer(st.updateLatest, VOICETUNE_VERSION_W)) return false;
    if (latestOut) *latestOut = st.updateLatest;
    return true;
}

} // namespace updatecheck
