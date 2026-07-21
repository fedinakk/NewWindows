#pragma once

#include "Common.h"

#include <shellapi.h>

// System tray icon with balloon notifications. The context menu is built and
// handled by App (WM_APP_TRAY callback).
class TrayIcon {
public:
    void Create(HWND hwnd, UINT callbackMsg, const wchar_t* tooltip);
    void Destroy();
    void SetTooltip(const wchar_t* tooltip);
    void Balloon(const wchar_t* title, const wchar_t* text);

private:
    NOTIFYICONDATAW nid_{};
    bool added_ = false;
    HICON icon_ = nullptr;

    static HICON MakeIcon();
};
