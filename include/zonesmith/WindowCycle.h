#pragma once

#include "AppState.h"

void ResetWindowCycle(
    AppState& app
);

bool BeginWindowCycle(
    AppState& app,
    HWND source
);

void AdvanceWindowPreview(
    AppState& app,
    HWND source,
    int direction
);

void CommitWindowPreview(
    AppState& app
);

bool HandleTitleBarHorizontalWheel(
    AppState& app,
    const MSLLHOOKSTRUCT* event
);