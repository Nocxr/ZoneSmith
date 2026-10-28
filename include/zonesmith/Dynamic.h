#pragma once
#include "AppState.h"

// Dynamic layouts are owned per monitor. Only explicitly docked windows participate.
bool IsDynamicLayout(const AppState& app);
RECT DynamicPreview(const AppState& app, POINT cursor);
void DockDynamicWindow(AppState& app, HWND window, POINT cursor);
bool StartDynamicDividerDrag(AppState& app, POINT cursor);
bool MoveDynamicDividerDrag(AppState& app, POINT cursor);
void EndDynamicDividerDrag(AppState& app);
void CommitDynamicDividerDrag(AppState& app);

// Frees the original tile when a real window move starts (not when it docks).
bool DetachDynamicWindow(AppState& app, HWND window);

// Click-through guide overlay while the Ctrl+Alt divider-edit gesture is active.
void UpdateDynamicDividerGuides(AppState& app, bool enabled);
void DestroyDynamicDividerGuides(AppState& app);

// Immediately release destroyed windows and reclaim their tiles.
void ReleaseDestroyedDynamicWindow(AppState& app, HWND window);

void ReconcileDynamicWindows(AppState& app);
