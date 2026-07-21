#pragma once

#include "Common.h"
#include "Config.h"
#include "OverviewWindow.h"
#include "PanController.h"
#include "TrayIcon.h"
#include "WindowTracker.h"

// Application core: owns the hidden main window, the camera state machine
// (panning / inertia / flight), hotkeys, tray and the housekeeping timers.
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

    POINT camera_{0, 0}; // (0,0) = where windows were at startup ("home")
    CamMode mode_ = CamMode::Idle;

    // Panning anchors (integer math from fixed points, so no drift).
    POINT panStartCamera_{0, 0};
    POINT panStartCursor_{0, 0};

    // Inertia state (float accumulator, px and px/s).
    double inertiaX_ = 0, inertiaY_ = 0;
    double velX_ = 0, velY_ = 0;
    ULONGLONG lastTick_ = 0;

    // Camera flight (home / bookmarks / overview jump).
    POINT flyFrom_{0, 0}, flyTo_{0, 0};
    ULONGLONG flyStart_ = 0;
    int flyDuration_ = 0;

    bool paused_ = false;
    bool dirty_ = false;
    bool dirtyPosted_ = false;
    bool tickTimerOn_ = false;
    bool elevatedNotified_ = false;
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

    void OnPanBegin();
    void OnPanUpdate();
    void OnPanEnd();
    void OnTick();
    void OnHousekeep();
    void OnHotkey(int id);
    void OnTrayMessage(LPARAM event);
    void OnMenuCommand(int id);

    void SetCamera(POINT camera);
    void FlyTo(POINT target);
    void StartTickTimer();
    void StopTickTimer();
    void RefreshTracker();
    void ShowOverview();
    void CenterOnWindow(HWND hwnd);
    void TogglePause();
    void ReloadConfig();
};
