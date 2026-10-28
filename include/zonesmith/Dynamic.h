#pragma once
#include "AppState.h"

// Win32 adapter for the pure layout model in DynamicLayout.h. Dynamic layouts
// are owned per monitor; only windows explicitly dropped into one take part.

bool IsDynamicLayout(const AppState& app);       // the overlay's current monitor
bool IsDynamicMonitorAt(const AppState& app, POINT point);
bool IsDynamicTiled(const AppState& app, HWND window);

// Drag lifecycle. Nothing on screen changes until the drop is committed.
void BeginDynamicDrag(AppState& app, HWND window, bool titleBarMove);
// Returns true when the preview differs from the one already painted.
bool UpdateDynamicDragPreview(AppState& app, POINT cursor);
void CycleDynamicDropOption(AppState& app);
void QueueDynamicDrop(AppState& app);
void QueueDynamicUndock(AppState& app, HWND window);
// Puts a tiled window back in its tile (cancelled drag).
void QueueDynamicReturn(AppState& app, HWND window);
// A tiled window was resized by its border: move the dividers it touched.
void QueueDynamicResize(AppState& app, HWND window);
// Returns true when a dynamic drop was handled (so static snapping is skipped).
bool ApplyPendingDynamicOperation(AppState& app);
void PaintDynamicOverlay(const AppState& app, HDC dc, RECT client);

bool StartDynamicDividerDrag(AppState& app, POINT cursor);
bool MoveDynamicDividerDrag(AppState& app, POINT cursor);
void EndDynamicDividerDrag(AppState& app);
void ShowDynamicDividerGuides(AppState& app, bool enabled);
void DestroyDynamicDividerGuides(AppState& app);

// Minimize/restore/close/hide handling. Hidden windows free their space but
// keep their logical position; destroyed windows are removed.
bool InstallDynamicWindowHooks(AppState& app);
void RemoveDynamicWindowHooks(AppState& app);
void ReconcileDynamicWindowStates(AppState& app);

// Layouts are saved to the state file and re-matched to open windows (by
// executable, class and title) at startup.
void RestoreDynamicLayouts(AppState& app);
void SaveDynamicLayouts(AppState& app);
