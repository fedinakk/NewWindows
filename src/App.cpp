#include "App.h"

#include <timeapi.h>

#include <algorithm>
#include <cmath>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace {

const wchar_t kMainClass[] = L"InfiniteCanvasMain";
const wchar_t kTooltip[] = L"InfiniteCanvas — бесконечный холст";
const wchar_t kTooltipPaused[] = L"InfiniteCanvas — пауза";

double EaseOutCubic(double t)
{
    double inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

} // namespace

App* App::s_app = nullptr;

int App::Run(HINSTANCE instance)
{
    instance_ = instance;
    s_app = this;

    cfg_.Load();
    LogInit(ExeDir() + L"\\infinitecanvas.log", cfg_.logToFile);
    Log(L"Starting");

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpcFreq_ = freq.QuadPart;

    tracker_.Init(&cfg_, GetCurrentProcessId());

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kMainClass;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(0, kMainClass, L"InfiniteCanvas", WS_POPUP,
                            0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!hwnd_) {
        Log(L"CreateWindowEx failed (error %lu)", GetLastError());
        return 1;
    }

    Setup();
    MessageLoop();

    s_app = nullptr;
    return 0;
}

// Message pump + high-resolution frame timer in one loop. SetTimer cannot go
// below ~15.6 ms (~66 Hz), so animation frames are driven by a waitable
// timer the loop waits on alongside the input queue.
void App::MessageLoop()
{
    for (;;) {
        DWORD handleCount = (frameTimer_ && frameTimerArmed_) ? 1 : 0;
        DWORD wait = MsgWaitForMultipleObjectsEx(handleCount, &frameTimer_, INFINITE,
                                                 QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (handleCount && wait == WAIT_OBJECT_0) {
            OnFrame();
            if (frameTimerArmed_)
                ArmFrameTimer();
        }

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

void App::Setup()
{
    // 1 ms system timer resolution for the whole session; paired with
    // timeEndPeriod in Teardown.
    if (timeBeginPeriod(1) == TIMERR_NOERROR)
        timePeriodRaised_ = true;

    frameTimer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!frameTimer_) // pre-1803 Windows 10: plain waitable timer
        frameTimer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    framePeriod100ns_ = 10000000ll / std::max(30, std::min(240, cfg_.targetFps));

    pan_.Init(hwnd_, &cfg_);
    tray_.Create(hwnd_, WM_APP_TRAY, kTooltip);
    RegisterHotkeys();
    InstallWinEventHooks();
    SetTimer(hwnd_, IDT_HOUSEKEEP, 2000, nullptr);

    fps_.Init(instance_, [this]() { return CurrentStats(); });
    if (cfg_.showFps)
        fps_.Show();

    backdrop_.Create(instance_, &cfg_);
    pan_.SetExtraBackground(backdrop_.Handle());
    backdrop_.OnCameraChanged(cam_);

    // Adopt the initial layout: current screen positions become the canvas
    // origin, so Ctrl+Alt+Home always returns to this arrangement.
    RefreshTracker();
}

void App::Teardown()
{
    KillTimer(hwnd_, IDT_HOUSEKEEP);
    StopFrameTimer();
    if (frameTimer_) {
        CloseHandle(frameTimer_);
        frameTimer_ = nullptr;
    }
    for (auto& hook : eventHooks_) {
        if (hook) {
            UnhookWinEvent(hook);
            hook = nullptr;
        }
    }
    for (int id : {HK_HOME, HK_OVERVIEW, HK_PAUSE, HK_EXIT, HK_FPS})
        UnregisterHotKey(hwnd_, id);
    for (int i = 0; i < 4; ++i) {
        UnregisterHotKey(hwnd_, HK_BM_GO_FIRST + i);
        UnregisterHotKey(hwnd_, HK_BM_SET_FIRST + i);
    }
    backdrop_.Destroy();
    fps_.Destroy();
    overview_.Destroy();
    tray_.Destroy();
    pan_.Shutdown();
    if (timePeriodRaised_) {
        timeEndPeriod(1);
        timePeriodRaised_ = false;
    }
    Log(L"Stopped");
}

void App::RegisterHotkeys()
{
    struct HotkeyDef {
        int id;
        UINT modifiers;
        UINT vk;
        const wchar_t* name;
    };
    const UINT base = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    std::vector<HotkeyDef> keys = {
        {HK_HOME, base, VK_HOME, L"Ctrl+Alt+Home"},
        {HK_OVERVIEW, base, 'O', L"Ctrl+Alt+O"},
        {HK_PAUSE, base, 'P', L"Ctrl+Alt+P"},
        {HK_FPS, base, 'F', L"Ctrl+Alt+F"},
        {HK_EXIT, base | MOD_SHIFT, 'Q', L"Ctrl+Alt+Shift+Q"},
    };
    if (cfg_.enableBookmarks) {
        for (int i = 0; i < 4; ++i) {
            keys.push_back({HK_BM_GO_FIRST + i, base, (UINT)('1' + i), L"Ctrl+Alt+digit"});
            keys.push_back({HK_BM_SET_FIRST + i, base | MOD_SHIFT, (UINT)('1' + i), L"Ctrl+Alt+Shift+digit"});
        }
    }
    for (const auto& key : keys) {
        if (!RegisterHotKey(hwnd_, key.id, key.modifiers, key.vk))
            Log(L"RegisterHotKey %s failed (error %lu) — занята другим приложением?",
                key.name, GetLastError());
    }
}

void App::InstallWinEventHooks()
{
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    // User finished dragging/resizing a window: re-sync its canvas position.
    eventHooks_[0] = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND,
                                     nullptr, WinEventProc, 0, 0, flags);
    // Minimize / restore.
    eventHooks_[1] = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
                                     nullptr, WinEventProc, 0, 0, flags);
    // Windows appearing and disappearing.
    eventHooks_[2] = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE,
                                     nullptr, WinEventProc, 0, 0, flags);
}

