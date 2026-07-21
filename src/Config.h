#pragma once

#include "Common.h"

#include <vector>

// Camera bookmark (Ctrl+Alt+Shift+1..4 to save, Ctrl+Alt+1..4 to jump).
struct Bookmark {
    bool set = false;
    Camera cam;
};

// Settings loaded from config.ini next to the executable.
// The file is created with commented defaults on first run.
class Config {
public:
    // [Pan]
    bool ctrlAltMiddle = true;  // primary: Ctrl+Alt + middle drag, anywhere
    bool middleButton = true;   // legacy: middle drag on the desktop background
    bool altLeftButton = true;  // legacy: Alt+LMB drag on the desktop background
    double sensitivity = 1.0;
    int dragThresholdPx = 4;

    // [Zoom]
    bool zoomEnabled = true;
    bool captureWheel = true;   // swallow Ctrl+Alt+wheel globally
    double zoomStep = 1.1;      // scale multiplier per wheel notch
    double zoomMin = 0.25;
    double zoomMax = 2.5;

    // [Render]
    int targetFps = 240;        // animation loop target, clamped to [30, 240]

    // [Inertia]
    bool inertiaEnabled = true;
    double inertiaFriction = 5.0;    // exponential decay, 1/s
    double inertiaMinVelocity = 250; // px/s at release to start coasting

    // [Camera]
    int flightMs = 180; // smooth camera flight duration; 0 = instant

    // [Filter]
    bool skipUntitled = true;
    bool skipFullscreen = true;
    bool skipMaximized = false;
    std::vector<std::wstring> excludeClasses;
    std::vector<std::wstring> excludeTitles;     // substrings, case-insensitive
    std::vector<std::wstring> backgroundClasses; // desktop background window classes

    // [Overview]
    bool overviewEnabled = true;
    int overviewAlpha = 235;

    // [Backdrop]
    bool backdropEnabled = true;
    COLORREF backdropColor = RGB(0x14, 0x16, 0x1C);
    COLORREF backdropGridColor = RGB(0x2A, 0x2E, 0x3A);
    int backdropGridStep = 96; // virtual pixels between grid dots
    bool backdropShowGrid = true;

    // [Hotkeys]
    bool enableBookmarks = true;

    // [Debug]
    bool showFps = false;
    bool logToFile = false;

    Bookmark bookmarks[4];

    // Loads config.ini (creating it with defaults first if missing).
    void Load();
    void SaveBookmark(int index);

    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;

    void EnsureFileExists();
    std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* def) const;
    bool ReadBool(const wchar_t* section, const wchar_t* key, bool def) const;
    int ReadInt(const wchar_t* section, const wchar_t* key, int def) const;
    double ReadDouble(const wchar_t* section, const wchar_t* key, double def) const;
    COLORREF ReadColor(const wchar_t* section, const wchar_t* key, COLORREF def) const;
    std::vector<std::wstring> ReadList(const wchar_t* section, const wchar_t* key, const wchar_t* def) const;
};
