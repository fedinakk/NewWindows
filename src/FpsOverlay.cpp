#include "FpsOverlay.h"

#include <cstdio>

namespace {
const wchar_t kFpsClass[] = L"InfiniteCanvasFps";
const UINT_PTR kRepaintTimer = 1;
const int kRefreshMs = 100;
}

FpsOverlay* FpsOverlay::s_instance = nullptr;

void FpsOverlay::Init(HINSTANCE instance, StatsFn provider)
{
    instance_ = instance;
    provider_ = std::move(provider);
    s_instance = this;
}

void FpsOverlay::EnsureCreated()
{
    if (hwnd_)
        return;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kFpsClass;
    RegisterClassW(&wc);

    // Click-through (WS_EX_TRANSPARENT) so it never interferes with gestures.
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                            WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                            kFpsClass, L"FPS", WS_POPUP,
                            0, 0, 10, 10, nullptr, nullptr, instance_, nullptr);
    if (!hwnd_) {
        Log(L"FpsOverlay: CreateWindowEx failed (error %lu)", GetLastError());
        return;
    }
    SetLayeredWindowAttributes(hwnd_, 0, 215, LWA_ALPHA);
}

void FpsOverlay::Reposition()
{
    const UINT dpi = DpiForWindow(hwnd_);
    const int width = MulDiv(250, dpi, 96);
    const int height = MulDiv(56, dpi, 96);
    const int margin = MulDiv(12, dpi, 96);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    SetWindowPos(hwnd_, HWND_TOPMOST,
                 mi.rcWork.right - width - margin, mi.rcWork.top + margin,
                 width, height, SWP_NOACTIVATE);
}

void FpsOverlay::Toggle()
{
    if (IsVisible())
        Hide();
    else
        Show();
}

void FpsOverlay::Show()
{
    EnsureCreated();
    if (!hwnd_)
        return;
    Reposition();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetTimer(hwnd_, kRepaintTimer, kRefreshMs, nullptr);
}

void FpsOverlay::Hide()
{
    if (!hwnd_)
        return;
    KillTimer(hwnd_, kRepaintTimer);
    ShowWindow(hwnd_, SW_HIDE);
}

bool FpsOverlay::IsVisible() const
{
    return hwnd_ && IsWindowVisible(hwnd_);
}

void FpsOverlay::Destroy()
{
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    s_instance = nullptr;
}

void FpsOverlay::Paint(HDC dc, const RECT& client)
{
    const UINT dpi = DpiForWindow(hwnd_);
    FrameStats stats = provider_ ? provider_() : FrameStats{};

    HBRUSH bg = CreateSolidBrush(RGB(16, 18, 24));
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    HFONT bigFont = CreateFontW(-MulDiv(18, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT smallFont = CreateFontW(-MulDiv(11, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, bigFont);
    SetBkMode(dc, TRANSPARENT);

    wchar_t line1[64], line2[96];
    if (stats.fps > 0) {
        _snwprintf_s(line1, _TRUNCATE, L"%.0f FPS", stats.fps);
        _snwprintf_s(line2, _TRUNCATE, L"кадр %.2f мс · масштаб %.0f%% · окон %d",
                     stats.frameMs, stats.scale * 100.0, stats.windows);
    } else {
        _snwprintf_s(line1, _TRUNCATE, L"— FPS");
        _snwprintf_s(line2, _TRUNCATE, L"холст неподвижен · масштаб %.0f%% · окон %d",
                     stats.scale * 100.0, stats.windows);
    }

    RECT text = client;
    InflateRect(&text, -MulDiv(10, dpi, 96), -MulDiv(6, dpi, 96));
    SetTextColor(dc, RGB(120, 235, 160));
    DrawTextW(dc, line1, -1, &text, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);
    SelectObject(dc, smallFont);
    SetTextColor(dc, RGB(185, 190, 205));
    DrawTextW(dc, line2, -1, &text, DT_SINGLELINE | DT_LEFT | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);

    SelectObject(dc, oldFont);
    DeleteObject(bigFont);
    DeleteObject(smallFont);
}

LRESULT CALLBACK FpsOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    FpsOverlay* self = s_instance;
    if (!self || self->hwnd_ != hwnd)
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wparam == kRepaintTimer)
            InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);

        HDC memDc = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
        HGDIOBJ oldBmp = SelectObject(memDc, bmp);
        self->Paint(memDc, client);
        BitBlt(dc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
        SelectObject(memDc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDc);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