void CALLBACK App::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                LONG idObject, LONG idChild, DWORD, DWORD)
{
    App* self = s_app;
    if (!self || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        return;

    if (event == EVENT_SYSTEM_MOVESIZEEND) {
        PostMessageW(self->hwnd_, WM_APP_MOVESIZEEND, 0, (LPARAM)hwnd);
        return;
    }
    if (!self->dirtyPosted_) {
        self->dirtyPosted_ = true;
        PostMessageW(self->hwnd_, WM_APP_DIRTY, 0, 0);
    }
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (s_app)
        return s_app->Handle(hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT App::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_APP_PAN_BEGIN:
        OnPanBegin();
        return 0;
    case WM_APP_PAN_UPDATE:
        OnPanUpdate();
        return 0;
    case WM_APP_PAN_END:
        OnPanEnd();
        return 0;
    case WM_APP_ZOOM:
        OnZoom();
        return 0;

    case WM_APP_DIRTY:
        dirtyPosted_ = false;
        dirty_ = true;
        // During a pan, adopt new windows right away so they join the canvas
        // instead of being left behind. The 200 ms limit throttles only this
        // re-enumeration - the movement itself is applied per mouse event.
        if (mode_ == CamMode::Panning && GetTickCount64() - lastPanRefresh_ > 200) {
            lastPanRefresh_ = GetTickCount64();
            RefreshTracker();
        }
        return 0;

    case WM_APP_MOVESIZEEND:
        if (!tracker_.ResyncWindow((HWND)lparam, cam_))
            dirty_ = true; // unknown window: pick it up on the next refresh
        return 0;

    case WM_HOTKEY:
        OnHotkey((int)wparam);
        return 0;

    case WM_TIMER:
        if (wparam == IDT_HOUSEKEEP)
            OnHousekeep();
        return 0;

    case WM_APP_TRAY:
        OnTrayMessage(lparam);
        return 0;

    case WM_DISPLAYCHANGE:
        backdrop_.Refit();
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wparam)
            Teardown();
        return 0;

    case WM_DESTROY:
        Teardown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------- panning --

void App::OnPanBegin()
{
    overview_.Hide();
    StopFrameTimer();
    mode_ = CamMode::Panning;
    lastPanRefresh_ = GetTickCount64();
    RefreshTracker(); // authoritative re-sync right before the gesture
    panStartCam_ = cam_;
    panStartCursor_ = pan_.StartPoint();
}

void App::OnPanUpdate()
{
    POINT cursor = pan_.ConsumeLatest();
    if (mode_ != CamMode::Panning)
        return;
    // Screen-space cursor delta converted to virtual units via the scale
    // captured at the anchor (re-anchored if the user zooms mid-pan).
    const double k = cfg_.sensitivity / panStartCam_.scale;
    Camera cam = panStartCam_;
    cam.x = panStartCam_.x - (cursor.x - panStartCursor_.x) * k;
    cam.y = panStartCam_.y - (cursor.y - panStartCursor_.y) * k;
    SetCamera(cam);
}

void App::OnPanEnd()
{
    if (mode_ != CamMode::Panning)
        return;
    mode_ = CamMode::Idle;

    double vx = 0, vy = 0;
    pan_.ReleaseVelocity(vx, vy);
    const double speed = std::hypot(vx, vy);
    if (!cfg_.inertiaEnabled || !frameTimer_ || speed < cfg_.inertiaMinVelocity)
        return;

    mode_ = CamMode::Inertia;
    // Cursor velocity is screen px/s; camera coasts in virtual units.
    velX_ = -vx * cfg_.sensitivity / cam_.scale;
    velY_ = -vy * cfg_.sensitivity / cam_.scale;
    lastAnimQpc_ = NowQpc();
    StartFrameTimer();
}

// ------------------------------------------------------------------- zoom --

void App::OnZoom()
{
    int wheelDelta = 0;
    POINT anchor{};
    pan_.ConsumeZoom(wheelDelta, anchor);
    if (!cfg_.zoomEnabled || wheelDelta == 0)
        return;
    if (overview_.IsVisible())
        return;

    const double oldScale = cam_.scale;
    const double steps = (double)wheelDelta / WHEEL_DELTA;
    double newScale = oldScale * std::pow(cfg_.zoomStep, steps);
    newScale = std::max(cfg_.zoomMin, std::min(cfg_.zoomMax, newScale));
    if (newScale == oldScale)
        return;

    // Zoom to the cursor: the virtual point under it must stay under it.
    double vx = 0, vy = 0;
    ScreenToVirtual(cam_, anchor, vx, vy);
    Camera cam;
    cam.scale = newScale;
    cam.x = vx - anchor.x / newScale;
    cam.y = vy - anchor.y / newScale;

    if (mode_ == CamMode::Flying) {
        StopFrameTimer();
        mode_ = CamMode::Idle;
    }
    SetCamera(cam);

    if (mode_ == CamMode::Panning) {
        // Re-anchor the active pan so the gesture continues seamlessly.
        panStartCam_ = cam_;
        panStartCursor_ = anchor;
    } else if (mode_ == CamMode::Inertia) {
        // Keep the on-screen coasting speed constant across the zoom.
        velX_ *= oldScale / newScale;
        velY_ *= oldScale / newScale;
    }
}

// ------------------------------------------------------------ frame timer --

void App::OnFrame()
{
    const long long now = NowQpc();

    if (mode_ == CamMode::Inertia) {
        double dt = (double)(now - lastAnimQpc_) / qpcFreq_;
        lastAnimQpc_ = now;
        if (dt <= 0)
            return;
        dt = std::min(dt, 0.1); // a stalled loop must not teleport the canvas

        Camera cam = cam_;
        cam.x += velX_ * dt;
        cam.y += velY_ * dt;
        const double decay = std::exp(-cfg_.inertiaFriction * dt);
        velX_ *= decay;
        velY_ *= decay;
        SetCamera(cam);

        // Stop when the on-screen speed becomes imperceptible.
        if (std::hypot(velX_, velY_) * cam_.scale < 60.0) {
            mode_ = CamMode::Idle;
            StopFrameTimer();
        }
        return;
    }

    if (mode_ == CamMode::Flying) {
        double t = flyDuration_ > 0 ? SecondsSince(flyStartQpc_) * 1000.0 / flyDuration_ : 1.0;
        t = std::min(1.0, std::max(0.0, t));
        const double e = EaseOutCubic(t);
        Camera cam;
        cam.x = flyFrom_.x + (flyTo_.x - flyFrom_.x) * e;
        cam.y = flyFrom_.y + (flyTo_.y - flyFrom_.y) * e;
        cam.scale = flyFrom_.scale + (flyTo_.scale - flyFrom_.scale) * e;
        SetCamera(cam);
        if (t >= 1.0) {
            SetCamera(flyTo_);
            mode_ = CamMode::Idle;
            StopFrameTimer();
        }
        return;
    }

    StopFrameTimer();
}

void App::StartFrameTimer()
{
    if (!frameTimer_)
        return;
    if (!frameTimerArmed_) {
        frameTimerArmed_ = true;
        ArmFrameTimer();
    }
}

void App::ArmFrameTimer()
{
    LARGE_INTEGER due;
    due.QuadPart = -framePeriod100ns_;
    SetWaitableTimer(frameTimer_, &due, 0, nullptr, nullptr, FALSE);
}

void App::StopFrameTimer()
{
    if (frameTimerArmed_) {
        frameTimerArmed_ = false;
        if (frameTimer_)
            CancelWaitableTimer(frameTimer_);
    }
}

void App::OnHousekeep()
{
    // Safety net: if the hook lost its button-up, do not stay stuck panning.
    if (mode_ == CamMode::Panning && !pan_.IsPanning())
        mode_ = CamMode::Idle;
    if (mode_ != CamMode::Idle)
        return;
    // Cheap periodic refresh: prunes closed windows, adopts new ones and
    // re-syncs anything the user moved without a MOVESIZEEND we caught.
    RefreshTracker();
    backdrop_.EnsureAlive();
}

// ----------------------------------------------------------------- camera --

void App::SetCamera(const Camera& camera)
{
    if (camera == cam_)
        return;
    cam_ = camera;
    tracker_.ApplyCamera(cam_);
    backdrop_.OnCameraChanged(cam_);
    RecordFrame();
}

void App::FlyTo(const Camera& target)
{
    if (mode_ == CamMode::Panning)
        return; // do not fight an active gesture
    overview_.Hide();
    RefreshTracker();

    if (cfg_.flightMs <= 0 || !frameTimer_ || target == cam_) {
        mode_ = CamMode::Idle;
        StopFrameTimer();
        SetCamera(target);
        return;
    }

    mode_ = CamMode::Flying;
    flyFrom_ = cam_;
    flyTo_ = target;
    flyStartQpc_ = NowQpc();
    flyDuration_ = cfg_.flightMs;
    StartFrameTimer();
}

void App::RefreshTracker()
{
    tracker_.Refresh(cam_);
    dirty_ = false;

    if (tracker_.ElevatedSeen() && !elevatedNotified_) {
        elevatedNotified_ = true;
        tray_.Balloon(L"InfiniteCanvas",
                      L"Некоторые окна (например, Диспетчер задач) принадлежат процессам с "
                      L"более высокими правами и не могут быть перемещены. Запустите "
                      L"InfiniteCanvas от имени администратора, чтобы управлять ими.");
    }
}

// ------------------------------------------------------------- fps stats --

long long App::NowQpc() const
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

double App::SecondsSince(long long qpc) const
{
    return (double)(NowQpc() - qpc) / qpcFreq_;
}

void App::RecordFrame()
{
    const long long now = NowQpc();
    if (lastApplyQpc_) {
        double gapMs = (double)(now - lastApplyQpc_) * 1000.0 / qpcFreq_;
        if (gapMs < 250.0)
            statFrameMs_ = gapMs;
    }
    lastApplyQpc_ = now;

    if (!statWindowStartQpc_)
        statWindowStartQpc_ = now;
    ++statFrames_;
    double windowSec = (double)(now - statWindowStartQpc_) / qpcFreq_;
    if (windowSec >= 0.25) {
        statFps_ = statFrames_ / windowSec;
        statFrames_ = 0;
        statWindowStartQpc_ = now;
    }
}

FrameStats App::CurrentStats()
{
    FrameStats stats;
    stats.scale = cam_.scale;
    stats.windows = (int)tracker_.Windows().size();
    // Report zero while the canvas is idle: an FPS number would be stale.
    if (lastApplyQpc_ && SecondsSince(lastApplyQpc_) < 0.7) {
        stats.fps = statFps_;
        stats.frameMs = statFrameMs_;
    }
    return stats;
}

// ---------------------------------------------------------------- hotkeys --

void App::OnHotkey(int id)
{
    if (id >= HK_BM_GO_FIRST && id < HK_BM_GO_FIRST + 4) {
        const auto& bm = cfg_.bookmarks[id - HK_BM_GO_FIRST];
        if (bm.set)
            FlyTo(bm.cam);
        return;
    }
    if (id >= HK_BM_SET_FIRST && id < HK_BM_SET_FIRST + 4) {
        int slot = id - HK_BM_SET_FIRST;
        cfg_.bookmarks[slot].set = true;
        cfg_.bookmarks[slot].cam = cam_;
        cfg_.SaveBookmark(slot);
        tray_.Balloon(L"InfiniteCanvas", L"Закладка сохранена");
        return;
    }

    switch (id) {
    case HK_HOME:
        FlyTo(Camera{});
        break;
    case HK_OVERVIEW:
        ShowOverview();
        break;
    case HK_PAUSE:
        TogglePause();
        break;
    case HK_FPS:
        fps_.Toggle();
        break;
    case HK_EXIT:
        DestroyWindow(hwnd_);
        break;
    }
}

// ----------------------------------------------------------------- tray ----

void App::OnTrayMessage(LPARAM event)
{
    switch (LOWORD(event)) {
    case WM_LBUTTONDBLCLK:
        ShowOverview();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_OVERVIEW, L"Обзор холста\tCtrl+Alt+O");
        AppendMenuW(menu, MF_STRING, IDM_HOME, L"Вернуться домой (100%)\tCtrl+Alt+Home");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (paused_ ? MF_CHECKED : 0), IDM_PAUSE, L"Пауза\tCtrl+Alt+P");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG, L"Открыть config.ini");
        AppendMenuW(menu, MF_STRING, IDM_RELOAD_CONFIG, L"Перезагрузить конфиг");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Выход\tCtrl+Alt+Shift+Q");

        POINT pt{};
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd_); // required for the menu to close correctly
        int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                 pt.x, pt.y, 0, hwnd_, nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (cmd)
            OnMenuCommand(cmd);
        break;
    }
    }
}

