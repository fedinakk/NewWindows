#include "App.h"

#include <algorithm>
#include <cmath>

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

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    s_app = nullptr;
    return (int)msg.wParam;
}

void App::Setup()
{
    pan_.Init(hwnd_, &cfg_);
    tray_.Create(hwnd_, WM_APP_TRAY, kTooltip);
    RegisterHotkeys();
    InstallWinEventHooks();
    SetTimer(hwnd_, IDT_HOUSEKEEP, 2000, nullptr);

    // Adopt the initial layout: current screen positions become the canvas
    // origin, so Ctrl+Alt+Home always returns to this arrangement.
    RefreshTracker();
}

void App::Teardown()
{
    KillTimer(hwnd_, IDT_HOUSEKEEP);
    StopTickTimer();
    for (auto& hook : eventHooks_) {
        if (hook) {
            UnhookWinEvent(hook);
            hook = nullptr;
        }
    }
    for (int id : {HK_HOME, HK_OVERVIEW, HK_PAUSE, HK_EXIT})
        UnregisterHotKey(hwnd_, id);
    for (int i = 0; i < 4; ++i) {
        UnregisterHotKey(hwnd_, HK_BM_GO_FIRST + i);
        UnregisterHotKey(hwnd_, HK_BM_SET_FIRST + i);
    }
    overview_.Destroy();
    tray_.Destroy();
    pan_.Shutdown();
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

    case WM_APP_DIRTY:
        dirtyPosted_ = false;
        dirty_ = true;
        // During a pan, adopt new windows right away (rate-limited) so they
        // join the canvas instead of being left behind.
        if (mode_ == CamMode::Panning && GetTickCount64() - lastPanRefresh_ > 200) {
            lastPanRefresh_ = GetTickCount64();
            RefreshTracker();
        }
        return 0;

    case WM_APP_MOVESIZEEND:
        if (!tracker_.ResyncWindow((HWND)lparam, camera_))
            dirty_ = true; // unknown window: pick it up on the next refresh
        return 0;

    case WM_HOTKEY:
        OnHotkey((int)wparam);
        return 0;

    case WM_TIMER:
        if (wparam == IDT_TICK)
            OnTick();
        else if (wparam == IDT_HOUSEKEEP)
            OnHousekeep();
        return 0;

    case WM_APP_TRAY:
        OnTrayMessage(lparam);
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
    StopTickTimer();
    mode_ = CamMode::Panning;
    lastPanRefresh_ = GetTickCount64();
    RefreshTracker(); // authoritative re-sync right before the gesture
    panStartCamera_ = camera_;
    panStartCursor_ = pan_.StartPoint();
}

void App::OnPanUpdate()
{
    POINT cursor = pan_.ConsumeLatest();
    if (mode_ != CamMode::Panning)
        return;
    const double s = cfg_.sensitivity;
    POINT cam{
        panStartCamera_.x - (LONG)std::lround((cursor.x - panStartCursor_.x) * s),
        panStartCamera_.y - (LONG)std::lround((cursor.y - panStartCursor_.y) * s),
    };
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
    if (!cfg_.inertiaEnabled || speed < cfg_.inertiaMinVelocity)
        return;

    mode_ = CamMode::Inertia;
    velX_ = -vx * cfg_.sensitivity;
    velY_ = -vy * cfg_.sensitivity;
    inertiaX_ = camera_.x;
    inertiaY_ = camera_.y;
    lastTick_ = GetTickCount64();
    StartTickTimer();
}

void App::OnTick()
{
    const ULONGLONG now = GetTickCount64();

    if (mode_ == CamMode::Inertia) {
        double dt = (double)(now - lastTick_) / 1000.0;
        lastTick_ = now;
        if (dt <= 0)
            return;
        dt = std::min(dt, 0.1); // a stalled timer must not teleport the canvas

        inertiaX_ += velX_ * dt;
        inertiaY_ += velY_ * dt;
        const double decay = std::exp(-cfg_.inertiaFriction * dt);
        velX_ *= decay;
        velY_ *= decay;

        SetCamera(POINT{(LONG)std::lround(inertiaX_), (LONG)std::lround(inertiaY_)});

        if (std::hypot(velX_, velY_) < 60.0) {
            mode_ = CamMode::Idle;
            StopTickTimer();
        }
        return;
    }

    if (mode_ == CamMode::Flying) {
        double t = flyDuration_ > 0 ? (double)(now - flyStart_) / flyDuration_ : 1.0;
        t = std::min(1.0, std::max(0.0, t));
        const double e = EaseOutCubic(t);
        SetCamera(POINT{
            flyFrom_.x + (LONG)std::lround((flyTo_.x - flyFrom_.x) * e),
            flyFrom_.y + (LONG)std::lround((flyTo_.y - flyFrom_.y) * e),
        });
        if (t >= 1.0) {
            mode_ = CamMode::Idle;
            StopTickTimer();
        }
        return;
    }

    StopTickTimer();
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
}

