# ZoneSmith

Windows snapping, zone overlays and window cycling, using the current core extracted from Dashboard.

The standalone executable is a tray app. Its menu provides pause, layout editing/reload, start-with-Windows and exit. Core behavior and the persistent layouts/state directory come from Dashboard's current implementation; the extraction does not rewrite user settings.

Build with `make`, launch with `make run`, and register the `zonesmith` command with `make install`. See [BUILDING.md](BUILDING.md) for the shared commands.

## Dashboard integration

`ZoneSmithCore` / `ZoneSmith::Core` owns the hooks, layout configuration, overlays, snapping and window cycling. It has no Dashboard or ImGui dependency. Dashboard pins this repo as `third_party/zonesmith`, links the core into its ZoneSmith module DLL, and owns the settings/plugin adapter. Set `ZONESMITH_BUILD_STANDALONE=OFF` when embedding.

Use either Dashboard's enabled module or the standalone app as the active hook host to avoid two sets of hooks responding to the same action. Existing user layouts and state are preserved by the current persistent-data implementation.

## Dynamic tiling POC (feature branch)

`Dynamic` is a built-in layout at the end of the normal wheel-cycled layout list.
Select it by dragging a window, opening ZoneSmith's overlay with right-click while dragging,
and scrolling the wheel until `Dynamic` appears. Once selected on a monitor,
the Dynamic drop overlay opens automatically when dragging a window there. Selection remains per monitor; existing
static INI layouts remain available.

With Dynamic active, drop the first window to fill the monitor's work area.
Subsequent drops split the occupied tile only within its outer 25% edge band.
The preview shows the exact resulting half; a centre drop undocks an already
tiled window instead of creating an unexpected split. Existing windows in that tile
shrink automatically. A dropped window must be explicitly docked; ZoneSmith
does not automatically tile unrelated windows.

To adjust a split, **hold Ctrl+Alt and left-drag the shared boundary** between
two tiled windows (within 12 pixels of the split); release to keep the ratio.
The POC uses the boundary as the slider, rather than placing permanent handles
on top of applications. Divider ratios persist while the app runs, but window
membership and split ratios are not yet saved across ZoneSmith restarts.
Closed windows are pruned when the next window is docked. Static layouts
and their persistence are unaffected.

### Dynamic POC input / lifecycle updates

While **Ctrl+Alt** are held, the dynamic split boundaries are displayed as
click-through cyan guide bars. The selected bar turns amber while dragging.
Ctrl+Alt+left-drag a highlighted bar to resize; window placement shortcuts are
suppressed during this gesture. Divider resize work is queued on ZoneSmith's
window thread rather than executed inside the low-level mouse hook.

Closing a tiled window immediately removes its leaf from the split tree and
reflows the surviving windows. The last remaining tile fills the monitor work
area. If an application enforces a large minimum window size, Windows may
still limit its actual resized geometry.
