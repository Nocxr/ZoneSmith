#include "App.h"

#include "Config.h"
#include "InputHooks.h"
#include "Overlay.h"
#include "OverlayWindow.h"
#include "WindowCycle.h"
#include "WindowUtils.h"

bool InitializeZoneSmith(
    AppState& app,
    HINSTANCE instance
) {
    g_app = &app;

    app.instance =
        instance;

    LoadLayouts(app);

    SyncWindowsKeyState(app);

    app.windowsWheelUsed =
        false;

    ResetWindowCycle(app);

    WNDCLASSEXW windowClass{
        sizeof(windowClass)
    };

    windowClass.hInstance =
        instance;

    windowClass.lpfnWndProc =
        ZoneSmithOverlayProc;

    windowClass.lpszClassName =
        kOverlayClass;

    windowClass.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );

    if (
        !RegisterClassExW(
            &windowClass
        ) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS
    ) {
        g_app = nullptr;
        return false;
    }

    app.overlay =
        CreateWindowExW(
            WS_EX_TOPMOST |
                WS_EX_TOOLWINDOW |
                WS_EX_LAYERED |
                WS_EX_TRANSPARENT |
                WS_EX_NOACTIVATE,

            kOverlayClass,

            L"ZoneSmith overlay",

            WS_POPUP,

            0,
            0,
            0,
            0,

            nullptr,
            nullptr,

            instance,

            nullptr
        );

    if (!app.overlay) {
        g_app = nullptr;
        return false;
    }

    SetLayeredWindowAttributes(
        app.overlay,
        0,
        190,
        LWA_ALPHA
    );

    app.mouseHook =
        SetWindowsHookExW(
            WH_MOUSE_LL,
            MouseHook,
            instance,
            0
        );

    if (!app.mouseHook) {
        ShutdownZoneSmith(app);
        return false;
    }

    app.keyboardHook =
        SetWindowsHookExW(
            WH_KEYBOARD_LL,
            KeyboardHook,
            instance,
            0
        );

    if (!app.keyboardHook) {
        ShutdownZoneSmith(app);
        return false;
    }

    app.moveSizeHook =
        SetWinEventHook(
            EVENT_SYSTEM_MOVESIZESTART,
            EVENT_SYSTEM_MOVESIZEEND,
            nullptr,
            MoveSizeEventHook,
            0,
            0,
            WINEVENT_OUTOFCONTEXT |
                WINEVENT_SKIPOWNPROCESS
        );

    if (!app.moveSizeHook) {
        ShutdownZoneSmith(app);
        return false;
    }

    return true;
}

void ShutdownZoneSmith(
    AppState& app
) {
    SaveCurrentMonitorLayout(
        app
    );

    HideOverlay(app);

    if (app.moveSizeHook) {
        UnhookWinEvent(
            app.moveSizeHook
        );

        app.moveSizeHook =
            nullptr;
    }

    if (app.keyboardHook) {
        UnhookWindowsHookEx(
            app.keyboardHook
        );

        app.keyboardHook =
            nullptr;
    }

    if (app.mouseHook) {
        UnhookWindowsHookEx(
            app.mouseHook
        );

        app.mouseHook =
            nullptr;
    }

    if (app.overlay) {
        DestroyWindow(
            app.overlay
        );

        app.overlay =
            nullptr;
    }

    if (app.instance) {
        UnregisterClassW(
            kOverlayClass,
            app.instance
        );
    }

    g_app = nullptr;
}
