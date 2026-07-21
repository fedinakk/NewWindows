#pragma once

#include "CanvasBackdrop.h"
#include "Common.h"
#include "Config.h"
#include "FpsOverlay.h"
#include "OverviewWindow.h"
#include "PanController.h"
#include "TrayIcon.h"
#include "WindowTracker.h"

// Application core: owns the hidden main window, the camera state machine
// (panning / inertia / flight), zoom, hotkeys, tray, backdrop and the
// high-resolution frame loop.
class App {
public:
    int Run(HINSTANCE instance);

private:
    enum class CamMode { Idle, Panning, Inertia, Flying };

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    Config cfg_;
    WindowTracker tracker_;
    PanController pan_;
    TrayIcon tray_;
    OverviewWindow overview_;
    FpsOverlay fps_;
    CanvasBackdrop backdrop_;

    // Camera: (0,0, scale 1) = the arrangement at startup ("home").
    Camera cam_;
    CamMode mode_ = CamMode::Idle;

    // Panning anchors: math always runs from fixed points, so no drift.
    Camera panStartCam_;
    POINT panStartCursor_{0, 0};

    // Inertia velocity, virtual px/s.
    double velX_ = 0, velY_ = 0;

    // Camera flight (home / bookmarks / overview jump).
    Camera flyFrom_, flyTo_;
    long long flyStartQpc_ = 0;
    int flyDuration_ = 0;

    // High-resolution frame loop (animations only; panning is mouse-driven).
    HANDLE frameTimer_ = nullptr;
    bool frameTimerArmed_ = false;
    long long framePeriod100ns_ = 41667; // 240 fps
    long long qpcFreq_ = 1;
    long long lastAnimQpc_ = 0;

    // FPS statistics (every camera application counts as one frame).
    long long statWindowStartQpc_ = 0;
    int statFrames_ = 0;
    double statFps_ = 0;
    double statFrameMs_ = 0;
    long long lastApplyQpc_ = 0;

    bool paused_ = false;
    bool dirty_ = false;
    bool dirtyPosted_ = false;
    bool elevatedNotified_ = false;
    bool timePeriodRaised_ = false;
    ULONGLONG lastPanRefresh_ = 0;

    HWINEVENTHOOK eventHooks_[3] = {};

    static App* s_app;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD thread, DWORD time);

    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void Setup();
    void Teardown();
    void RegisterHotkeys();
    void InstallWinEventHooks();
    void MessageLoop();

    void OnPanBegin();
    void OnPanUpdate();
    void OnPanEnd();
    void OnZoom();
    void OnFrame();
    void OnHousekeep();
    void OnHotkey(int id);
    void OnTrayMessage(LPARAM event);
    void OnMenuCommand(int id);

    void SetCamera(const Camera& camera);
    void FlyTo(const Camera& target);
    void StartFrameTimer();
    void StopFrameTimer();
    void ArmFrameTimer();
    void RefreshTracker();
    void ShowOverview();
    void CenterOnWindow(HWND hwnd);
    void TogglePause();
    void ReloadConfig();

    long long NowQpc() const;
    double SecondsSince(long long qpc) const;
    void RecordFrame();
    FrameStats CurrentStats();
};
