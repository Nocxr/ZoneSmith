#pragma once

#include "AppState.h"

RECT ZoneRectInArea(
    const AppState& app,
    int zone,
    const RECT& area
);

RECT ZoneRect(
    const AppState& app,
    int zone
);

unsigned int ZonesAt(
    const AppState& app,
    POINT point
);

RECT ZonesRect(
    const AppState& app,
    unsigned int zones
);

void CycleLayout(
    AppState& app,
    int direction
);

void ShowOverlay(
    AppState& app,
    POINT point
);

void HideOverlay(
    AppState& app
);

void PaintOverlay(
    AppState& app,
    HWND window
);