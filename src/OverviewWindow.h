#pragma once

#include "Common.h"

#include <functional>
#include <vector>

// One window rectangle on the overview map (canvas coordinates).
struct OverviewItem {
    HWND hwnd = nullptr;
    std::wstring title;
    RECT virt{0, 0, 0, 0};
    bool movable = true;
};

// Semi-transparent fullscreen overlay: a minimap of the whole canvas.
// Clicking a rectangle jumps the camera to that window ("zoom out and see
// everything" without actually scaling live windows, which Win32 cannot do).
class OverviewWindow {
public:
    using PickFn = std::function<void(HWND)>;

    void Show(HINSTANCE instance, std::vector<OverviewItem> items, RECT viewportVirt,
              int alpha, PickFn onPick);
    void Hide();
    bool IsVisible() const;
    void Destroy();

private:
    HWND hwnd_ = nullptr;
    std::vector<OverviewItem> items_; // z-order, topmost first
    RECT viewport_{0, 0, 0, 0};
    PickFn pick_;
    int hover_ = -1;

    // World (canvas) -> overlay client mapping.
    double scale_ = 1.0;
    double offsetX_ = 0.0;
    double offsetY_ = 0.0;

    static OverviewWindow* s_instance;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void EnsureCreated(HINSTANCE instance);
    void ComputeMapping();
    RECT MapRect(const RECT& world) const;
    int HitTest(POINT client) const;
    void Paint(HDC dc, const RECT& client);
};
