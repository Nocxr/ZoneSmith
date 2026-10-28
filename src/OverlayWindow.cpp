#include "OverlayWindow.h"

#include "AppState.h"
#include "Overlay.h"
#include "Snap.h"
#include "Dynamic.h"

LRESULT CALLBACK ZoneSmithOverlayProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (!g_app) {
        return DefWindowProcW(
            window,
            message,
            wParam,
            lParam
        );
    }

    AppState& app =
        *g_app;

    switch (message) {
    case kDividerMoveMessage:
        app.dynamicDividerMoveQueued=false;
        if(app.dynamicDividerNode>=0)
            MoveDynamicDividerDrag(app,app.dynamicDividerLatest);
        return 0;
    case kDividerEndMessage:
        app.dynamicDividerMoveQueued=false;
        if(app.dynamicDividerNode>=0) {
            MoveDynamicDividerDrag(app,app.dynamicDividerLatest);
            EndDynamicDividerDrag(app);
        }
        return 0;
    case WM_PAINT:
        PaintOverlay(
            app,
            window
        );
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_TIMER:
        if (wParam == kDynamicWindowEventTimerId) {
            KillTimer(window, kDynamicWindowEventTimerId);
            ReconcileDynamicWindowStates(app);
            return 0;
        }
        if (wParam == kDynamicReconcileTimerId) {
            ReconcileDynamicWindowStates(app);
            return 0;
        }
        if (
            wParam ==
            kSnapTimerId
        ) {
            ApplyPendingOperation(
                app
            );

            return 0;
        }
        break;
    }

    return DefWindowProcW(
        window,
        message,
        wParam,
        lParam
    );
}
