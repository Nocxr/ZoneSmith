#include "InputHooks.h"

#include "Overlay.h"
#include "Snap.h"
#include "WindowCycle.h"
#include "WindowUtils.h"

#include <array>
#include <cmath>

namespace {

void SuppressStartMenuAfterWinWheel() {
    //
    // A harmless Ctrl tap makes Windows treat the Win press
    // as a chord so releasing Win doesn't open Start.
    //
    std::array<INPUT, 2> inputs{};

    inputs[0].type =
        INPUT_KEYBOARD;

    inputs[0].ki.wVk =
        VK_CONTROL;

    inputs[1].type =
        INPUT_KEYBOARD;

    inputs[1].ki.wVk =
        VK_CONTROL;

    inputs[1].ki.dwFlags =
        KEYEVENTF_KEYUP;

    SendInput(
        static_cast<UINT>(
            inputs.size()
        ),
        inputs.data(),
        sizeof(INPUT)
    );
}

}

LRESULT CALLBACK MouseHook(
    int code,
    WPARAM message,
    LPARAM data
) {
    if (
        code != HC_ACTION ||
        !g_app
    ) {
        return CallNextHookEx(
            nullptr,
            code,
            message,
            data
        );
    }

    AppState& app =
        *g_app;

    const auto* event =
        reinterpret_cast<
            MSLLHOOKSTRUCT*
        >(data);

    app.cursor =
        event->pt;

    //
    // Use Windows' actual current modifier state for mouse
    // behavior. Do not trust the cached keyboard-hook flags.
    //
    const bool windowsKeyDown =
        IsWindowsKeyDownNow();

    //
    // ------------------------------------------------------------
    // Suspension
    // ------------------------------------------------------------
    //
    const bool suspensionRelevant =
        app.overlayVisible ||
        app.windowCycleActive ||
        app.horizontalCycleActive ||
        message == WM_LBUTTONDOWN ||
        message == WM_RBUTTONDOWN ||
        message == WM_MOUSEWHEEL ||
        message == WM_MOUSEHWHEEL;

    if (
        app.paused ||
        (
            suspensionRelevant &&
            ShouldSuspendForForeground(
                app
            )
        )
    ) {
        HideOverlay(app);

        app.dragWindow =
            nullptr;

        ResetWindowCycle(app);

        if (
            message ==
            WM_LBUTTONUP
        ) {
            app.leftDown =
                false;
        }

        return CallNextHookEx(
            app.mouseHook,
            code,
            message,
            data
        );
    }

    //
    // ------------------------------------------------------------
    // Win + vertical wheel
    // ------------------------------------------------------------
    //
    if (
        message == WM_MOUSEWHEEL &&
        !app.leftDown &&
        windowsKeyDown
    ) {
        //
        // Win+wheel and horizontal cycling are separate
        // gestures. Never allow a horizontal session to
        // bleed into this one.
        //
        if (
            app.horizontalCycleActive
        ) {
            ResetWindowCycle(app);
        }

        app.windowsWheelUsed =
            true;

        HWND source =
            GetAncestor(
                WindowFromPoint(
                    event->pt
                ),
                GA_ROOT
            );

        //
        // If the pointer has changed windows, rebuild the
        // overlap group from scratch.
        //
        if (
            app.windowCycleNeedsRetarget
        ) {
            ResetWindowCycle(app);
        }

        if (!app.windowCycleActive) {
            if (
                !source ||
                !BeginWindowCycle(
                    app,
                    source
                )
            ) {
                return 1;
            }
        }

        const SHORT wheelDelta =
            static_cast<SHORT>(
                HIWORD(
                    event->mouseData
                )
            );

        AdvanceWindowPreview(
            app,
            nullptr,
            wheelDelta > 0
                ? 1
                : -1
        );

        return 1;
    }

    //
    // ------------------------------------------------------------
    // Horizontal title-bar wheel
    // ------------------------------------------------------------
    //
    if (
        message == WM_MOUSEHWHEEL &&
        !app.leftDown &&
        !windowsKeyDown
    ) {
        //
        // If Win+wheel left an active common cycle, kill it
        // before entering horizontal mode.
        //
        if (
            app.windowCycleActive &&
            !app.horizontalCycleActive
        ) {
            ResetWindowCycle(app);
        }

        if (
            HandleTitleBarHorizontalWheel(
                app,
                event
            )
        ) {
            return 1;
        }
    }

    //
    // Moving the mouse during Win+wheel means the next Win
    // wheel event should acquire a new source window.
    //
    if (
        message == WM_MOUSEMOVE &&
        app.windowCycleActive &&
        !app.horizontalCycleActive &&
        windowsKeyDown
    ) {
        app.windowCycleNeedsRetarget =
            true;
    }

    //
    // Don't immediately destroy horizontal cycling for tiny
    // accidental mouse movement.
    //
    if (
        message == WM_MOUSEMOVE &&
        app.horizontalCycleActive
    ) {
        const int dx =
            event->pt.x -
            app.horizontalCycleAnchor.x;

        const int dy =
            event->pt.y -
            app.horizontalCycleAnchor.y;

        //
        // Larger than the old 12px tolerance.
        // Tilt wheels can physically move the mouse slightly.
        //
        constexpr int tolerance = 32;

        if (
            std::abs(dx) > tolerance ||
            std::abs(dy) > tolerance
        ) {
            ResetWindowCycle(app);
        }
    }

    if (
        app.horizontalCycleActive &&
        (
            message == WM_LBUTTONDOWN ||
            message == WM_RBUTTONDOWN ||
            message == WM_MBUTTONDOWN
        )
    ) {
        ResetWindowCycle(app);
    }

    //
    // Normal vertical scrolling means we're done with a
    // horizontal session.
    //
    if (
        message == WM_MOUSEWHEEL &&
        app.horizontalCycleActive &&
        !windowsKeyDown
    ) {
        ResetWindowCycle(app);
    }

    //
    // ------------------------------------------------------------
    // Normal drag / zone behavior
    // ------------------------------------------------------------
    //
    if (
        message ==
        WM_LBUTTONDOWN
    ) {
        app.leftDown =
            true;
    }

    else if (
        message ==
            WM_MOUSEMOVE &&
        app.leftDown
    ) {
        if (
            app.overlayVisible
        ) {
            HMONITOR oldMonitor =
                MonitorFromRect(
                    &app.monitorWork,
                    MONITOR_DEFAULTTONEAREST
                );

            HMONITOR newMonitor =
                MonitorFromPoint(
                    event->pt,
                    MONITOR_DEFAULTTONEAREST
                );

            if (
                oldMonitor !=
                newMonitor
            ) {
                ShowOverlay(
                    app,
                    event->pt
                );
            }

            const unsigned int zones =
                ZonesAt(
                    app,
                    event->pt
                );

            if (
                zones !=
                app.hotZones
            ) {
                app.hotZones =
                    zones;

                InvalidateRect(
                    app.overlay,
                    nullptr,
                    TRUE
                );
            }
        }
    }

    else if (
        (
            message == WM_MOUSEWHEEL ||
            message == WM_MOUSEHWHEEL
        ) &&
        app.leftDown &&
        app.dragWindow &&
        app.overlayVisible
    ) {
        const SHORT wheelDelta =
            static_cast<SHORT>(
                HIWORD(
                    event->mouseData
                )
            );

        CycleLayout(
            app,
            wheelDelta > 0
                ? 1
                : -1
        );

        return 1;
    }

    else if (
        message ==
            WM_MBUTTONDOWN &&
        app.leftDown &&
        app.dragWindow &&
        app.overlayVisible
    ) {
        const unsigned int zones =
            ZonesAt(
                app,
                event->pt
            );

        if (zones != 0) {
            app.selectedZones ^=
                zones;

            app.hotZones =
                zones;

            InvalidateRect(
                app.overlay,
                nullptr,
                TRUE
            );
        }

        app.swallowMiddleUp =
            true;

        return 1;
    }

    else if (
        message ==
            WM_MBUTTONUP &&
        app.swallowMiddleUp
    ) {
        app.swallowMiddleUp =
            false;

        return 1;
    }

    else if (
        message ==
            WM_RBUTTONDOWN &&
        app.leftDown &&
        app.dragWindow
    ) {
        if (
            app.overlayVisible
        ) {
            HideOverlay(app);
        } else {
            app.selectedZones =
                0;

            ShowOverlay(
                app,
                event->pt
            );
        }

        app.swallowRightUp =
            true;

        return 1;
    }

    else if (
        message ==
            WM_RBUTTONUP &&
        app.swallowRightUp
    ) {
        app.swallowRightUp =
            false;

        return 1;
    }

    else if (
        message ==
        WM_LBUTTONUP
    ) {
        if (
            app.overlayVisible
        ) {
            QueueSnap(app);
        }

        else if (
            app.restoreOnReleaseWindow ==
            app.dragWindow
        ) {
            QueueRestoreAfterMove(
                app,
                app.dragWindow,
                event->pt
            );
        }

        HideOverlay(app);

        app.leftDown =
            false;

        app.dragWindow =
            nullptr;

        app.restoreOnReleaseWindow =
            nullptr;
    }

    return CallNextHookEx(
        app.mouseHook,
        code,
        message,
        data
    );
}

