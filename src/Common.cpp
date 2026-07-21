#include "Common.h"

#include <cstdarg>
#include <cstdio>

namespace {
std::wstring g_logPath;
bool g_logToFile = false;
}

void LogInit(const std::wstring& filePath, bool toFile)
{
    g_logPath = filePath;
    g_logToFile = toFile;
}

void Log(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringW(L"[InfiniteCanvas] ");
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");

    if (!g_logToFile || g_logPath.empty())
        return;

    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1200];
    _snwprintf_s(line, _TRUNCATE, L"%02u:%02u:%02u.%03u %s\r\n",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);

    char utf8[2400];
    int len = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (len > 1) {
        DWORD written = 0;
        WriteFile(file, utf8, (DWORD)(len - 1), &written, nullptr);
    }
    CloseHandle(file);
}

std::wstring ExeDir()
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, len);
    size_t slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos)
        dir.resize(slash);
    return dir;
}

void EnablePerMonitorDpi()
{
    // SetProcessDpiAwarenessContext is Win10 1703+, so resolve it dynamically.
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setCtx = (SetCtxFn)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setCtx) {
            const HANDLE perMonitorV2 = (HANDLE)(INT_PTR)-4; // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            if (setCtx(perMonitorV2))
                return;
        }
    }

    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using SetAwarenessFn = HRESULT(WINAPI*)(int);
        auto setAwareness = (SetAwarenessFn)(void*)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (setAwareness && SUCCEEDED(setAwareness(2 /* PROCESS_PER_MONITOR_DPI_AWARE */)))
            return;
    }

    SetProcessDPIAware();
}

UINT DpiForWindow(HWND hwnd)
{
    using GetDpiFn = UINT(WINAPI*)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto getDpi = (GetDpiFn)(void*)GetProcAddress(user32, "GetDpiForWindow");
        if (getDpi) {
            UINT dpi = getDpi(hwnd);
            if (dpi)
                return dpi;
        }
    }
    return 96;
}
