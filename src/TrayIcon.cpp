#include "TrayIcon.h"

#include <cmath>
#include <cstring>

// Draws the tray icon at runtime (a blue disc with a white pan cross), so the
// project needs no binary .ico resource.
HICON TrayIcon::MakeIcon()
{
    const int S = 32;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (!color)
        return LoadIconW(nullptr, IDI_APPLICATION);

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBmp = SelectObject(dc, color);
    memset(bits, 0, S * S * 4);

    HBRUSH disc = CreateSolidBrush(RGB(61, 109, 242));
    HGDIOBJ oldBrush = SelectObject(dc, disc);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, 1, 1, S - 1, S - 1);

    HPEN white = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(dc, white);
    MoveToEx(dc, S / 2, 9, nullptr);
    LineTo(dc, S / 2, S - 9);
    MoveToEx(dc, 9, S / 2, nullptr);
    LineTo(dc, S - 9, S / 2);

    HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, whiteBrush);
    SelectObject(dc, GetStockObject(NULL_PEN));
    const POINT up[] = {{S / 2 - 4, 10}, {S / 2 + 4, 10}, {S / 2, 5}};
    const POINT down[] = {{S / 2 - 4, S - 10}, {S / 2 + 4, S - 10}, {S / 2, S - 5}};
    const POINT left[] = {{10, S / 2 - 4}, {10, S / 2 + 4}, {5, S / 2}};
    const POINT right[] = {{S - 10, S / 2 - 4}, {S - 10, S / 2 + 4}, {S - 5, S / 2}};
    Polygon(dc, up, 3);
    Polygon(dc, down, 3);
    Polygon(dc, left, 3);
    Polygon(dc, right, 3);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(white);
    DeleteObject(whiteBrush);
    DeleteObject(disc);
    GdiFlush();

    // GDI leaves alpha at 0; make every painted pixel opaque.
    auto* px = (DWORD*)bits;
    for (int i = 0; i < S * S; ++i)
        if (px[i] & 0x00FFFFFF)
            px[i] |= 0xFF000000;

    SelectObject(dc, oldBmp);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(mask);
    DeleteObject(color);
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void TrayIcon::Create(HWND hwnd, UINT callbackMsg, const wchar_t* tooltip)
{
    icon_ = MakeIcon();

    nid_ = NOTIFYICONDATAW{};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = callbackMsg;
    nid_.hIcon = icon_;
    wcsncpy_s(nid_.szTip, tooltip, _TRUNCATE);

    added_ = !!Shell_NotifyIconW(NIM_ADD, &nid_);
    if (!added_)
        Log(L"TrayIcon: NIM_ADD failed");
}

void TrayIcon::Destroy()
{
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
    }
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

void TrayIcon::SetTooltip(const wchar_t* tooltip)
{
    if (!added_)
        return;
    nid_.uFlags = NIF_TIP;
    wcsncpy_s(nid_.szTip, tooltip, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

void TrayIcon::Balloon(const wchar_t* title, const wchar_t* text)
{
    if (!added_)
        return;
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid_.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid_.szInfo, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.szInfo[0] = L'\0';
    nid_.szInfoTitle[0] = L'\0';
}
