# ZoneSmith

Windows snapping, zone overlays and window cycling, using the current core extracted from Dashboard.

The standalone executable is a tray app. Its menu provides pause, layout editing/reload, start-with-Windows and exit. Core behavior and the persistent layouts/state directory come from Dashboard's current implementation; the extraction does not rewrite user settings.

Build with `make`, launch with `make run`, and register the `zonesmith` command with `make install`. See [BUILDING.md](BUILDING.md) for the shared commands.

## Dashboard integration

`ZoneSmithCore` / `ZoneSmith::Core` owns the hooks, layout configuration, overlays, snapping and window cycling. It has no Dashboard or ImGui dependency. Dashboard pins this repo as `third_party/zonesmith`, links the core into its ZoneSmith module DLL, and owns the settings/plugin adapter. Set `ZONESMITH_BUILD_STANDALONE=OFF` when embedding.

Use either Dashboard's enabled module or the standalone app as the active hook host to avoid two sets of hooks responding to the same action. Existing user layouts and state are preserved by the current persistent-data implementation.

## Dynamic layout

`Dynamic` is a built-in layout at the end of the wheel-cycled layout list,
selected per monitor like any other layout. Static INI layouts are unchanged.

**Dragging.** Only a title-bar move starts a Dynamic drop; resizing a window
by its border never shows the overlay or changes the layout. Windows already
in the layout stay where they are until you release. The overlay shows every
tile as it will be after the drop, with the incoming window's tile filled; the
same computed layout is what gets applied on release.

**Where you hover picks the operation:**

| Hover | Options (middle-click to step through) |
| --- | --- |
| Outer 25% of a tile, nearest edge | Split that tile, then each larger region sharing that edge, up to the whole monitor |
| Centre of another tile | Swap (if the dragged window is already tiled), Stack |
| Centre of the dragged window's own tile | Return to tile |
| Empty Dynamic monitor | Fill monitor |

The default is always the narrowest scope (the hovered tile). Middle-click
widens it: for the right half of a side-by-side pair, hovering its bottom edge
offers *Split tile - bottom* and then *Split whole monitor - bottom* (a
full-width bottom row). The outline colour shows the scope: blue = one tile,
purple = several tiles, orange = whole monitor, green = swap, teal = stack.
Regions are those of the split tree, so combining tiles means choosing an
enclosing region (for example, a whole column).

Right-click during a drag hides the overlay; releasing then takes a tiled
window out of the layout and the others reclaim its space. Dropping on a
static-layout monitor does the same. Escape cancels the drag: the layout is
unchanged and a tiled window goes back to its tile.

A drop that would squeeze a window below its minimum size is shown in red with
the application's name and does nothing. ZoneSmith learns a window's minimum
size the first time it refuses to shrink into a tile.

**Dividers.** Hold Ctrl+Alt to show the dividers, then left-drag one to resize
the regions on either side. Resizing a tiled window by its border also works:
every divider that border touches follows it live, and the neighbouring tiles
adjust as you drag. Edges on the monitor boundary snap back on release. Dividers stop where a
window would go below its minimum size.

**Stacks.** Stacked tiles show their window count in the drop overlay and as a
badge while Ctrl+Alt is held. Win+wheel over a stack cycles its windows.

**Minimize, hide and close.** Minimized, hidden and cloaked windows release
their space but keep their logical position, so restoring one puts it back
where it was. Closed windows are removed. Changes are applied on window events,
with a 500 ms check as a backstop.

**Restarts.** Layouts are saved to the state file and restored when ZoneSmith
starts, matching saved tiles to open windows by executable, window class and
title (falling back to executable and class). Windows that aren't open at
startup are left out and the remaining tiles share their space. Saving pauses
when Windows ends the session, so apps closing during logoff don't erase the
layout.

Windows are placed by their visible frame, so the invisible resize borders of
Windows 10/11 don't leave gaps between tiles. Static zone snaps use the same
placement.

The layout model (`include/zonesmith/DynamicLayout.h`) is plain C++ with no
Win32 calls, and every operation validates its result before it is applied.
`make test` runs its unit tests.
