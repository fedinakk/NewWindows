#include "WindowTracker.h"

#include "Config.h"

#include <dwmapi.h>

#include <algorithm>

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

namespace {

const UINT kSwpBaseFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER;

// Shell and system windows that must never be moved, regardless of config.
const wchar_t* kSystemBlacklist[] = {
    L"Progman",
    L"WorkerW",
    L"Shell_TrayWnd",
    L"Shell_SecondaryTrayWnd",
    L"Button", // Start button
    L"#32768", // popup menus
    L"tooltips_class32",
    L"ForegroundStaging",
    L"MultitaskingViewFrame",
    L"XamlExplorerHostIslandWindow",
    L"NotifyIconOverflowWindow",
    L"TopLevelWindowForOverflowXamlIsland",
    L"Shell_InputSwitchTopLevelWindow",
    L"TaskListThumbnailWnd",
    L"TaskListOverlayWnd",
    L"EdgeUiInputTopWndClass",
    L"Windows.UI.Core.CoreWindow",
};

bool EqualsAny(const wchar_t* cls, const std::vector<std::wstring>& list)
{
    for (const auto& item : list)
        if (_wcsicmp(cls, item.c_str()) == 0)
            return true;
    return false;
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty() || haystack.size() < needle.size())
        return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() && towlower(haystack[i + j]) == towlower(needle[j]))
            ++j;
        if (j == needle.size())
            return true;
    }
    return false;
}

DWORD ProcessIntegrityLevel(HANDLE process)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
        return (DWORD)-1;

    BYTE buf[256];
    DWORD len = 0;
    DWORD level = (DWORD)-1;
    if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len)) {
        auto* label = (TOKEN_MANDATORY_LABEL*)buf;
        PSID sid = label->Label.Sid;
        level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    }
    CloseHandle(token);
    return level;
}

} // namespace

void WindowTracker::Init(const Config* cfg, DWORD ownPid)
{
    cfg_ = cfg;
    ownPid_ = ownPid;
    ownIntegrity_ = ProcessIntegrityLevel(GetCurrentProcess());
    systemMinTrack_ = SIZE{GetSystemMetrics(SM_CXMINTRACK), GetSystemMetrics(SM_CYMINTRACK)};
}

struct EnumContext {
    WindowTracker* tracker;
    std::vector<TrackedWindow>* out;
};

BOOL CALLBACK WindowTracker::EnumProc(HWND hwnd, LPARAM lparam)
{
    auto* ctx = (EnumContext*)lparam;
    if (ctx->tracker->IsManageable(hwnd)) {
        TrackedWindow entry;
        entry.hwnd = hwnd;
        ctx->out->push_back(std::move(entry));
    }
    return TRUE;
}

bool WindowTracker::IsResizableWindow(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (style & WS_THICKFRAME) != 0 && !IsZoomed(hwnd);
}

// Adopt a window at its current screen rect: the virtual rect is whatever
// currently maps onto that screen rect under the current camera. A window
// opened while zoomed out keeps its apparent size (no visual snap), which
// means its canvas size differs from its natural size - documented in README.
void WindowTracker::AdoptRect(TrackedWindow& entry, const RECT& rect, const Camera& camera) const
{
    ScreenToVirtual(camera, POINT{rect.left, rect.top}, entry.vx, entry.vy);
    entry.vw = (rect.right - rect.left) / camera.scale;
    entry.vh = (rect.bottom - rect.top) / camera.scale;
    entry.appliedPos = POINT{rect.left, rect.top};
    entry.appliedSize = SIZE{rect.right - rect.left, rect.bottom - rect.top};
}

