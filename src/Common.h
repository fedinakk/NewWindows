#pragma once

// Common definitions shared by all InfiniteCanvas modules.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10+
#endif

#include <windows.h>

#include <cmath>
#include <string>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#endif

// The camera: offset + scale. All coordinate conversions in the app go
// through these two helpers so offset and scale can never get out of sync.
//   screen = (virtual - offset) * scale
//   virtual = screen / scale + offset
// Coordinates are physical pixels (the process is per-monitor DPI aware),
// so no DPI factor may ever appear in this transform.
struct Camera {
    double x = 0.0; // virtual coordinate shown at screen (0,0)
    double y = 0.0;
    double scale = 1.0;

    bool operator==(const Camera& o) const { return x == o.x && y == o.y && scale == o.scale; }
    bool operator!=(const Camera& o) const { return !(*this == o); }
};

inline POINT VirtualToScreen(const Camera& c, double vx, double vy)
{
    return POINT{(LONG)std::lround((vx - c.x) * c.scale), (LONG)std::lround((vy - c.y) * c.scale)};
}

inline void ScreenToVirtual(const Camera& c, POINT s, double& vx, double& vy)
{
    vx = s.x / c.scale + c.x;
    vy = s.y / c.scale + c.y;
}

// Application-private window messages (all delivered to the hidden main window).
enum : UINT {
    WM_APP_PAN_BEGIN    = WM_APP + 1, // drag threshold exceeded, panning starts
    WM_APP_PAN_UPDATE   = WM_APP + 2, // coalesced cursor update during panning
    WM_APP_PAN_END      = WM_APP + 3, // pan button released
    WM_APP_TRAY         = WM_APP + 4, // tray icon callback
    WM_APP_DIRTY        = WM_APP + 5, // window list may have changed (coalesced)
    WM_APP_MOVESIZEEND  = WM_APP + 6, // user finished moving a window; lParam = HWND
    WM_APP_ZOOM         = WM_APP + 7, // coalesced Ctrl+Alt+wheel zoom steps
};

// Timers on the main window. Animation frames do NOT use SetTimer (its
// ~15.6 ms floor caps motion at ~66 Hz); they run off a high-resolution
// waitable timer in the message loop instead.
enum : UINT_PTR {
    IDT_HOUSEKEEP = 2, // 2 s: refresh window list while idle
};

// Global hotkey identifiers.
enum : int {
    HK_HOME        = 1,
    HK_OVERVIEW    = 2,
    HK_PAUSE       = 3,
    HK_EXIT        = 4,
    HK_FPS         = 5,
    HK_BM_GO_FIRST = 10, // 10..13 -> Ctrl+Alt+1..4
    HK_BM_SET_FIRST = 20, // 20..23 -> Ctrl+Alt+Shift+1..4
};

// Tray menu command identifiers.
enum : int {
    IDM_OVERVIEW      = 100,
    IDM_HOME          = 101,
    IDM_PAUSE         = 102,
    IDM_OPEN_CONFIG   = 103,
    IDM_RELOAD_CONFIG = 104,
    IDM_EXIT          = 110,
};

// Logging (OutputDebugString always; optional file, see [Debug] LogToFile).
void LogInit(const std::wstring& filePath, bool toFile);
void Log(const wchar_t* fmt, ...);

// Directory containing the executable, without trailing backslash.
std::wstring ExeDir();

// Best-effort per-monitor-v2 DPI awareness (falls back on older systems).
void EnablePerMonitorDpi();

// GetDpiForWindow with a 96 fallback for pre-1607 systems.
UINT DpiForWindow(HWND hwnd);
