#include "CanvasBackdrop.h"

#include "Config.h"

#include <algorithm>

namespace {

const wchar_t kBackdropClass[] = L"InfiniteCanvasBackdrop";
const UINT kSpawnWorkerW = 0x052C; // undocumented Progman message

struct DefViewSearch {
    HWND host = nullptr; // top-level window containing SHELLDLL_DefView
};

BOOL CALLBACK FindDefViewHost(HWND hwnd, LPARAM lparam)
{
    auto* search = (DefViewSearch*)lparam;
    if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) {
        search->host = hwnd;
        return FALSE;
    }
    return TRUE;
}

} // namespace

CanvasBackdrop* CanvasBackdrop::s_instance = nullptr;

HWND CanvasBackdrop::FindWallpaperHost(HWND* insertAfterChild)
{
    *insertAfterChild = nullptr;

    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman)
        return nullptr;

    // Ask Progman to split the wallpaper into its own WorkerW layer. Two
    // parameter conventions exist across Win10/11 builds; send both.
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(progman, kSpawnWorkerW, 0xD, 0x1, SMTO_NORMAL, 1000, &ignored);
    SendMessageTimeoutW(progman, kSpawnWorkerW, 0, 0, SMTO_NORMAL, 1000, &ignored);

    // Classic Win10/11 layout: the WorkerW right after the one hosting
    // SHELLDLL_DefView is the wallpaper surface behind the icons.
    DefViewSearch search;
    EnumWindows(FindDefViewHost, (LPARAM)&search);
    if (search.host && search.host != progman) {
        wchar_t cls[64] = L"";
        GetClassNameW(search.host, cls, 64);
        if (_wcsicmp(cls, L"WorkerW") == 0) {
            HWND next = FindWindowExW(nullptr, search.host, L"WorkerW", nullptr);
            if (next)
                return next;
        }
    }

    // Win11 24H2 layout: SHELLDLL_DefView and a WorkerW live as children of
    // Progman; parent into that WorkerW child.
    HWND workerChild = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
    if (workerChild)
        return workerChild;

    // Last resort: Progman itself, inserted just below the icon view so the
    // icons stay on top. Without a DefView to anchor under, give up.
    HWND defview = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defview) {
        *insertAfterChild = defview;
        return progman;
    }
    return nullptr;
}

void CanvasBackdrop::Create(HINSTANCE instance, const Config* cfg)
{
    instance_ = instance;
    cfg_ = cfg;
    if (!cfg_->backdropEnabled || hwnd_)
        return;

    s_instance = this;

    HWND insertAfter = nullptr;
    HWND host = FindWallpaperHost(&insertAfter);
    if (!host) {
        Log(L"Backdrop: wallpaper host not found on this shell build, backdrop disabled");
        return;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kBackdropClass;
    RegisterClassW(&wc);

    RECT hostRect{};
    GetClientRect(host, &hostRect);
    if (hostRect.right <= 0 || hostRect.bottom <= 0) {
        hostRect.right = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        hostRect.bottom = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE, kBackdropClass, L"InfiniteCanvas backdrop",
                            WS_CHILD, 0, 0, hostRect.right, hostRect.bottom,
                            host, nullptr, instance, nullptr);
    if (!hwnd_) {
        Log(L"Backdrop: CreateWindowEx failed (error %lu)", GetLastError());
        return;
    }

    if (insertAfter) {
        // Inside Progman: sit just below the icon view in the child z-order.
        SetWindowPos(hwnd_, insertAfter, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    Log(L"Backdrop: attached to wallpaper host %p", (void*)host);
}

void CanvasBackdrop::Destroy()
{
    if (hwnd_) {
        if (IsWindow(hwnd_))
            DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (gridTile_) {
        DeleteObject(gridTile_);
        gridTile_ = nullptr;
        gridTilePx_ = 0;
    }
    s_instance = nullptr;
}

void CanvasBackdrop::EnsureAlive()
{
    if (!cfg_ || !cfg_->backdropEnabled)
        return;
    if (hwnd_ && IsWindow(hwnd_))
        return;
    // Explorer restarted and destroyed our child window along with WorkerW.
    hwnd_ = nullptr;
    Create(instance_, cfg_);
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void CanvasBackdrop::Refit()
{
    if (!hwnd_ || !IsWindow(hwnd_))
        return;
    HWND host = GetParent(hwnd_);
    if (!host)
        return;
    RECT hostRect{};
    GetClientRect(host, &hostRect);
    if (hostRect.right > 0 && hostRect.bottom > 0)
        MoveWindow(hwnd_, 0, 0, hostRect.right, hostRect.bottom, TRUE);
}

void CanvasBackdrop::OnCameraChanged(const Camera& camera)
{
    cam_ = camera;
    if (hwnd_ && IsWindow(hwnd_)) {
        // WM_PAINT self-coalesces in the message queue, so invalidating on
        // every camera change cannot flood the loop.
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CanvasBackdrop::EnsureGridTile(int tilePx)
{
    if (gridTile_ && gridTilePx_ == tilePx)
        return;
    if (gridTile_)
        DeleteObject(gridTile_);
    gridTilePx_ = tilePx;

    HDC screen = GetDC(nullptr);
    gridTile_ = CreateCompatibleBitmap(screen, tilePx, tilePx);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    HGDIOBJ old = SelectObject(dc, gridTile_);
    RECT all{0, 0, tilePx, tilePx};
    HBRUSH bg = CreateSolidBrush(cfg_->backdropColor);
    FillRect(dc, &all, bg);
    DeleteObject(bg);

    HBRUSH dot = CreateSolidBrush(cfg_->backdropGridColor);
    RECT dotRect{0, 0, 2, 2};
    FillRect(dc, &dotRect, dot);
    DeleteObject(dot);

    SelectObject(dc, old);
    DeleteDC(dc);
}

void CanvasBackdrop::Paint(HDC dc, const RECT& client)
{
    if (!cfg_->backdropShowGrid) {
        HBRUSH bg = CreateSolidBrush(cfg_->backdropColor);
        FillRect(dc, &client, bg);
        DeleteObject(bg);
        return;
    }

    // Level-of-detail: keep the on-screen grid step in a readable range so
    // deep zoom-out never produces a dot storm.
    double step = (double)cfg_->backdropGridStep;
    double stepPx = step * cam_.scale;
    while (stepPx < 32.0)
        stepPx *= 2.0;
    while (stepPx > 256.0)
        stepPx /= 2.0;
    int tilePx = std::max(8, (int)std::lround(stepPx));
    EnsureGridTile(tilePx);

    // One FillRect with a pattern brush paints the whole grid; the brush
    // origin carries the camera offset so the dots track the canvas.
    HBRUSH pattern = CreatePatternBrush(gridTile_);
    int originX = -(int)std::lround(cam_.x * cam_.scale) % tilePx;
    int originY = -(int)std::lround(cam_.y * cam_.scale) % tilePx;
    SetBrushOrgEx(dc, originX, originY, nullptr);
    FillRect(dc, &client, pattern);
    DeleteObject(pattern);
}

LRESULT CALLBACK CanvasBackdrop::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    CanvasBackdrop* self = s_instance;
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
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
