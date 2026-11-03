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
 
#include "app_discovery.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <map>

namespace {

template <class T>
void SafeRelease(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

struct ProcEntry {
    DWORD        parent;
    std::wstring exe;
};

std::map<DWORD, ProcEntry> SnapshotProcesses()
{
    std::map<DWORD, ProcEntry> procs;
    HANDLE                     snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return procs;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        procs[pe.th32ProcessID] = {pe.th32ParentProcessID, ToLower(pe.szExeFile)};
    CloseHandle(snap);
    return procs;
}

// Laeuft die Elternkette hoch, solange der Elternprozess dieselbe Exe ist (Chrome-Audio-Service -> Chrome-Hauptprozess).
DWORD RootPid(const std::map<DWORD, ProcEntry>& procs, DWORD pid)
{
    auto it = procs.find(pid);
    if (it == procs.end()) return pid;
    const std::wstring& exe = it->second.exe;
    for (int i = 0; i < 32; ++i) {
        auto parent = procs.find(it->second.parent);
        if (parent == procs.end() || parent->second.exe != exe || parent->first == pid) break;
        pid = parent->first;
        it  = parent;
    }
    return pid;
}

struct TitleScan {
    std::map<DWORD, std::wstring> titles; // erster sichtbarer Top-Level-Fenstertitel je Prozess
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lp)
{
    auto* scan = reinterpret_cast<TitleScan*>(lp);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t title[256];
    int     len = GetWindowTextW(hwnd, title, 256);
    if (len <= 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    scan->titles.emplace(pid, std::wstring(title, len));
    return TRUE;
}

} // namespace

std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}

std::vector<AppInfo> DiscoverApps()
{
    std::vector<AppInfo> result;

    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool    uninit = SUCCEEDED(hrCom); // RPC_E_CHANGED_MODE: Thread ist schon STA, COM trotzdem nutzbar

    IMMDeviceEnumerator*   enumerator = nullptr;
    IMMDeviceCollection*   devices    = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) &&
        SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {

        auto        procs = SnapshotProcesses();
        TitleScan   scan;
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&scan));
        const DWORD self = GetCurrentProcessId();

        std::map<std::wstring, AppInfo> byExe;
        UINT                            deviceCount = 0;
        devices->GetCount(&deviceCount);
        for (UINT d = 0; d < deviceCount; ++d) {
            IMMDevice*             device = nullptr;
            IAudioSessionManager2* mgr    = nullptr;
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(devices->Item(d, &device)) &&
                SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) &&
                SUCCEEDED(mgr->GetSessionEnumerator(&sessions))) {
                int count = 0;
                sessions->GetCount(&count);
                for (int i = 0; i < count; ++i) {
                    IAudioSessionControl*  ctl  = nullptr;
                    IAudioSessionControl2* ctl2 = nullptr;
                    if (SUCCEEDED(sessions->GetSession(i, &ctl)) &&
                        SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&ctl2)))) {
                        DWORD pid = 0;
                        AudioSessionState state = AudioSessionStateInactive;
                        ctl2->GetProcessId(&pid);
                        ctl2->GetState(&state);
                        if (ctl2->IsSystemSoundsSession() != S_OK && pid != 0 && pid != self && state != AudioSessionStateExpired) {
                            auto p = procs.find(pid);
                            if (p != procs.end()) {
                                const std::wstring& exe = p->second.exe;
                                auto                it  = byExe.find(exe);
                                if (it == byExe.end()) {
                                    AppInfo app;
                                    app.pid = RootPid(procs, pid);
                                    app.exe = exe;
                                    it      = byExe.emplace(exe, app).first;
                                }
                                if (state == AudioSessionStateActive) it->second.playing = true;
                            }
                        }
                    }
                    SafeRelease(ctl2);
                    SafeRelease(ctl);
                }
            }
            SafeRelease(sessions);
            SafeRelease(mgr);
            SafeRelease(device);
        }

        for (auto& kv : byExe) {
            AppInfo app = kv.second;
            auto    t   = scan.titles.find(app.pid);
            if (t == scan.titles.end()) { // Fenster gehoert evtl. einem anderen Prozess derselben Exe
                for (auto& pe : procs)
                    if (pe.second.exe == app.exe && (t = scan.titles.find(pe.first)) != scan.titles.end()) break;
            }
            if (t != scan.titles.end()) app.title = t->second;
            if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, app.pid)) {
                wchar_t buf[MAX_PATH * 2];
                DWORD   len = MAX_PATH * 2;
                if (QueryFullProcessImageNameW(h, 0, buf, &len)) app.path.assign(buf, len);
                CloseHandle(h);
            }
            result.push_back(std::move(app));
        }
        std::sort(result.begin(), result.end(), [](const AppInfo& a, const AppInfo& b) {
            return a.playing != b.playing ? a.playing : a.exe < b.exe;
        });
    }
    SafeRelease(devices);
    SafeRelease(enumerator);
    if (uninit) CoUninitialize();
    return result;
}

std::wstring DisplayName(const AppInfo& app)
{
    std::wstring s = app.exe;
    if (!app.title.empty()) s += L" - " + (app.title.size() > 60 ? app.title.substr(0, 57) + L"..." : app.title);
    if (app.playing) s += L"  [spielt]";
    return s;
}

std::wstring PrettyExeName(const std::wstring& exeLower)
{
    if (exeLower.rfind(L"ts3client", 0) == 0) return L"TeamSpeak 3";
    std::wstring n = exeLower;
    if (n.size() > 4 && n.compare(n.size() - 4, 4, L".exe") == 0) n.resize(n.size() - 4);
    if (!n.empty()) n[0] = static_cast<wchar_t>(towupper(n[0]));
    return n;
}

const AppInfo* FindByExe(const std::vector<AppInfo>& apps, const std::wstring& exeLower)
{
    for (const auto& a : apps)
        if (a.exe == exeLower) return &a;
    return nullptr;
}
