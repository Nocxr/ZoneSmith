#pragma once

#include "DynamicLayout.h"
#include "Types.h"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <utility>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr wchar_t kOverlayClass[] = L"ZoneSmithOverlay";

constexpr int kMaxZones = 9;
constexpr int kQuitHotkeyId = 1;

constexpr UINT_PTR kSnapTimerId = 1;
constexpr UINT_PTR kDynamicReconcileTimerId = 2;
constexpr UINT_PTR kDynamicWindowEventTimerId = 3;

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kDividerMoveMessage = WM_APP + 30;
constexpr UINT kDividerEndMessage = WM_APP + 31;
constexpr UINT kTrayInstructionsId = 1001;
constexpr UINT kTrayLayoutsId = 1002;
constexpr UINT kTrayPauseId = 1003;
constexpr UINT kTrayStartupId = 1004;
constexpr UINT kTrayExitId = 1005;

constexpr DWORD kHorizontalCycleTimeoutMs = 700;
constexpr int kHorizontalCycleMoveTolerance = 12;

// Dynamic layout state for one monitor. `tree` is the full logical layout,
// including hidden (minimized) windows; `applied` is what was last placed on
// screen, so reconciliation only moves windows when the layout changed.
struct DynamicMonitorState {
    zsdyn::Tree tree;
    std::vector<zsdyn::WindowPlacement> applied;
    unsigned int revision{};
    unsigned int savedRevision{};
};

// One drop computed from the current drag position. The overlay paints it and
// a drop commits it unchanged, so preview and placement cannot disagree.
struct DynamicDropPlan {
    bool valid{};
    HWND window{};
    std::wstring key;
    zsdyn::Rect work{};
    zsdyn::DropOption option{};
    int optionIndex{};
    int optionCount{};
    zsdyn::Tree result;
    unsigned int revision{};
    // Non-empty when the drop would squeeze a window below its minimum
    // size; the preview shows why and the drop does nothing.
    std::wstring blocked;
    // Set when the dragged window leaves a layout on another monitor.
    bool hasSource{};
    std::wstring sourceKey;
    zsdyn::Tree sourceResult;
    unsigned int sourceRevision{};
};

struct AppState {
    HINSTANCE instance{};

    HWND overlay{};
    HWND dividerGuideWindow{};
    bool dividerGuideVisible{};
    std::wstring dividerGuideMonitor;
    HWND dynamicTitleDragCandidate{};
    bool dynamicReflowApplying{};

    HHOOK mouseHook{};
    HHOOK keyboardHook{};
    HWINEVENTHOOK moveSizeHook{};
    HWINEVENTHOOK dynamicMinimizeHook{};
    HWINEVENTHOOK dynamicObjectHook{};
    HWINEVENTHOOK dynamicCloakHook{};

    HWND dragWindow{};
    // True only for a title-bar move. Border resizes never dock.
    bool dynamicDragIsMove{};
    bool dynamicDragWasZoomed{};
    SIZE dynamicDragStartSize{};
    // Right-click hid the overlay during this drag: drop floats the window.
    bool dynamicOverlayDismissed{};
    // Escape during this drag: no layout change; a tiled window returns.
    bool dynamicDragCancelled{};
    // Tiled window being resized by its border; dividers follow on release.
    HWND dynamicResizeWindow{};
    HWND pendingDynamicResize{};
    // Minimum visible sizes learned from windows that refused to shrink.
    std::unordered_map<HWND, SIZE> dynamicMinSizes;
    // Windows is ending the session: apps closing must not erase saved layouts.
    bool dynamicSaveFrozen{};
    int dynamicOptionIndex{};
    std::wstring dynamicOptionContext;
    DynamicDropPlan dynamicPreview;
    DynamicDropPlan pendingDynamicDrop;
    HWND pendingDynamicUndock{};
    std::unordered_map<std::wstring, DynamicMonitorState> dynamicMonitors;

    std::wstring dynamicDividerMonitor;
    int dynamicDividerNode{-1};
    bool dynamicDividerApplying{};
    bool dynamicDividerMoveQueued{};
    POINT dynamicDividerLatest{};
    RECT dynamicDividerBounds{};

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