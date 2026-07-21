#pragma once

#include "Common.h"

#include <vector>

class Config;

// A managed top-level window and its rectangle on the virtual canvas.
// Screen rect = (virtual rect - camera offset) * camera scale; see Common.h.
struct TrackedWindow {
    HWND hwnd = nullptr;
    double vx = 0, vy = 0; // virtual top-left
    double vw = 0, vh = 0; // virtual size
    POINT appliedPos{0, 0}; // last screen position we requested
    SIZE appliedSize{0, 0}; // last screen size we requested (or observed)
    SIZE minSize{0, 0};     // learned minimum: starts at SM_C{X,Y}MINTRACK and
                            // grows when a window refuses to shrink further
    bool movable = true;    // false for elevated (UIPI-protected) windows
    bool resizable = true;  // WS_THICKFRAME and not maximized: zoom resizes it
    std::wstring title;
};

// Owns the list of managed windows, their canvas coordinates and the batched
// movement. The camera itself is owned by App and passed in.
class WindowTracker {
public:
    void Init(const Config* cfg, DWORD ownPid);

    // Full re-enumeration. For windows we already track, the virtual rect is
    // kept unless the actual screen rect differs from what we last applied
    // (user moves/resizes are absorbed; our own rounding is not re-quantized).
    void Refresh(const Camera& camera);

    // Re-sync a single window after the user moved/resized it.
    // Returns false if the window is not currently tracked.
    bool ResyncWindow(HWND hwnd, const Camera& camera);

    // Move (and, when scale requires it, resize) every movable window in one
    // DeferWindowPos batch; falls back to individual SetWindowPos on failure.
    void ApplyCamera(const Camera& camera);

    const std::vector<TrackedWindow>& Windows() const { return windows_; }
    const TrackedWindow* Find(HWND hwnd) const;

    // True once we've met a window we cannot move (elevated process etc.).
    bool ElevatedSeen() const { return elevatedSeen_; }

private:
    const Config* cfg_ = nullptr;
    DWORD ownPid_ = 0;
    DWORD ownIntegrity_ = (DWORD)-1;
    SIZE systemMinTrack_{0, 0};
    std::vector<TrackedWindow> windows_; // z-order, topmost first
    bool elevatedSeen_ = false;

    void AdoptRect(TrackedWindow& entry, const RECT& rect, const Camera& camera) const;
    bool IsManageable(HWND hwnd) const;
    bool IsFullscreen(HWND hwnd, LONG_PTR style, const RECT& rect) const;
    bool ProbeMovable(HWND hwnd);
    static bool IsResizableWindow(HWND hwnd);

    static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam);
};
