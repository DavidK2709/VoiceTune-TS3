
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
 #include "soundboard_window.h"

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "engine.h"
#include "i18n.h"
#include "soundboard_view.h"
#include "ui_theme.h"

namespace {

using namespace ui;

constexpr UINT_PTR kTimerId = 1;
constexpr int      kTimerMs = 33;

struct Win {
    HWND                  hwnd = nullptr;
    float                 s    = 1;
    int                   tick = 0;
    sbv::View             view;
    std::unique_ptr<Font> body, small, tiny, head;
    bool                  pinned = false;
    bool                  tracking = false;
};

std::mutex        g_mutex;
std::thread       g_thread;
std::atomic<bool> g_running{false};
std::atomic<HWND> g_hwnd{nullptr};

float DpiScale(HWND hwnd)
{
    UINT dpi = 96;
    if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
        using Fn = UINT(WINAPI*)(HWND);
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(user, "GetDpiForWindow")))) dpi = fn(hwnd);
    }
    if (dpi == 0) dpi = 96;
    return dpi / 96.0f;
}

sbv::Host MakeHost(Win* w)
{
    sbv::Host h;
    h.hwnd   = w->hwnd;
    h.s      = w->s;
    h.tick   = w->tick;
    h.popout = true;
    h.body   = w->body.get();
    h.small  = w->small.get();
    h.tiny   = w->tiny.get();
    h.head   = w->head.get();
    return h;
}

void MakeFonts(Win* w)
{
    auto font = [w](float px, int style) { return std::make_unique<Font>(L"Segoe UI", px * w->s, style, UnitPixel); };
    w->head  = font(15, FontStyleBold);
    w->body  = font(14, FontStyleRegular);
    w->small = font(12, FontStyleRegular);
    w->tiny  = font(11, FontStyleRegular);
}

