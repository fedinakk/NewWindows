#pragma once

#include "Common.h"

class Config;

// The canvas backdrop: our own surface embedded into the desktop wallpaper
// layer (behind every app window AND behind the desktop icons), drawing a
// solid canvas with a dot grid that pans and zooms with the camera. This is
// what turns "windows sliding over a static wallpaper" into "moving across
// an infinite desktop".
//
// This is deliberately NOT a shell replacement: Explorer keeps running, the
// taskbar (Shell_TrayWnd / Shell_SecondaryTrayWnd) is never touched, and if
// the wallpaper host cannot be found the backdrop simply stays disabled.
class CanvasBackdrop {
public:
    void Create(HINSTANCE instance, const Config* cfg);
    void Destroy();

    // Re-attach if Explorer restarted and took our window with it.
    void EnsureAlive();

    // Resize to the wallpaper host after a display layout change.
    void Refit();

    void OnCameraChanged(const Camera& camera);
    HWND Handle() const { return hwnd_; }

private:
    HINSTANCE instance_ = nullptr;
    const Config* cfg_ = nullptr;
    HWND hwnd_ = nullptr;
    Camera cam_;

    HBITMAP gridTile_ = nullptr;
    int gridTilePx_ = 0; // side of the cached tile, screen px

    static CanvasBackdrop* s_instance;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void Paint(HDC dc, const RECT& client);
    void EnsureGridTile(int tilePx);

    // Finds (or provokes, via the Progman 0x052C message) the WorkerW layer
    // that sits between the wallpaper and the desktop icons. Returns the
    // window to parent our backdrop into, or nullptr if the shell layout is
    // unknown - in that case the backdrop is skipped entirely rather than
    // risking covering the desktop icons.
    static HWND FindWallpaperHost(HWND* insertAfterChild);
};