void WindowTracker::Refresh(const Camera& camera)
{
    std::vector<TrackedWindow> next;
    next.reserve(64);
    EnumContext ctx{this, &next};
    EnumWindows(EnumProc, (LPARAM)&ctx);

    std::vector<TrackedWindow> ready;
    ready.reserve(next.size());
    for (auto& entry : next) {
        RECT rect{};
        if (!GetWindowRect(entry.hwnd, &rect))
            continue; // window died between EnumWindows and now

        wchar_t title[256] = L"";
        GetWindowTextW(entry.hwnd, title, 256);
        entry.title = title;
        entry.resizable = IsResizableWindow(entry.hwnd);

        const TrackedWindow* old = Find(entry.hwnd);
        if (!old) {
            entry.movable = ProbeMovable(entry.hwnd);
            entry.minSize = systemMinTrack_;
            AdoptRect(entry, rect, camera);
            ready.push_back(std::move(entry));
            continue;
        }

        entry.movable = old->movable;
        entry.minSize = old->minSize;
        entry.vx = old->vx;
        entry.vy = old->vy;
        entry.vw = old->vw;
        entry.vh = old->vh;
        entry.appliedPos = old->appliedPos;
        entry.appliedSize = old->appliedSize;

        const SIZE actual{rect.right - rect.left, rect.bottom - rect.top};

        // Position: absorb only external moves; if the rect matches what we
        // last applied, keep the fractional virtual position (re-quantizing
        // it through integer screen coords every refresh would drift).
        if (rect.left != entry.appliedPos.x || rect.top != entry.appliedPos.y) {
            ScreenToVirtual(camera, POINT{rect.left, rect.top}, entry.vx, entry.vy);
            entry.appliedPos = POINT{rect.left, rect.top};
        }

        if (!entry.resizable) {
            // We never request sizes for these; their canvas footprint is
            // simply their current screen size seen through the camera.
            entry.vw = actual.cx / camera.scale;
            entry.vh = actual.cy / camera.scale;
            entry.appliedSize = actual;
        } else if (actual.cx != entry.appliedSize.cx || actual.cy != entry.appliedSize.cy) {
            // Per dimension: bigger than requested means the window refused
            // to shrink (its own min size) - learn the floor, keep the
            // virtual size pristine so zooming back restores it. Smaller
            // means an external resize - absorb it and forget the floor.
            if (actual.cx > entry.appliedSize.cx) {
                entry.minSize.cx = std::max(entry.minSize.cx, actual.cx);
            } else {
                entry.vw = actual.cx / camera.scale;
                entry.minSize.cx = systemMinTrack_.cx;
            }
            if (actual.cy > entry.appliedSize.cy) {
                entry.minSize.cy = std::max(entry.minSize.cy, actual.cy);
            } else {
                entry.vh = actual.cy / camera.scale;
                entry.minSize.cy = systemMinTrack_.cy;
            }
            entry.appliedSize = actual;
        }

        ready.push_back(std::move(entry));
    }

    windows_ = std::move(ready);
}

bool WindowTracker::ResyncWindow(HWND hwnd, const Camera& camera)
{
    for (auto& entry : windows_) {
        if (entry.hwnd != hwnd)
            continue;
        RECT rect{};
        if (!GetWindowRect(hwnd, &rect))
            return false;
        // The user moved/resized it deliberately: their rect is the truth.
        entry.resizable = IsResizableWindow(hwnd);
        entry.minSize = systemMinTrack_;
        AdoptRect(entry, rect, camera);
        return true;
    }
    return false;
}

const TrackedWindow* WindowTracker::Find(HWND hwnd) const
{
    for (const auto& entry : windows_)
        if (entry.hwnd == hwnd)
            return &entry;
    return nullptr;
}

