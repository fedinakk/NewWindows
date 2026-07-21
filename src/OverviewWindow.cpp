#include "OverviewWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace {

const wchar_t kOverlayClass[] = L"InfiniteCanvasOverlay";

const COLORREF kBgColor        = RGB(24, 26, 32);
const COLORREF kWinFill        = RGB(58, 63, 77);
const COLORREF kWinBorder      = RGB(122, 139, 255);
const COLORREF kWinFillHover   = RGB(82, 92, 118);
const COLORREF kWinBorderHover = RGB(240, 244, 255);
const COLORREF kPinnedFill     = RGB(40, 42, 50);
const COLORREF kPinnedBorder   = RGB(96, 100, 110);
const COLORREF kViewportBorder = RGB(255, 204, 77);
const COLORREF kTextColor      = RGB(230, 232, 240);
const COLORREF kDimTextColor   = RGB(150, 155, 170);

void UnionInto(RECT& acc, const RECT& r)
{
    acc.left = std::min(acc.left, r.left);
    acc.top = std::min(acc.top, r.top);
    acc.right = std::max(acc.right, r.right);
    acc.bottom = std::max(acc.bottom, r.bottom);
}

} // namespace

OverviewWindow* OverviewWindow::s_instance = nullptr;

void OverviewWindow::EnsureCreated(HINSTANCE instance)
{
    if (hwnd_)
        return;

    s_instance = this;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kOverlayClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            kOverlayClass, L"Обзор холста", WS_POPUP,
                            0, 0, 100, 100, nullptr, nullptr, instance, nullptr);
    if (!hwnd_)
        Log(L"Overview: CreateWindowEx failed (error %lu)", GetLastError());
}

void OverviewWindow::Show(HINSTANCE instance, std::vector<OverviewItem> items,
                          RECT viewportVirt, int alpha, PickFn onPick)
{
    EnsureCreated(instance);
    if (!hwnd_)
        return;

    items_ = std::move(items);
    viewport_ = viewportVirt;
    pick_ = std::move(onPick);
    hover_ = -1;

    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);

    SetLayeredWindowAttributes(hwnd_, 0, (BYTE)alpha, LWA_ALPHA);
    SetWindowPos(hwnd_, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_SHOWWINDOW);
    ComputeMapping();
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void OverviewWindow::Hide()
{
    if (hwnd_ && IsWindowVisible(hwnd_))
        ShowWindow(hwnd_, SW_HIDE);
}

bool OverviewWindow::IsVisible() const
{
    return hwnd_ && IsWindowVisible(hwnd_);
}

void OverviewWindow::Destroy()
{
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    s_instance = nullptr;
}

void OverviewWindow::ComputeMapping()
{
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int cw = client.right - client.left;
    const int ch = client.bottom - client.top;

    RECT world = viewport_;
    for (const auto& item : items_)
        UnionInto(world, item.virt);

    double worldW = std::max(1.0, (double)(world.right - world.left));
    double worldH = std::max(1.0, (double)(world.bottom - world.top));

    const double pad = 56.0;
    scale_ = std::min((cw - 2 * pad) / worldW, (ch - 2 * pad) / worldH);
    scale_ = std::max(0.001, std::min(scale_, 1.0));

    offsetX_ = (cw - worldW * scale_) / 2.0 - world.left * scale_;
    offsetY_ = (ch - worldH * scale_) / 2.0 - world.top * scale_;
}

RECT OverviewWindow::MapRect(const RECT& world) const
{
    RECT r;
    r.left = (LONG)std::lround(world.left * scale_ + offsetX_);
    r.top = (LONG)std::lround(world.top * scale_ + offsetY_);
    r.right = (LONG)std::lround(world.right * scale_ + offsetX_);
    r.bottom = (LONG)std::lround(world.bottom * scale_ + offsetY_);
    return r;
}

int OverviewWindow::HitTest(POINT client) const
{
    for (size_t i = 0; i < items_.size(); ++i) {
        RECT r = MapRect(items_[i].virt);
        if (PtInRect(&r, client))
            return (int)i;
    }
    return -1;
}

void OverviewWindow::Paint(HDC dc, const RECT& client)
{
    const UINT dpi = DpiForWindow(hwnd_);

    HBRUSH bgBrush = CreateSolidBrush(kBgColor);
    FillRect(dc, &client, bgBrush);
    DeleteObject(bgBrush);

    HFONT titleFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT legendFont = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, titleFont);
    SetBkMode(dc, TRANSPARENT);

    // Draw bottom-to-top so the topmost window is painted last.
    for (int i = (int)items_.size() - 1; i >= 0; --i) {
        const auto& item = items_[i];
        RECT r = MapRect(item.virt);
        if (r.right - r.left < 3 || r.bottom - r.top < 3)
            continue;

        const bool hovered = (i == hover_);
        COLORREF fill = item.movable ? (hovered ? kWinFillHover : kWinFill) : kPinnedFill;
        COLORREF border = item.movable ? (hovered ? kWinBorderHover : kWinBorder) : kPinnedBorder;

        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(dc, &r, brush);
        DeleteObject(brush);

        HBRUSH frame = CreateSolidBrush(border);
        RECT edge = r;
        FrameRect(dc, &edge, frame);
        if (hovered) {
            InflateRect(&edge, -1, -1);
            FrameRect(dc, &edge, frame);
        }
        DeleteObject(frame);

        if (r.right - r.left >= 56 && r.bottom - r.top >= 22 && !item.title.empty()) {
            RECT text = r;
            InflateRect(&text, -6, -4);
            SelectObject(dc, titleFont);
            SetTextColor(dc, item.movable ? kTextColor : kDimTextColor);
            DrawTextW(dc, item.title.c_str(), -1, &text,
                      DT_SINGLELINE | DT_END_ELLIPSIS | DT_LEFT | DT_TOP | DT_NOPREFIX);
        }
    }

    // Current viewport ("camera") frame.
    {
        RECT vp = MapRect(viewport_);
        HBRUSH frame = CreateSolidBrush(kViewportBorder);
        FrameRect(dc, &vp, frame);
        InflateRect(&vp, -1, -1);
        FrameRect(dc, &vp, frame);
        DeleteObject(frame);
    }

    // Header and legend.
    SelectObject(dc, legendFont);
    SetTextColor(dc, kTextColor);
    RECT header = client;
    InflateRect(&header, -MulDiv(20, dpi, 96), -MulDiv(14, dpi, 96));
    DrawTextW(dc, L"Обзор холста", -1, &header, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);
    SetTextColor(dc, kDimTextColor);
    DrawTextW(dc, L"ЛКМ — перейти к окну   ·   Esc / ПКМ — закрыть", -1, &header,
              DT_SINGLELINE | DT_LEFT | DT_BOTTOM | DT_NOPREFIX);

    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(legendFont);
}

LRESULT CALLBACK OverviewWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    OverviewWindow* self = s_instance;
    if (!self || self->hwnd_ != hwnd)
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

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

    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        int hit = self->HitTest(pt);
        if (hit != self->hover_) {
            self->hover_ = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        int hit = self->HitTest(pt);
        self->Hide();
        if (hit >= 0 && self->pick_)
            self->pick_(self->items_[hit].hwnd);
        return 0;
    }

    case WM_RBUTTONUP:
        self->Hide();
        return 0;

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
            self->Hide();
        return 0;

    case WM_KILLFOCUS:
        self->Hide();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
