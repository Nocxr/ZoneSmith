#include "Snap.h"

#include "Overlay.h"
#include "WindowUtils.h"

#include <algorithm>
#include <cmath>

void QueueSnap(
    AppState& app
) {
    if (
        !app.dragWindow ||
        !IsWindow(app.dragWindow) ||
        (
            app.selectedZones == 0 &&
            app.hotZones == 0
        )
    ) {
        return;
    }

    if (
        !app.savedWindows.contains(
            app.dragWindow
        )
    ) {
        WINDOWPLACEMENT current{
            sizeof(WINDOWPLACEMENT)
        };

        GetWindowPlacement(
            app.dragWindow,
            &current
        );

        const RECT rect =
            VisibleWindowRect(
                app.dragWindow
            );

        app.savedWindows.emplace(
            app.dragWindow,
            SavedWindow{
                rect,
                current.showCmd ==
                    SW_SHOWMAXIMIZED
            }
        );
    }

    const unsigned int zones =
        app.selectedZones != 0
            ? (
                app.selectedZones |
                app.hotZones
            )
            : app.hotZones;

    app.pendingSnap =
        PendingSnap{
            app.dragWindow,
            ZonesRect(
                app,
                zones
            )
        };

    app.pendingRestore = {};

    app.restoreOnReleaseWindow =
        nullptr;

    SetTimer(
        app.overlay,
        kSnapTimerId,
        75,
        nullptr
    );
}

void QueueRestoreAfterMove(
    AppState& app,
    HWND window,
    POINT cursor
) {
    const auto found =
        app.savedWindows.find(
            window
        );

    if (
        found ==
        app.savedWindows.end()
    ) {
        return;
    }

    app.pendingSnap = {};

    app.pendingRestore =
        PendingRestore{
            window,
            SIZE{
                found->second.rect.right -
                    found->second.rect.left,

                found->second.rect.bottom -
                    found->second.rect.top
            },
            cursor
        };

    SetTimer(
        app.overlay,
        kSnapTimerId,
        75,
        nullptr
    );
}

void ApplyPendingOperation(
    AppState& app
) {
    KillTimer(
        app.overlay,
        kSnapTimerId
    );

    if (
        app.pendingSnap.window &&
        IsWindow(
            app.pendingSnap.window
        )
    ) {
        if (
            IsZoomed(
                app.pendingSnap.window
            )
        ) {
            ShowWindow(
                app.pendingSnap.window,
                SW_RESTORE
            );
        }

        const RECT target =
            app.pendingSnap.target;

        SetWindowPos(
            app.pendingSnap.window,
            nullptr,
            target.left,
            target.top,
            target.right -
                target.left,
            target.bottom -
                target.top,
            SWP_NOACTIVATE |
                SWP_NOZORDER
        );

        app.pendingSnap = {};

        return;
    }

    app.pendingSnap = {};

    if (
        app.pendingRestore.window &&
        IsWindow(
            app.pendingRestore.window
        )
    ) {
        const RECT current =
            VisibleWindowRect(
                app.pendingRestore.window
            );

        const LONG currentWidth =
            std::max(
                1L,
                current.right -
                    current.left
            );

        const double ratio =
            std::clamp(
                static_cast<double>(
                    app.pendingRestore.cursor.x -
                    current.left
                ) /
                    currentWidth,
                0.0,
                1.0
            );

        const int left =
            app.pendingRestore.cursor.x -
            static_cast<int>(
                std::round(
                    ratio *
                    app.pendingRestore.size.cx
                )
            );

        SetWindowPos(
            app.pendingRestore.window,
            nullptr,
            left,
            current.top,
            app.pendingRestore.size.cx,
            app.pendingRestore.size.cy,
            SWP_NOACTIVATE |
                SWP_NOZORDER
        );

        app.savedWindows.erase(
            app.pendingRestore.window
        );
    }

    app.pendingRestore = {};
}