// ----------------------------------------------------------------- camera --

void App::SetCamera(POINT camera)
{
    if (camera.x == camera_.x && camera.y == camera_.y)
        return;
    camera_ = camera;
    tracker_.ApplyCamera(camera_);
}

void App::FlyTo(POINT target)
{
    if (mode_ == CamMode::Panning)
        return; // do not fight an active gesture
    overview_.Hide();
    RefreshTracker();

    if (cfg_.flightMs <= 0 || (target.x == camera_.x && target.y == camera_.y)) {
        mode_ = CamMode::Idle;
        StopTickTimer();
        SetCamera(target);
        return;
    }

    mode_ = CamMode::Flying;
    flyFrom_ = camera_;
    flyTo_ = target;
    flyStart_ = GetTickCount64();
    flyDuration_ = cfg_.flightMs;
    StartTickTimer();
}

void App::StartTickTimer()
{
    if (!tickTimerOn_) {
        SetTimer(hwnd_, IDT_TICK, 15, nullptr);
        tickTimerOn_ = true;
    }
}

void App::StopTickTimer()
{
    if (tickTimerOn_) {
        KillTimer(hwnd_, IDT_TICK);
        tickTimerOn_ = false;
    }
}

void App::RefreshTracker()
{
    tracker_.Refresh(camera_);
    dirty_ = false;

    if (tracker_.ElevatedSeen() && !elevatedNotified_) {
        elevatedNotified_ = true;
        tray_.Balloon(L"InfiniteCanvas",
                      L"Некоторые окна (например, Диспетчер задач) принадлежат процессам с "
                      L"более высокими правами и не могут быть перемещены. Запустите "
                      L"InfiniteCanvas от имени администратора, чтобы управлять ими.");
    }
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
        cfg_.bookmarks[slot].cam = camera_;
        cfg_.SaveBookmark(slot);
        tray_.Balloon(L"InfiniteCanvas", L"Закладка сохранена");
        return;
    }

    switch (id) {
    case HK_HOME:
        FlyTo(POINT{0, 0});
        break;
    case HK_OVERVIEW:
        ShowOverview();
        break;
    case HK_PAUSE:
        TogglePause();
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
        AppendMenuW(menu, MF_STRING, IDM_HOME, L"Вернуться домой\tCtrl+Alt+Home");
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
        FlyTo(POINT{0, 0});
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
    StopTickTimer();
    mode_ = CamMode::Idle;
    RefreshTracker();

    std::vector<OverviewItem> items;
    items.reserve(tracker_.Windows().size());
    for (const auto& entry : tracker_.Windows()) {
        OverviewItem item;
        item.hwnd = entry.hwnd;
        item.title = entry.title;
        item.virt = RECT{entry.virt.x, entry.virt.y,
                         entry.virt.x + entry.size.cx, entry.virt.y + entry.size.cy};
        item.movable = entry.movable;
        items.push_back(std::move(item));
    }

    RECT viewport{
        GetSystemMetrics(SM_XVIRTUALSCREEN) + camera_.x,
        GetSystemMetrics(SM_YVIRTUALSCREEN) + camera_.y,
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + camera_.x,
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) + camera_.y,
    };

    overview_.Show(instance_, std::move(items), viewport, cfg_.overviewAlpha,
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

    FlyTo(POINT{
        entry->virt.x + entry->size.cx / 2 - center.x,
        entry->virt.y + entry->size.cy / 2 - center.y,
    });
}

void App::TogglePause()
{
    paused_ = !paused_;
    pan_.SetPaused(paused_);
    tray_.SetTooltip(paused_ ? kTooltipPaused : kTooltip);
}

void App::ReloadConfig()
{
    // Unregister bookmark hotkeys first: EnableBookmarks may have changed.
    for (int i = 0; i < 4; ++i) {
        UnregisterHotKey(hwnd_, HK_BM_GO_FIRST + i);
        UnregisterHotKey(hwnd_, HK_BM_SET_FIRST + i);
    }
    for (int id : {HK_HOME, HK_OVERVIEW, HK_PAUSE, HK_EXIT})
        UnregisterHotKey(hwnd_, id);

    cfg_.Load();
    LogInit(ExeDir() + L"\\infinitecanvas.log", cfg_.logToFile);
    RegisterHotkeys();
    RefreshTracker();
    tray_.Balloon(L"InfiniteCanvas", L"Конфиг перезагружен");
}