LRESULT CALLBACK KeyboardHook(
    int code,
    WPARAM message,
    LPARAM data
) {
    if (
        code != HC_ACTION ||
        !g_app
    ) {
        return CallNextHookEx(
            nullptr,
            code,
            message,
            data
        );
    }

    AppState& app =
        *g_app;

    const auto* event =
        reinterpret_cast<
            KBDLLHOOKSTRUCT*
        >(data);

    const bool keyDown =
        message == WM_KEYDOWN ||
        message == WM_SYSKEYDOWN;

    const bool keyUp =
        message == WM_KEYUP ||
        message == WM_SYSKEYUP;

    int zoneNumber = -1;

    if (
        event->vkCode >= '1' &&
        event->vkCode <= '9'
    ) {
        zoneNumber =
            static_cast<int>(
                event->vkCode -
                '1'
            );
    }

    else if (
        event->vkCode >=
            VK_NUMPAD1 &&
        event->vkCode <=
            VK_NUMPAD9
    ) {
        zoneNumber =
            static_cast<int>(
                event->vkCode -
                VK_NUMPAD1
            );
    }

    //
    // ------------------------------------------------------------
    // Windows key bookkeeping
    // ------------------------------------------------------------
    //
    if (
        event->vkCode ==
        VK_LWIN
    ) {
        if (keyDown) {
            if (
                !app.leftWindowsDown &&
                !app.rightWindowsDown
            ) {
                app.windowsWheelUsed =
                    false;

                //
                // New Win gesture = absolutely no old cycle.
                //
                ResetWindowCycle(app);
            }

            app.leftWindowsDown =
                true;
        }

        if (keyUp) {
            app.leftWindowsDown =
                false;
        }
    }

    else if (
        event->vkCode ==
        VK_RWIN
    ) {
        if (keyDown) {
            if (
                !app.leftWindowsDown &&
                !app.rightWindowsDown
            ) {
                app.windowsWheelUsed =
                    false;

                ResetWindowCycle(app);
            }

            app.rightWindowsDown =
                true;
        }

        if (keyUp) {
            app.rightWindowsDown =
                false;
        }
    }

    //
    // IMPORTANT:
    //
    // Do NOT call GetAsyncKeyState/SyncWindowsKeyState here.
    //
    // We're currently inside the low-level key-up callback,
    // and Windows can still report the async key state as down.
    //
    if (
        keyUp &&
        (
            event->vkCode == VK_LWIN ||
            event->vkCode == VK_RWIN
        ) &&
        !app.leftWindowsDown &&
        !app.rightWindowsDown
    ) {
        const bool usedWheel =
            app.windowsWheelUsed;

        //
        // End the Win cycle unconditionally.
        //
        ResetWindowCycle(app);

        app.windowsWheelUsed =
            false;

        if (usedWheel) {
            SuppressStartMenuAfterWinWheel();
        }
    }

    const bool suspensionRelevant =
        app.overlayVisible ||
        event->vkCode == VK_LWIN ||
        event->vkCode == VK_RWIN ||
        event->vkCode == VK_OEM_3 ||
        event->vkCode == VK_ESCAPE ||
        zoneNumber >= 0;

    if (
        app.paused ||
        (
            suspensionRelevant &&
            ShouldSuspendForForeground(
                app
            )
        )
    ) {
        HideOverlay(app);
        ResetWindowCycle(app);

        return CallNextHookEx(
            app.keyboardHook,
            code,
            message,
            data
        );
    }

    //
    // ------------------------------------------------------------
    // Zone number shortcuts
    // ------------------------------------------------------------
    //
    if (
        zoneNumber >= 0
    ) {
        if (
            keyUp &&
            app.numberKeyDown[
                zoneNumber
            ]
        ) {
            app.numberKeyDown[
                zoneNumber
            ] =
                false;

            return 1;
        }

        if (
            keyDown &&
            app.leftDown &&
            app.dragWindow &&
            app.overlayVisible &&
            zoneNumber <
                static_cast<int>(
                    CurrentLayout(app)
                        .zones
                        .size()
                )
        ) {
            if (
                !app.numberKeyDown[
                    zoneNumber
                ]
            ) {
                app.numberKeyDown[
                    zoneNumber
                ] =
                    true;

                app.selectedZones =
                    0;

                app.hotZones =
                    1u <<
                    zoneNumber;

                PostMessageW(
                    app.dragWindow,
                    WM_CANCELMODE,
                    0,
                    0
                );

                QueueSnap(app);
                HideOverlay(app);
            }

            return 1;
        }
    }

    //
    // Compact overlay toggle.
    //
    if (
        event->vkCode ==
        VK_OEM_3
    ) {
        if (keyUp) {
            app.backtickDown =
                false;
        }

        if (
            app.leftDown &&
            app.dragWindow &&
            (
                keyDown ||
                keyUp
            )
        ) {
            if (
                keyDown &&
                !app.backtickDown
            ) {
                app.backtickDown =
                    true;

                app.compactMode =
                    !app.compactMode;

                if (
                    app.overlayVisible
                ) {
                    ShowOverlay(
                        app,
                        app.cursor
                    );
                }
            }

            return 1;
        }
    }

    if (
        keyDown &&
        event->vkCode ==
            VK_ESCAPE &&
        app.overlayVisible
    ) {
        HideOverlay(app);

        return 1;
    }

    return CallNextHookEx(
        app.keyboardHook,
        code,
        message,
        data
    );
}

void CALLBACK MoveSizeEventHook(
    HWINEVENTHOOK,
    DWORD event,
    HWND window,
    LONG objectId,
    LONG,
    DWORD,
    DWORD
) {
    if (!g_app) {
        return;
    }

    AppState& app =
        *g_app;

    if (
        objectId != OBJID_WINDOW ||
        !window ||
        window == app.overlay
    ) {
        return;
    }

    if (
        event ==
        EVENT_SYSTEM_MOVESIZESTART
    ) {
        if (
            ShouldSuspendForForeground(
                app
            ) ||
            IsWindowExcluded(
                app,
                window
            )
        ) {
            app.dragWindow =
                nullptr;

            HideOverlay(app);

            return;
        }

        app.dragWindow =
            GetAncestor(
                window,
                GA_ROOT
            );

        app.restoreOnReleaseWindow =
            app.savedWindows.contains(
                app.dragWindow
            )
                ? app.dragWindow
                : nullptr;
    }

    else if (
        event ==
            EVENT_SYSTEM_MOVESIZEEND &&
        !app.leftDown
    ) {
        app.dragWindow =
            nullptr;

        HideOverlay(app);
    }
}