void ApplyPinned(Win* w, bool pinned)
{
    if (w->pinned == pinned) return;
    w->pinned = pinned;
    SetWindowPos(w->hwnd, pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Paint(Win* w, HDC dc, const RECT& rc)
{
    const int wd = rc.right - rc.left, ht = rc.bottom - rc.top;
    if (wd <= 0 || ht <= 0) return;
    HDC     mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, wd, ht);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(C(col::bg));
        const float  m = 12 * w->s;
        const RectF  area(m, m, wd - 2 * m, ht - 2 * m);
        const auto   h = MakeHost(w);
        w->view.Paint(g, area, h);
    }
    BitBlt(dc, 0, 0, wd, ht, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

std::vector<std::wstring> DroppedPaths(HDROP drop, POINT* pt)
{
    DragQueryPoint(drop, pt);
    std::vector<std::wstring> paths;
    const UINT                n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        wchar_t buf[MAX_PATH * 2];
        if (DragQueryFileW(drop, i, buf, MAX_PATH * 2)) paths.push_back(buf);
    }
    DragFinish(drop);
    return paths;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* w = reinterpret_cast<Win*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        w       = static_cast<Win*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        w->hwnd = hwnd;
        w->s    = DpiScale(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(w));
        MakeFonts(w);
        DragAcceptFiles(hwnd, TRUE);
        if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
            using FilterFn = BOOL(WINAPI*)(HWND, UINT, DWORD, void*);
            if (auto filter = reinterpret_cast<FilterFn>(reinterpret_cast<void*>(GetProcAddress(user, "ChangeWindowMessageFilterEx")))) {
                filter(hwnd, WM_DROPFILES, 1 /*MSGFLT_ALLOW*/, nullptr);
                filter(hwnd, 0x0049 /*WM_COPYGLOBALDATA*/, 1, nullptr);
            }
        }
        BOOL dark = TRUE; // dunkle Titelleiste (Windows 10/11)
        if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
            using Fn = HRESULT(WINAPI*)(HWND, DWORD, const void*, DWORD);
            if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")))) fn(hwnd, 20, &dark, sizeof(dark));
            FreeLibrary(dwm);
        }
        ApplyPinned(w, Engine::Get().GetSettings().soundPinned);
        SetTimer(hwnd, kTimerId, kTimerMs, nullptr);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        const float s = w ? w->s : 1.0f;
        mm->ptMinTrackSize.x = static_cast<LONG>(560 * s);
        mm->ptMinTrackSize.y = static_cast<LONG>(460 * s);
        return 0;
    }
    case WM_DPICHANGED:
        if (w) {
            w->s = LOWORD(wp) / 96.0f;
            MakeFonts(w);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_TIMER:
        if (w) {
            ++w->tick;
            ApplyPinned(w, Engine::Get().GetSettings().soundPinned);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        GetClientRect(hwnd, &rc);
        if (w) Paint(w, dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (w) {
            const auto h = MakeHost(w);
            w->view.MouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), h);
            if (!w->tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
                w->tracking = true;
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (w) {
            w->tracking = false;
            const auto h = MakeHost(w);
            w->view.MouseMove(-1, -1, h);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (w) {
            SetFocus(hwnd);
            const auto h = MakeHost(w);
            w->view.MouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), h);
            if (w->view.Dragging()) SetCapture(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (w) {
            const auto h = MakeHost(w);
            w->view.MouseUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), h);
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (w && w->view.Dragging()) {
            w->view.Cancel();
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (w) {
            const auto h = MakeHost(w);
            w->view.RightDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), h);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (w) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            const auto h = MakeHost(w);
            w->view.Wheel(p.x, p.y, GET_WHEEL_DELTA_WPARAM(wp), h);
        }
        return 0;
    case WM_CHAR:
        if (w) {
            const auto h = MakeHost(w);
            w->view.Char(static_cast<wchar_t>(wp), h);
        }
        return 0;
    case WM_KEYDOWN:
        if (w) {
            const auto h = MakeHost(w);
            if (!w->view.Key(wp, h) && wp == VK_ESCAPE) DestroyWindow(hwnd);
        }
        return 0;
    case WM_DROPFILES:
        if (w) {
            POINT pt{0, 0};
            const auto paths = DroppedPaths(reinterpret_cast<HDROP>(wp), &pt);
            const auto h     = MakeHost(w);
            w->view.Drop(pt.x, pt.y, paths, h);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        WINDOWPLACEMENT wpl{};
        wpl.length = sizeof(wpl);
        if (GetWindowPlacement(hwnd, &wpl)) { // Position und Groesse (auch aus dem Zustand "minimiert/maximiert" die normale Lage) fuer das naechste Oeffnen merken
            const RECT& r = wpl.rcNormalPosition;
            Engine::Get().SetWindowRect(1, r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
        KillTimer(hwnd, kTimerId);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void WindowThread()
{
    GdiplusStartupInput gsi;
    ULONG_PTR           token = 0;
    GdiplusStartup(&token, &gsi, nullptr);

    WNDCLASSW wc{};
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VoiceTuneSoundboard";
    RegisterClassW(&wc);

    {
        Win   win;
        const Settings st0 = Engine::Get().GetSettings();
        int            px = CW_USEDEFAULT, py = CW_USEDEFAULT, pw = 860, ph = 600;
        bool           restored = false;
        if (st0.sbX != Settings::kNoPos && st0.sbY != Settings::kNoPos && st0.sbW >= 300 && st0.sbH >= 300) {
            const RECT want{st0.sbX, st0.sbY, st0.sbX + st0.sbW, st0.sbY + st0.sbH};
            if (MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) { // nur wenn das Fenster noch auf einem vorhandenen Bildschirm liegt
                px = st0.sbX;
                py = st0.sbY;
                pw = st0.sbW;
                ph = st0.sbH;
                restored = true;
            }
        }
        HWND  hwnd = CreateWindowExW(0, wc.lpszClassName, L"VoiceTune – Soundboard", WS_OVERLAPPEDWINDOW, px, py, pw, ph, nullptr, nullptr, wc.hInstance, &win);
        if (hwnd) {
            const float s = win.s;
            if (!restored) SetWindowPos(hwnd, nullptr, 0, 0, static_cast<int>(820 * s), static_cast<int>(580 * s), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            g_hwnd = hwnd;
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        g_hwnd = nullptr;
        win.head.reset(); // vor GdiplusShutdown freigeben
        win.body.reset();
        win.small.reset();
        win.tiny.reset();
    }

    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    GdiplusShutdown(token);
    g_running = false;
}

} // namespace

void OpenSoundboardWindow()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_running) {
        if (HWND h = g_hwnd.load()) {
            ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
        }
        return;
    }
    if (g_thread.joinable()) g_thread.join();
    g_running = true;
    g_thread  = std::thread(WindowThread);
}

void CloseSoundboardWindow()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (HWND h = g_hwnd.load()) PostMessageW(h, WM_CLOSE, 0, 0);
    if (g_thread.joinable()) g_thread.join();
}
