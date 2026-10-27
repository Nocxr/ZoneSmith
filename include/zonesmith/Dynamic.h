#pragma once
#include "AppState.h"

// Dynamic layouts are owned per monitor. Only explicitly docked windows participate.
bool IsDynamicLayout(const AppState& app);
RECT DynamicPreview(const AppState& app, POINT cursor);
void DockDynamicWindow(AppState& app, HWND window, POINT cursor);
bool StartDynamicDividerDrag(AppState& app, POINT cursor);
bool MoveDynamicDividerDrag(AppState& app, POINT cursor);
void EndDynamicDividerDrag(AppState& app);
