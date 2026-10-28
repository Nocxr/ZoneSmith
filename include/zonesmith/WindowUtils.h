#pragma once

#include "AppState.h"
#include <dwmapi.h>

RECT VisibleWindowRect(HWND window);

// Moves/sizes a window so its visible frame (excluding the invisible resize
// borders of Windows 10/11) matches target.
void SetVisibleWindowRect(
    HWND window,
    const RECT& target
);

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