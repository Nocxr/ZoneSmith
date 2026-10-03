#include "WindowCycle.h"

#include "WindowUtils.h"

#include <dwmapi.h>

#include <algorithm>

namespace {

struct WindowCycleContext {
    AppState* app{};
    HWND source{};
    RECT sourceRect{};
    std::vector<HWND> windows;
};

void ClearCycleWindows(AppState& app) {
    app.cycleWindows.clear();
    app.cycleIndex = -1;
    app.windowCycleActive = false;
    app.windowCycleNeedsRetarget = false;
}

BOOL CALLBACK CollectOverlappingWindow(
    HWND window,
    LPARAM data
) {
    auto& context =
        *reinterpret_cast<WindowCycleContext*>(data);

    AppState& app =
        *context.app;

    if (
        window == app.overlay ||
        window == GetShellWindow() ||
        !IsWindowVisible(window) ||
        IsIconic(window) ||
        IsWindowExcluded(app, window) ||
        (
            GetWindowLongPtrW(
                window,
                GWL_EXSTYLE
            ) &
            WS_EX_TOOLWINDOW
        ) != 0
    ) {
        return TRUE;
    }

    DWORD cloaked{};

    if (
        SUCCEEDED(
            DwmGetWindowAttribute(
                window,
                DWMWA_CLOAKED,
                &cloaked,
                sizeof(cloaked)
            )
        ) &&
        cloaked != 0
    ) {
        return TRUE;
    }

    RECT rect{};

    if (!GetWindowRect(window, &rect)) {
        return TRUE;
    }

    RECT overlap{};

    if (
        !IntersectRect(
            &overlap,
            &context.sourceRect,
            &rect
        )
    ) {
        return TRUE;
    }

    const long long overlapArea =
        static_cast<long long>(
            overlap.right - overlap.left
        ) *
        (
            overlap.bottom - overlap.top
        );

    const long long candidateArea =
        static_cast<long long>(
            rect.right - rect.left
        ) *
        (
            rect.bottom - rect.top
        );

    if (
        candidateArea > 0 &&
        overlapArea * 100 >=
            candidateArea *
            app.windowOverlapPercent
    ) {
        context.windows.push_back(
            window
        );
    }

    return TRUE;
}

}

void ResetWindowCycle(
    AppState& app
) {
    ClearCycleWindows(app);

    app.horizontalCycleActive = false;
    app.horizontalCycleAnchor = {};
    app.horizontalCycleLastTick = 0;
    app.horizontalWheelRemainder = 0;
}

bool BeginWindowCycle(
    AppState& app,
    HWND source
) {
    // ALWAYS discard an old cycle before constructing
    // a new cycle group.
    ClearCycleWindows(app);

    if (
        !IsCycleWindow(
            app,
            source
        )
    ) {
        return false;
    }

    WindowCycleContext context{
        &app,
        source,
        VisibleWindowRect(source),
        {}
    };

    EnumWindows(
        CollectOverlappingWindow,
        reinterpret_cast<LPARAM>(
            &context
        )
    );

    if (
        context.windows.size() < 2
    ) {
        ClearCycleWindows(app);
        return false;
    }

    const auto current =
        std::find(
            context.windows.begin(),
            context.windows.end(),
            source
        );

    if (
        current ==
        context.windows.end()
    ) {
        ClearCycleWindows(app);
        return false;
    }

    app.cycleIndex =
        static_cast<int>(
            std::distance(
                context.windows.begin(),
                current
            )
        );

    app.cycleWindows =
        std::move(
            context.windows
        );

    app.windowCycleActive = true;
    app.windowCycleNeedsRetarget = false;

    return true;
}

void AdvanceWindowPreview(
    AppState& app,
    HWND source,
    int direction
) {
    if (!app.windowCycleActive) {
        if (
            !source ||
            !BeginWindowCycle(
                app,
                source
            )
        ) {
            return;
        }
    }

    const int count =
        static_cast<int>(
            app.cycleWindows.size()
        );

    if (count < 2) {
        ResetWindowCycle(app);
        return;
    }

    for (
        int attempt = 0;
        attempt < count;
        ++attempt
    ) {
        app.cycleIndex =
            (
                app.cycleIndex +
                direction +
                count
            ) %
            count;

        HWND selected =
            app.cycleWindows[
                app.cycleIndex
            ];

        if (
            !IsWindow(selected) ||
            !IsCycleWindow(
                app,
                selected
            )
        ) {
            continue;
        }

        SetWindowPos(
            selected,
            HWND_TOP,
            0,
            0,
            0,
            0,
            SWP_NOMOVE |
                SWP_NOSIZE |
                SWP_SHOWWINDOW
        );

        SetForegroundWindow(
            selected
        );

        BringWindowToTop(
            selected
        );

        return;
    }

    ResetWindowCycle(app);
}

void CommitWindowPreview(
    AppState& app
) {
    ResetWindowCycle(app);
}

bool HandleTitleBarHorizontalWheel(
    AppState& app,
    const MSLLHOOKSTRUCT* event
) {
    if (!event) {
        return false;
    }

    const DWORD now =
        GetTickCount();

    //
    // A pause between horizontal wheel gestures starts
    // an entirely fresh session.
    //
    if (
        app.horizontalCycleActive &&
        now -
            app.horizontalCycleLastTick >
            kHorizontalCycleTimeoutMs
    ) {
        ResetWindowCycle(app);
    }

    if (!app.horizontalCycleActive) {
        //
        // IMPORTANT:
        //
        // Throw away anything left by Win+wheel before
        // beginning horizontal cycling.
        //
        ResetWindowCycle(app);

        HWND source =
            WindowFromPoint(
                event->pt
            );

        if (!source) {
            return false;
        }

        source =
            GetAncestor(
                source,
                GA_ROOT
            );

        if (
            !source ||
            !IsPointOnWindowTitleBar(
                app,
                source,
                event->pt
            )
        ) {
            return false;
        }

        if (
            !BeginWindowCycle(
                app,
                source
            )
        ) {
            return false;
        }

        app.horizontalCycleActive =
            true;

        app.horizontalCycleAnchor =
            event->pt;

        app.horizontalCycleLastTick =
            now;

        app.horizontalWheelRemainder =
            0;
    }

    app.horizontalCycleLastTick =
        now;

    const SHORT delta =
        static_cast<SHORT>(
            HIWORD(
                event->mouseData
            )
        );

    app.horizontalWheelRemainder +=
        delta;

    const int threshold =
        WHEEL_DELTA *
        std::max(
            1,
            app.horizontalCycleNotches
        );

    while (
        app.horizontalWheelRemainder >=
        threshold
    ) {
        AdvanceWindowPreview(
            app,
            nullptr,
            1
        );

        app.horizontalWheelRemainder -=
            threshold;
    }

    while (
        app.horizontalWheelRemainder <=
        -threshold
    ) {
        AdvanceWindowPreview(
            app,
            nullptr,
            -1
        );

        app.horizontalWheelRemainder +=
            threshold;
    }

    return true;
}