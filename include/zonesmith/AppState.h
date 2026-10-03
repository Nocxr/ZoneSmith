#pragma once

#include "Types.h"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr wchar_t kOverlayClass[] = L"ZoneSmithOverlay";

constexpr int kMaxZones = 9;
constexpr int kQuitHotkeyId = 1;

constexpr UINT_PTR kSnapTimerId = 1;

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayInstructionsId = 1001;
constexpr UINT kTrayLayoutsId = 1002;
constexpr UINT kTrayPauseId = 1003;
constexpr UINT kTrayStartupId = 1004;
constexpr UINT kTrayExitId = 1005;

constexpr DWORD kHorizontalCycleTimeoutMs = 700;
constexpr int kHorizontalCycleMoveTolerance = 12;

struct AppState {
    HINSTANCE instance{};

    HWND overlay{};

    HHOOK mouseHook{};
    HHOOK keyboardHook{};
    HWINEVENTHOOK moveSizeHook{};

    HWND dragWindow{};

    POINT cursor{};

    RECT monitorWork{};
    RECT overlayBounds{};

    bool leftDown{};
    bool overlayVisible{};

    bool swallowRightUp{};
    bool swallowMiddleUp{};

    bool compactMode{};
    bool backtickDown{};

    unsigned int hotZones{};
    unsigned int selectedZones{};

    std::vector<Layout> layouts;
    size_t layoutIndex{};

    std::array<bool, kMaxZones> numberKeyDown{};

    // Window cycling.
    std::vector<HWND> cycleWindows;
    int cycleIndex{-1};

    bool windowCycleActive{};
    bool windowCycleNeedsRetarget{};

    bool leftWindowsDown{};
    bool rightWindowsDown{};
    bool windowsWheelUsed{};

    // Horizontal title-bar cycling.
    bool horizontalCycleActive{};

    POINT horizontalCycleAnchor{};

    DWORD horizontalCycleLastTick{};

    int horizontalWheelRemainder{};
    int horizontalCycleNotches{2};

    // Settings.
    int windowOverlapPercent{25};
    int snapPadding{};

    bool pauseInFullscreen{true};
    bool startWithWindows{};
    bool paused{};

    // Persistent monitor/layout state.
    std::wstring currentMonitorKey;

    std::unordered_map<std::wstring, std::wstring>
        monitorLayouts;

    std::unordered_set<std::wstring>
        excludedApps;

    // Snap/restore state.
    std::unordered_map<HWND, SavedWindow>
        savedWindows;

    PendingSnap pendingSnap{};
    PendingRestore pendingRestore{};

    HWND restoreOnReleaseWindow{};

    NOTIFYICONDATAW trayIcon{};
};

extern AppState* g_app;

inline const Layout& CurrentLayout(const AppState& app) {
    return app.layouts[app.layoutIndex];
}