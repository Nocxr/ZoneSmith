#pragma once

#include "AppState.h"

void QueueSnap(
    AppState& app
);

void QueueRestoreAfterMove(
    AppState& app,
    HWND window,
    POINT cursor
);

void ApplyPendingOperation(
    AppState& app
);