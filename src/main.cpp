#include "App.h"
#include "Common.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // A second instance would install a second global mouse hook.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\InfiniteCanvas.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
                    L"InfiniteCanvas уже запущен.\nИщите иконку в системном трее.",
                    L"InfiniteCanvas", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    // Must run before any window is created.
    EnablePerMonitorDpi();

    App app;
    int rc = app.Run(instance);

    if (mutex)
        CloseHandle(mutex);
    return rc;
}
