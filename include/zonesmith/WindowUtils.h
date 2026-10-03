#pragma once

#include "AppState.h"
#include <dwmapi.h>

RECT VisibleWindowRect(HWND window);

std::wstring WindowExecutableName(
    HWND window
);

bool IsWindowExcluded(
    const AppState& app,
    HWND window
);

bool IsFullscreenWindow(
    HWND window
);

bool ShouldSuspendForForeground(
    const AppState& app
);

bool IsCycleWindow(
    const AppState& app,
    HWND window
);

bool IsPointOnWindowTitleBar(
    const AppState& app,
    HWND window,
    POINT point
);

bool IsKeyDownNow(
    int virtualKey
);

bool IsWindowsKeyDownNow();

void SyncWindowsKeyState(
    AppState& app
);