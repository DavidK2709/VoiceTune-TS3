
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
 
#include "tray.h"

#include <windows.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

using std::max;
using std::min;
#include <gdiplus.h>

#include "commands.h"
#include "config_dialog.h"
#include "engine.h"
#include "i18n.h"
#include "mini_window.h"
#include "soundboard_window.h"

namespace {

constexpr UINT WM_TRAY = WM_APP + 7;
constexpr UINT kIconId = 1;

std::mutex        g_mutex;
std::thread       g_thread;
std::atomic<bool> g_running{false};
std::atomic<HWND> g_hwnd{nullptr};

// App-Icon (PNG-Ressource 101 der DLL) als HICON
HICON LoadTrayIcon()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&LoadTrayIcon), &self)) return nullptr;
    HRSRC       res  = FindResourceW(self, MAKEINTRESOURCEW(101), RT_RCDATA);
    HGLOBAL     mem  = res ? LoadResource(self, res) : nullptr;
    const void* data = mem ? LockResource(mem) : nullptr;
    if (!data) return nullptr;
    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), SizeofResource(self, res));
    if (!stream) return nullptr;
    HICON icon = nullptr;
    {
        Gdiplus::Bitmap src(stream);
        if (src.GetLastStatus() == Gdiplus::Ok) {
            const int          px = max(16, GetSystemMetrics(SM_CXSMICON));
            Gdiplus::Bitmap    scaled(px, px, PixelFormat32bppARGB);
            Gdiplus::Graphics  gg(&scaled);
            gg.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gg.Clear(Gdiplus::Color(0, 0, 0, 0));
            gg.DrawImage(&src, Gdiplus::Rect(0, 0, px, px));
            scaled.GetHICON(&icon);
        }
    }
    stream->Release();
    return icon;
}

enum { ID_OPEN = 1, ID_TOGGLE, ID_MUTE, ID_PROFILE, ID_SOUNDBOARD, ID_MINI, ID_HIDE };

void ShowMenu(HWND hwnd)
{
    Engine&        en = Engine::Get();
    const Settings st = en.GetSettings();
    HMENU          m  = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, Tr(L"Steuerung öffnen"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (st.enabled ? MF_CHECKED : 0), ID_TOGGLE, Tr(L"Musik einspielen"));
    AppendMenuW(m, MF_STRING | (en.IsMicMuted() ? MF_CHECKED : 0), ID_MUTE, Tr(L"Mikrofon stumm"));
    AppendMenuW(m, MF_STRING | (st.profiles.items.empty() ? MF_GRAYED : 0), ID_PROFILE, Tr(L"Nächstes Profil"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SOUNDBOARD, Tr(L"Soundboard öffnen"));
    AppendMenuW(m, MF_STRING, ID_MINI, Tr(L"Mini-Fenster öffnen"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_HIDE, Tr(L"Tray-Symbol ausblenden"));
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(hwnd); // sonst schliesst das Menue nicht beim Wegklicken
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case ID_OPEN: OpenConfigDialog(); break;
    case ID_TOGGLE: RunCommand("toggle"); break;
    case ID_MUTE: RunCommand("micmute"); break;
    case ID_PROFILE: RunCommand("profile_next"); break;
    case ID_SOUNDBOARD: OpenSoundboardWindow(); break;
    case ID_MINI: OpenMiniWindow(); break;
    case ID_HIDE: Engine::Get().SetTrayIcon(false); DestroyWindow(hwnd); break;
    default: break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP) OpenConfigDialog();
        else if (LOWORD(lp) == WM_RBUTTONUP) ShowMenu(hwnd);
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd;
        nid.uID    = kIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TrayThread()
{
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR                    token = 0;
    Gdiplus::GdiplusStartup(&token, &gsi, nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VoiceTuneTray";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"VoiceTune Tray", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd) {
        HICON           icon = LoadTrayIcon();
        NOTIFYICONDATAW nid{};
        nid.cbSize           = sizeof(nid);
        nid.hWnd             = hwnd;
        nid.uID              = kIconId;
        nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAY;
        nid.hIcon            = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
        lstrcpynW(nid.szTip, L"VoiceTune", ARRAYSIZE(nid.szTip));
        Shell_NotifyIconW(NIM_ADD, &nid);
        g_hwnd = hwnd;
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        g_hwnd = nullptr;
        if (icon) DestroyIcon(icon);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    Gdiplus::GdiplusShutdown(token);
    g_running = false;
}

} // namespace

void TraySetEnabled(bool on)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (on) {
        if (g_running.exchange(true)) return;
        if (g_thread.joinable()) g_thread.join();
        g_thread = std::thread(TrayThread);
    } else {
        if (HWND h = g_hwnd.load()) PostMessageW(h, WM_CLOSE, 0, 0);
        if (g_thread.joinable()) g_thread.join();
    }
}

bool TrayIsEnabled() { return g_running.load(); }
