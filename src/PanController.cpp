#include "PanController.h"

#include "Config.h"

#include <cmath>
#include <cstdlib>

PanController* PanController::s_instance = nullptr;

void PanController::Init(HWND notifyWnd, const Config* cfg)
{
    notify_ = notifyWnd;
    cfg_ = cfg;
    s_instance = this;
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    if (!hook_)
        Log(L"PanController: SetWindowsHookEx failed (error %lu)", GetLastError());
}

void PanController::Shutdown()
{
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    s_instance = nullptr;
}

bool PanController::CtrlAltDown()
{
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000);
}

LRESULT CALLBACK PanController::MouseProc(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && s_instance) {
        auto* info = (const MSLLHOOKSTRUCT*)lparam;
        if (s_instance->Handle(wparam, info))
            return 1; // swallow the event
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

bool PanController::Handle(WPARAM message, const MSLLHOOKSTRUCT* info)
{
    // While paused, ignore new gestures but let an active one finish cleanly.
    if (paused_ && state_ == State::Idle)
        return false;

    const POINT pt = info->pt;

    switch (message) {
    case WM_MOUSEWHEEL:
        // Ctrl+Alt+wheel belongs to the canvas globally: it must never reach
        // the focused application (so no browser Ctrl+wheel zoom etc.).
        if (!cfg_->zoomEnabled || !cfg_->captureWheel)
            return false;
        if (!CtrlAltDown())
            return false;
        wheelAccum_ += (short)HIWORD(info->mouseData);
        wheelPt_ = pt;
        if (!zoomPending_) {
            zoomPending_ = true;
            PostMessageW(notify_, WM_APP_ZOOM, 0, 0);
        }
        return true;

    case WM_MBUTTONDOWN:
        // Primary gesture: Ctrl+Alt + middle drag pans from anywhere, even
        // over a focused application window.
        if (cfg_->ctrlAltMiddle && CtrlAltDown())
            return TryStart(pt, Button::CtrlAltMiddle, false);
        if (cfg_->middleButton)
            return TryStart(pt, Button::Middle, true);
        return false;

    case WM_LBUTTONDOWN:
        if (!cfg_->altLeftButton)
            return false;
        if (!(GetAsyncKeyState(VK_MENU) & 0x8000))
            return false;
        return TryStart(pt, Button::AltLeft, true);

    case WM_MOUSEMOVE:
        if (state_ == State::Idle)
            return false;
        AddSample(pt);
        if (state_ == State::Pending) {
            int dx = std::abs(pt.x - startPt_.x);
            int dy = std::abs(pt.y - startPt_.y);
            if (dx >= cfg_->dragThresholdPx || dy >= cfg_->dragThresholdPx) {
                state_ = State::Panning;
                PostMessageW(notify_, WM_APP_PAN_BEGIN, 0, 0);
            }
        }
        if (state_ == State::Panning) {
            latest_ = pt;
            if (!updatePending_) {
                updatePending_ = true;
                PostMessageW(notify_, WM_APP_PAN_UPDATE, 0, 0);
            }
        }
        return false; // never swallow moves

    case WM_MBUTTONUP:
        if (button_ != Button::Middle && button_ != Button::CtrlAltMiddle)
            return false;
        return Finish(pt);

    case WM_LBUTTONUP:
        if (button_ != Button::AltLeft)
            return false;
        return Finish(pt);
    }

    return false;
}

bool PanController::TryStart(POINT pt, Button button, bool requireBackground)
{
    if (state_ != State::Idle) {
        // Recover from a missed button-up (the OS may skip a slow LL hook).
        if (state_ == State::Panning)
            PostMessageW(notify_, WM_APP_PAN_END, 0, 0);
        state_ = State::Idle;
    }

    if (requireBackground && !IsBackgroundAt(pt))
        return false;

    state_ = State::Pending;
    button_ = button;
    startPt_ = pt;
    latest_ = pt;
    updatePending_ = false;
    sampleCount_ = 0;
    AddSample(pt);
    // Swallow the button-down: with Ctrl+Alt it must not reach the focused
    // app; on the empty desktop it would only start a marquee selection.
    return true;
}

bool PanController::Finish(POINT pt)
{
    if (state_ == State::Idle)
        return false;

    bool wasPanning = (state_ == State::Panning);
    state_ = State::Idle;
    button_ = Button::None;

    if (wasPanning) {
        ComputeReleaseVelocity(pt);
        PostMessageW(notify_, WM_APP_PAN_END, 0, 0);
    }
    return true; // we swallowed the down, swallow the up too
}

bool PanController::IsBackgroundAt(POINT pt) const
{
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd)
        return true;

    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root)
        root = hwnd;
    if (root == GetDesktopWindow())
        return true;
    if (extraBackground_ && (hwnd == extraBackground_ || root == extraBackground_))
        return true;

    wchar_t cls[128] = L"";
    GetClassNameW(root, cls, 128);
    for (const auto& bg : cfg_->backgroundClasses)
        if (_wcsicmp(cls, bg.c_str()) == 0)
            return true;
    return false;
}

POINT PanController::ConsumeLatest()
{
    updatePending_ = false;
    return latest_;
}

void PanController::ConsumeZoom(int& wheelDelta, POINT& pt)
{
    wheelDelta = wheelAccum_;
    pt = wheelPt_;
    wheelAccum_ = 0;
    zoomPending_ = false;
}

void PanController::AddSample(POINT pt)
{
    if (sampleCount_ < kMaxSamples) {
        samples_[sampleCount_++] = Sample{pt, GetTickCount64()};
    } else {
        memmove(samples_, samples_ + 1, sizeof(Sample) * (kMaxSamples - 1));
        samples_[kMaxSamples - 1] = Sample{pt, GetTickCount64()};
    }
}

void PanController::ComputeReleaseVelocity(POINT releasePt)
{
    relVx_ = relVy_ = 0;
    if (sampleCount_ < 2)
        return;

    const ULONGLONG now = GetTickCount64();
    const Sample& newest = samples_[sampleCount_ - 1];

    // If the cursor rested before release, there is no fling.
    if (now - newest.t > 100)
        return;

    // Find a sample 50..200 ms back for a stable velocity estimate.
    const Sample* base = nullptr;
    for (int i = sampleCount_ - 2; i >= 0; --i) {
        ULONGLONG age = now - samples_[i].t;
        if (age >= 50) {
            if (age <= 200)
                base = &samples_[i];
            break;
        }
        base = &samples_[i];
    }
    if (!base)
        return;

    double dt = (double)(newest.t - base->t) / 1000.0;
    if (dt < 0.02)
        return;

    relVx_ = (releasePt.x - base->pt.x) / dt;
    relVy_ = (releasePt.y - base->pt.y) / dt;
}

void PanController::ReleaseVelocity(double& vx, double& vy) const
{
    vx = relVx_;
    vy = relVy_;
}
