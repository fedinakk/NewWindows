#pragma once

#include "Common.h"

class Config;

// Global low-level mouse hook and the pan gesture state machine.
//
// The hook runs on the main thread (LL hooks are dispatched through the
// installing thread's message loop), so no locking is needed, but the
// callback must stay fast: heavy work (moving windows) is deferred to the
// main window via posted, coalesced messages.
class PanController {
public:
    void Init(HWND notifyWnd, const Config* cfg);
    void Shutdown();

    void SetPaused(bool paused) { paused_ = paused; }
    bool IsPaused() const { return paused_; }
    bool IsPanning() const { return state_ == State::Panning; }

    // Cursor position where the gesture started (pan anchor).
    POINT StartPoint() const { return startPt_; }

    // Latest cursor position; clears the coalescing flag.
    POINT ConsumeLatest();

    // Cursor velocity at release, px/s (computed on button-up).
    void ReleaseVelocity(double& vx, double& vy) const;

private:
    enum class State { Idle, Pending, Panning };
    enum class Button { None, Middle, AltLeft };

    struct Sample {
        POINT pt;
        ULONGLONG t;
    };

    HWND notify_ = nullptr;
    const Config* cfg_ = nullptr;
    HHOOK hook_ = nullptr;
    bool paused_ = false;

    State state_ = State::Idle;
    Button button_ = Button::None;
    POINT startPt_{0, 0};
    POINT latest_{0, 0};
    bool updatePending_ = false;

    static const int kMaxSamples = 32;
    Sample samples_[kMaxSamples];
    int sampleCount_ = 0;
    double relVx_ = 0, relVy_ = 0;

    static PanController* s_instance;
    static LRESULT CALLBACK MouseProc(int code, WPARAM wparam, LPARAM lparam);

    // Returns true if the event must be swallowed.
    bool Handle(WPARAM message, const MSLLHOOKSTRUCT* info);
    bool TryStart(POINT pt, Button button);
    bool Finish(POINT pt);
    bool IsBackgroundAt(POINT pt) const;
    void AddSample(POINT pt);
    void ComputeReleaseVelocity(POINT releasePt);
};