void WindowTracker::ApplyCamera(const Camera& camera)
{
    struct Move {
        size_t index;
        POINT pos;
        SIZE size;
        bool resize;
    };
    std::vector<Move> moves;
    moves.reserve(windows_.size());

    for (size_t i = 0; i < windows_.size(); ++i) {
        auto& entry = windows_[i];
        if (!entry.movable)
            continue;
        if (!IsWindow(entry.hwnd) || IsIconic(entry.hwnd))
            continue;
        // A hung window would stall the whole batch inside EndDeferWindowPos.
        if (IsHungAppWindow(entry.hwnd))
            continue;

        POINT pos = VirtualToScreen(camera, entry.vx, entry.vy);
        // A window can become maximized between refreshes; never resize those.
        bool resize = entry.resizable && !IsZoomed(entry.hwnd);
        SIZE size = entry.appliedSize;
        if (resize) {
            size.cx = std::max((LONG)std::lround(entry.vw * camera.scale), entry.minSize.cx);
            size.cy = std::max((LONG)std::lround(entry.vh * camera.scale), entry.minSize.cy);
        }

        bool posChanged = pos.x != entry.appliedPos.x || pos.y != entry.appliedPos.y;
        bool sizeChanged = resize && (size.cx != entry.appliedSize.cx || size.cy != entry.appliedSize.cy);
        if (!posChanged && !sizeChanged)
            continue; // never issue no-op DeferWindowPos calls

        moves.push_back({i, pos, size, sizeChanged});
    }

    if (moves.empty())
        return;

    HDWP hdwp = BeginDeferWindowPos((int)moves.size());
    bool ok = hdwp != nullptr;
    if (ok) {
        for (const auto& move : moves) {
            UINT flags = kSwpBaseFlags | (move.resize ? 0 : SWP_NOSIZE);
            hdwp = DeferWindowPos(hdwp, windows_[move.index].hwnd, nullptr,
                                  move.pos.x, move.pos.y, move.size.cx, move.size.cy, flags);
            if (!hdwp) {
                ok = false;
                break;
            }
        }
    }
    if (ok)
        ok = !!EndDeferWindowPos(hdwp);

    if (ok) {
        for (const auto& move : moves) {
            windows_[move.index].appliedPos = move.pos;
            if (move.resize)
                windows_[move.index].appliedSize = move.size;
        }
        return;
    }

    // Batch failed (one bad handle kills the whole HDWP) - move individually
    // and remember which windows we are not allowed to touch.
    for (const auto& move : moves) {
        auto& entry = windows_[move.index];
        UINT flags = kSwpBaseFlags | (move.resize ? 0 : SWP_NOSIZE);
        SetLastError(0);
        if (SetWindowPos(entry.hwnd, nullptr, move.pos.x, move.pos.y,
                         move.size.cx, move.size.cy, flags)) {
            entry.appliedPos = move.pos;
            if (move.resize)
                entry.appliedSize = move.size;
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            entry.movable = false;
            elevatedSeen_ = true;
            Log(L"ApplyCamera: access denied for '%s', marking unmovable", entry.title.c_str());
        }
    }
}

bool WindowTracker::IsManageable(HWND hwnd) const
{
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return false;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd)
        return false;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (style & WS_CHILD)
        return false;

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW))
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ownPid_ || pid == 0)
        return false;

    // Cloaked windows: UWP ghosts and windows on other virtual desktops.
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;

    wchar_t cls[128] = L"";
    GetClassNameW(hwnd, cls, 128);
    for (const wchar_t* sys : kSystemBlacklist)
        if (_wcsicmp(cls, sys) == 0)
            return false;
    if (EqualsAny(cls, cfg_->excludeClasses) || EqualsAny(cls, cfg_->backgroundClasses))
        return false;

    wchar_t title[256] = L"";
    GetWindowTextW(hwnd, title, 256);
    if (cfg_->skipUntitled && !title[0])
        return false;
    if (title[0]) {
        for (const auto& excluded : cfg_->excludeTitles)
            if (ContainsNoCase(title, excluded))
                return false;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect))
        return false;
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return false;

    if (cfg_->skipFullscreen && IsFullscreen(hwnd, style, rect))
        return false;
    if (cfg_->skipMaximized && IsZoomed(hwnd))
        return false;

    return true;
}

bool WindowTracker::IsFullscreen(HWND hwnd, LONG_PTR style, const RECT& rect) const
{
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return false;
    bool covers = rect.left <= mi.rcMonitor.left && rect.top <= mi.rcMonitor.top &&
                  rect.right >= mi.rcMonitor.right && rect.bottom >= mi.rcMonitor.bottom;
    // A maximized app still has WS_CAPTION; borderless fullscreen does not.
    return covers && !(style & (WS_CAPTION | WS_THICKFRAME));
}

bool WindowTracker::ProbeMovable(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        // Cannot even query the process: protected or elevated. UIPI would
        // silently reject our SetWindowPos anyway.
        elevatedSeen_ = true;
        return false;
    }
    DWORD level = ProcessIntegrityLevel(process);
    CloseHandle(process);

    if (level == (DWORD)-1 || (ownIntegrity_ != (DWORD)-1 && level > ownIntegrity_)) {
        elevatedSeen_ = true;
        return false;
    }
    return true;
}