void App::OnMenuCommand(int id)
{
    switch (id) {
    case IDM_OVERVIEW:
        ShowOverview();
        break;
    case IDM_HOME:
        FlyTo(Camera{});
        break;
    case IDM_PAUSE:
        TogglePause();
        break;
    case IDM_OPEN_CONFIG:
        ShellExecuteW(nullptr, L"open", cfg_.Path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case IDM_RELOAD_CONFIG:
        ReloadConfig();
        break;
    case IDM_EXIT:
        DestroyWindow(hwnd_);
        break;
    }
}

// --------------------------------------------------------------- features --

void App::ShowOverview()
{
    if (!cfg_.overviewEnabled)
        return;
    if (overview_.IsVisible()) {
        overview_.Hide();
        return;
    }
    if (mode_ == CamMode::Panning)
        return;
    StopFrameTimer();
    mode_ = CamMode::Idle;
    RefreshTracker();

    std::vector<OverviewItem> items;
    items.reserve(tracker_.Windows().size());
    for (const auto& entry : tracker_.Windows()) {
        OverviewItem item;
        item.hwnd = entry.hwnd;
        item.title = entry.title;
        item.virt = RECT{(LONG)std::lround(entry.vx), (LONG)std::lround(entry.vy),
                         (LONG)std::lround(entry.vx + entry.vw), (LONG)std::lround(entry.vy + entry.vh)};
        item.movable = entry.movable;
        items.push_back(std::move(item));
    }

    // Current viewport in canvas coordinates (virtual = screen/scale + cam).
    const double sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const double sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const double sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const double sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    RECT viewport{
        (LONG)std::lround(sx / cam_.scale + cam_.x),
        (LONG)std::lround(sy / cam_.scale + cam_.y),
        (LONG)std::lround((sx + sw) / cam_.scale + cam_.x),
        (LONG)std::lround((sy + sh) / cam_.scale + cam_.y),
    };

    overview_.Show(instance_, std::move(items), viewport, cfg_.overviewAlpha, cam_.scale,
                   [this](HWND hwnd) { CenterOnWindow(hwnd); });
}

void App::CenterOnWindow(HWND hwnd)
{
    const TrackedWindow* entry = tracker_.Find(hwnd);
    if (!entry)
        return;

    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
    const POINT center{(mi.rcWork.left + mi.rcWork.right) / 2,
                       (mi.rcWork.top + mi.rcWork.bottom) / 2};

    // Keep the current scale; the window's virtual center lands on the
    // monitor's center: cam = vCenter - screenCenter / scale.
    Camera target = cam_;
    target.x = (entry->vx + entry->vw / 2.0) - center.x / cam_.scale;
    target.y = (entry->vy + entry->vh / 2.0) - center.y / cam_.scale;
    FlyTo(target);
}

void App::TogglePause()
{
    paused_ = !paused_;
    pan_.SetPaused(paused_);
    tray_.SetTooltip(paused_ ? kTooltipPaused : kTooltip);
}

void App::ReloadConfig()
{
    // Unregister hotkeys first: EnableBookmarks may have changed.
    for (int i = 0; i < 4; ++i) {
        UnregisterHotKey(hwnd_, HK_BM_GO_FIRST + i);
        UnregisterHotKey(hwnd_, HK_BM_SET_FIRST + i);
    }
    for (int id : {HK_HOME, HK_OVERVIEW, HK_PAUSE, HK_EXIT, HK_FPS})
        UnregisterHotKey(hwnd_, id);

    cfg_.Load();
    LogInit(ExeDir() + L"\\infinitecanvas.log", cfg_.logToFile);
    framePeriod100ns_ = 10000000ll / std::max(30, std::min(240, cfg_.targetFps));
    RegisterHotkeys();

    backdrop_.Destroy();
    backdrop_.Create(instance_, &cfg_);
    pan_.SetExtraBackground(backdrop_.Handle());
    backdrop_.OnCameraChanged(cam_);

    RefreshTracker();
    tray_.Balloon(L"InfiniteCanvas", L"Конфиг перезагружен");
}
