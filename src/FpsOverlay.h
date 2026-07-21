#pragma once

#include "Common.h"

#include <functional>

// Snapshot of the render stats shown by the overlay.
struct FrameStats {
    double fps = 0;      // camera updates per second (0 while idle)
    double frameMs = 0;  // time between the two most recent updates
    double scale = 1.0;  // current camera scale
    int windows = 0;     // managed window count
};

// Small click-through debug overlay in the top-right corner showing the
// actual canvas update rate (Ctrl+Alt+F / [Debug] ShowFps).
class FpsOverlay {
public:
    using StatsFn = std::function<FrameStats()>;

    void Init(HINSTANCE instance, StatsFn provider);
    void Toggle();
    void Show();
    void Hide();
    bool IsVisible() const;
    void Destroy();

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    StatsFn provider_;

    static FpsOverlay* s_instance;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void EnsureCreated();
    void Reposition();
    void Paint(HDC dc, const RECT& client);
};
