#include "mini_window.h"

#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "engine.h"
#include "i18n.h"
#include "ui_theme.h"
#include "ui_widgets.h"

namespace {

using namespace ui;

constexpr UINT_PTR kTimerId = 1;
constexpr int      kW = 340, kH = 118;

struct Win {
    HWND                  hwnd = nullptr;
    float                 s    = 1;
    std::unique_ptr<Font> body, small, tiny;
    RectF                 slider, mute, toggle, close;
    int                   hover = 0;      // 1 Regler, 2 Stumm, 3 Ein/Aus, 4 Schliessen
    bool                  dragVol = false;
    bool                  tracking = false;
    float                 anim = 0;        // Ein/Aus-Schalter
};

std::mutex        g_mutex;
std::thread       g_thread;
std::atomic<bool> g_running{false};
std::atomic<HWND> g_hwnd{nullptr};

bool In(const RectF& r, int x, int y) { return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height; }

float DpiScale(HWND hwnd)
{
    UINT dpi = 96;
    if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
        using Fn = UINT(WINAPI*)(HWND);
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(user, "GetDpiForWindow")))) dpi = fn(hwnd);
    }
    return (dpi ? dpi : 96) / 96.0f;
}

void Layout(Win* w)
{
    const float s = w->s;
    w->close      = RectF((kW - 30) * s, 8 * s, 22 * s, 22 * s);
    w->slider     = RectF(14 * s, 62 * s, 190 * s, 22 * s);
    w->mute       = RectF(214 * s, 62 * s, 30 * s, 24 * s);
    w->toggle     = RectF((kW - 14 - 48) * s, 61 * s, 48 * s, 26 * s);
}

