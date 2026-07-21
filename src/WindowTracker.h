#pragma once

#include "Common.h"

#include <vector>

class Config;

// A managed top-level window and its position on the virtual canvas.
// Screen position = virtual position - camera offset.
struct TrackedWindow {
    HWND hwnd = nullptr;
    POINT virt{0, 0};    // top-left in canvas coordinates
    SIZE size{0, 0};     // last known size (used by the overview)
    POINT applied{0, 0}; // last screen position we know of / applied
    bool movable = true; // false for elevated (UIPI-protected) windows
    std::wstring title;
};

// Owns the list of managed windows, their canvas coordinates and the batched
// movement. The camera itself is owned by App and passed in.
class WindowTracker {
public:
    void Init(const Config* cfg, DWORD ownPid);

    // Full re-enumeration. Existing windows are re-synced from their actual
    // screen position (screen + camera becomes the authoritative virtual
    // position), so user drags and missed moves are absorbed here.
    void Refresh(POINT camera);

    // Re-sync a single window after the user moved it. Returns false if the
    // window is not currently tracked.
    bool ResyncWindow(HWND hwnd, POINT camera);

    // Move every movable window to (virtual - camera) in one DeferWindowPos
    // batch; falls back to individual SetWindowPos if the batch fails.
    void ApplyCamera(POINT camera);

    const std::vector<TrackedWindow>& Windows() const { return windows_; }
    const TrackedWindow* Find(HWND hwnd) const;

    // True once we've met a window we cannot move (elevated process etc.).
    bool ElevatedSeen() const { return elevatedSeen_; }

private:
    const Config* cfg_ = nullptr;
    DWORD ownPid_ = 0;
    DWORD ownIntegrity_ = (DWORD)-1;
    std::vector<TrackedWindow> windows_; // z-order, topmost first
    bool elevatedSeen_ = false;

    bool IsManageable(HWND hwnd) const;
    bool IsFullscreen(HWND hwnd, LONG_PTR style, const RECT& rect) const;
    bool ProbeMovable(HWND hwnd);

    static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam);
};
