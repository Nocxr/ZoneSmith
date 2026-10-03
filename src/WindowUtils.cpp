#include "WindowUtils.h"

#include "Config.h"

#include <array>
#include <filesystem>
#include <dwmapi.h>

RECT VisibleWindowRect(HWND window) {
    RECT rect{};

    if (
        FAILED(
            DwmGetWindowAttribute(
                window,
                DWMWA_EXTENDED_FRAME_BOUNDS,
                &rect,
                sizeof(rect)
            )
        )
    ) {
        GetWindowRect(
            window,
            &rect
        );
    }

    return rect;
}

std::wstring WindowExecutableName(
    HWND window
) {
    DWORD processId{};

    GetWindowThreadProcessId(
        window,
        &processId
    );

    if (!processId) {
        return {};
    }

    HANDLE process =
        OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId
        );

    if (!process) {
        return {};
    }

    std::array<wchar_t, 32768> path{};

    DWORD length =
        static_cast<DWORD>(
            path.size()
        );

    const bool success =
        QueryFullProcessImageNameW(
            process,
            0,
            path.data(),
            &length
        ) != FALSE;

    CloseHandle(process);

    if (!success) {
        return {};
    }

    return Lowercase(
        std::filesystem::path(
            std::wstring(
                path.data(),
                length
            )
        ).filename().wstring()
    );
}

bool IsWindowExcluded(
    const AppState& app,
    HWND window
) {
    const std::wstring executable =
        WindowExecutableName(window);

    return
        !executable.empty() &&
        app.excludedApps.contains(
            executable
        );
}

bool IsFullscreenWindow(
    HWND window
) {
    if (
        !window ||
        window == GetShellWindow() ||
        !IsWindowVisible(window) ||
        IsIconic(window)
    ) {
        return false;
    }

    const RECT rect =
        VisibleWindowRect(window);

    MONITORINFO monitor{
        sizeof(monitor)
    };

    if (
        !GetMonitorInfoW(
            MonitorFromWindow(
                window,
                MONITOR_DEFAULTTONEAREST
            ),
            &monitor
        )
    ) {
        return false;
    }

    constexpr LONG tolerance = 2;

    return
        rect.left <=
            monitor.rcMonitor.left +
                tolerance &&

        rect.top <=
            monitor.rcMonitor.top +
                tolerance &&

        rect.right >=
            monitor.rcMonitor.right -
                tolerance &&

        rect.bottom >=
            monitor.rcMonitor.bottom -
                tolerance;
}

bool ShouldSuspendForForeground(
    const AppState& app
) {
    HWND foreground =
        GetForegroundWindow();

    return
        app.paused ||

        IsWindowExcluded(
            app,
            foreground
        ) ||

        (
            app.pauseInFullscreen &&
            IsFullscreenWindow(
                foreground
            )
        );
}

bool IsCycleWindow(
    const AppState& app,
    HWND window
) {
    return
        window &&
        window != app.overlay &&
        window != GetShellWindow() &&
        IsWindowVisible(window) &&
        !IsIconic(window) &&
        !IsWindowExcluded(
            app,
            window
        ) &&
        (
            GetWindowLongPtrW(
                window,
                GWL_EXSTYLE
            ) &
            WS_EX_TOOLWINDOW
        ) == 0;
}

bool IsPointOnWindowTitleBar(
    const AppState& app,
    HWND window,
    POINT point
) {
    if (
        !window ||
        !IsWindow(window)
    ) {
        return false;
    }

    window =
        GetAncestor(
            window,
            GA_ROOT
        );

    if (
        !window ||
        !IsCycleWindow(
            app,
            window
        )
    ) {
        return false;
    }

    const RECT rect =
        VisibleWindowRect(window);

    if (
        !PtInRect(
            &rect,
            point
        )
    ) {
        return false;
    }

    DWORD_PTR hitTestResult{};

    if (
        SendMessageTimeoutW(
            window,
            WM_NCHITTEST,
            0,
            MAKELPARAM(
                point.x,
                point.y
            ),
            SMTO_ABORTIFHUNG |
                SMTO_BLOCK,
            40,
            &hitTestResult
        )
    ) {
        const LRESULT hit =
            static_cast<LRESULT>(
                hitTestResult
            );

        if (hit == HTCAPTION) {
            return true;
        }

        if (
            hit == HTCLOSE ||
            hit == HTMINBUTTON ||
            hit == HTMAXBUTTON ||
            hit == HTSYSMENU
        ) {
            return false;
        }
    }

    int titleBarHeight =
        GetSystemMetrics(
            SM_CYCAPTION
        ) +
        GetSystemMetrics(
            SM_CYFRAME
        ) +
        GetSystemMetrics(
            SM_CXPADDEDBORDER
        );

    titleBarHeight =
        std::max(
            titleBarHeight,
            40
        );

    return
        point.y >= rect.top &&
        point.y <
            rect.top +
            titleBarHeight;
}

bool IsKeyDownNow(
    int virtualKey
) {
    return
        (
            GetAsyncKeyState(
                virtualKey
            ) &
            0x8000
        ) != 0;
}

bool IsWindowsKeyDownNow() {
    return
        IsKeyDownNow(VK_LWIN) ||
        IsKeyDownNow(VK_RWIN);
}

void SyncWindowsKeyState(
    AppState& app
) {
    app.leftWindowsDown =
        IsKeyDownNow(VK_LWIN);

    app.rightWindowsDown =
        IsKeyDownNow(VK_RWIN);
}