void Paint(Win* w, HDC dc, const RECT& rc)
{
    const int wd = rc.right - rc.left, ht = rc.bottom - rc.top;
    HDC       mem = CreateCompatibleDC(dc);
    HBITMAP   bmp = CreateCompatibleBitmap(dc, wd, ht);
    HGDIOBJ   old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(C(col::bg));
        const float s  = w->s;
        Engine&     en = Engine::Get();
        const auto  st = en.GetSettings();
        const RectF all(0.5f, 0.5f, wd - 1.0f, ht - 1.0f);
        StrokeRound(g, all, 12 * s, C(col::border));
        DrawIcon(g, Icon::Note, RectF(12 * s, 10 * s, 20 * s, 20 * s), C(col::accent), s);
        const std::wstring src = en.SourceLabel();
        Text(g, src.empty() ? std::wstring(Tr(L"Keine Quelle")) : src, *w->body, RectF(38 * s, 8 * s, (kW - 80) * s, 24 * s), C(col::text));
        // Schliessen-Kreuz
        {
            Pen x(w->hover == 4 ? C(col::text) : C(col::sub), 1.6f * s);
            const RectF& c = w->close;
            g.DrawLine(&x, c.X + 6 * s, c.Y + 6 * s, c.X + c.Width - 6 * s, c.Y + c.Height - 6 * s);
            g.DrawLine(&x, c.X + c.Width - 6 * s, c.Y + 6 * s, c.X + 6 * s, c.Y + c.Height - 6 * s);
        }
        const std::wstring status = en.Status();
        Text(g, status, *w->tiny, RectF(38 * s, 30 * s, (kW - 52) * s, 16 * s), C(col::sub));
        // Musik-Lautstaerke
        Text(g, Tr(L"Musik"), *w->tiny, RectF(14 * s, 46 * s, 120 * s, 14 * s), C(col::sub));
        Text(g, std::to_wstring(st.musicVolPct) + L" %", *w->tiny, RectF(150 * s, 46 * s, 54 * s, 14 * s), C(col::sub), StringAlignmentFar);
        DrawSlider(g, w->slider, st.musicVolPct / 200.0f, w->dragVol || w->hover == 1, true);
        // Stumm
        const bool muted = en.IsMicMuted();
        FillRound(g, w->mute, 7 * s, muted ? C(col::bad, 70) : (w->hover == 2 ? C(col::cardHover) : C(col::track, 140)));
        {
            const Color ic = muted ? C(col::bad) : C(col::text);
            const RectF& b = w->mute;
            const float cx = b.X + b.Width / 2, cy = b.Y + b.Height / 2;
            FillRound(g, RectF(cx - 3 * s, cy - 8 * s, 6 * s, 10 * s), 3 * s, ic);
            Pen pen(ic, std::max(1.3f, 1.4f * s));
            g.DrawArc(&pen, cx - 5.5f * s, cy - 6.5f * s, 11 * s, 11 * s, 10, 160);
            g.DrawLine(&pen, cx, cy + 5 * s, cx, cy + 8 * s);
            g.DrawLine(&pen, cx - 3 * s, cy + 8 * s, cx + 3 * s, cy + 8 * s);
            if (muted) {
                Pen slash(C(col::bad), std::max(2.0f, 2.2f * s));
                g.DrawLine(&slash, cx - 9 * s, cy - 9 * s, cx + 9 * s, cy + 9 * s);
            }
        }
        // Ein/Aus
        Text(g, Tr(L"Musik einspielen"), *w->tiny, RectF(w->toggle.X - 120 * s, w->toggle.Y + 30 * s, 168 * s, 14 * s), C(col::sub), StringAlignmentFar);
        DrawToggle(g, w->toggle, w->anim);
        // Hinweis unten
        Text(g, Tr(L"Ziehen verschiebt das Fenster"), *w->tiny, RectF(14 * s, 94 * s, 200 * s, 16 * s), C(col::sub, 110));
    }
    BitBlt(dc, 0, 0, wd, ht, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
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
        auto font = [w](float px) { return std::make_unique<Font>(L"Segoe UI", px * w->s, FontStyleRegular, UnitPixel); };
        w->body   = font(14);
        w->small  = font(12);
        w->tiny   = font(11);
        w->anim   = Engine::Get().GetSettings().enabled ? 1.0f : 0.0f;
        Layout(w);
        SetTimer(hwnd, kTimerId, 33, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (w) {
            const float target = Engine::Get().GetSettings().enabled ? 1.0f : 0.0f;
            w->anim += (target - w->anim) * 0.35f;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         dc = BeginPaint(hwnd, &ps);
        RECT        rc;
        GetClientRect(hwnd, &rc);
        if (w) Paint(w, dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: { // Hintergrund = Titelleiste: das Fenster laesst sich ziehen
        if (!w) break;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        if (In(w->slider, p.x, p.y) || In(w->mute, p.x, p.y) || In(w->toggle, p.x, p.y) || In(w->close, p.x, p.y)) return HTCLIENT;
        return HTCAPTION;
    }
    case WM_MOUSEMOVE:
        if (w) {
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (w->dragVol) Engine::Get().SetMusicVolume(SliderValue(w->slider, x, 200));
            w->hover = In(w->slider, x, y) ? 1 : In(w->mute, x, y) ? 2 : In(w->toggle, x, y) ? 3 : In(w->close, x, y) ? 4 : 0;
            if (!w->tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
                w->tracking = true;
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (w) {
            w->hover    = 0;
            w->tracking = false;
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (w) {
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            Engine& en = Engine::Get();
            if (In(w->slider, x, y)) {
                w->dragVol = true;
                SetCapture(hwnd);
                en.SetMusicVolume(SliderValue(w->slider, x, 200));
            } else if (In(w->mute, x, y)) {
                en.SetMicMuted(!en.IsMicMuted());
            } else if (In(w->toggle, x, y)) {
                en.SetEnabled(!en.GetSettings().enabled);
            } else if (In(w->close, x, y)) {
                DestroyWindow(hwnd);
            }
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (w && w->dragVol) {
            w->dragVol = false;
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        RECT r;
        if (GetWindowRect(hwnd, &r)) Engine::Get().SetWindowRect(2, r.left, r.top, 0, 0);
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
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VoiceTuneMini";
    RegisterClassW(&wc);

    {
        Win        win;
        const auto st = Engine::Get().GetSettings();
        // Position gemerkt? Sonst unten rechts im Arbeitsbereich. Auf einen sichtbaren Monitor klemmen.
        win.s = 1.0f;
        if (HDC sdc = GetDC(nullptr)) {
            win.s = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f;
            ReleaseDC(nullptr, sdc);
        }
        const int w = static_cast<int>(kW * win.s), h = static_cast<int>(kH * win.s);
        RECT      work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int x = work.right - w - 24, y = work.bottom - h - 24;
        if (st.miniX != Settings::kNoPos && st.miniY != Settings::kNoPos) {
            const RECT want{st.miniX, st.miniY, st.miniX + w, st.miniY + h};
            if (MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) {
                x = st.miniX;
                y = st.miniY;
            }
        }
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"VoiceTune Mini", WS_POPUP, x, y, w, h, nullptr, nullptr, wc.hInstance, &win);
        if (hwnd) {
            g_hwnd = hwnd;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        g_hwnd = nullptr;
        win.body.reset();
        win.small.reset();
        win.tiny.reset();
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    GdiplusShutdown(token);
    g_running = false;
}

} // namespace

void OpenMiniWindow()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_running) {
        if (HWND h = g_hwnd.load()) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        return;
    }
    if (g_thread.joinable()) g_thread.join();
    g_running = true;
    g_thread  = std::thread(WindowThread);
}

void CloseMiniWindow()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (HWND h = g_hwnd.load()) PostMessageW(h, WM_CLOSE, 0, 0);
    if (g_thread.joinable()) g_thread.join();
}
