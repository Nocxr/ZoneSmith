# ZoneSmith

Windows snapping, zone overlays and window cycling, using the current core extracted from Dashboard.

The standalone executable is a tray app. Its menu provides pause, layout editing/reload, start-with-Windows and exit. Core behavior and the persistent layouts/state directory come from Dashboard's current implementation; the extraction does not rewrite user settings.

Build with `make`, launch with `make run`, and register the `zonesmith` command with `make install`. See [BUILDING.md](BUILDING.md) for the shared commands.

## Dashboard integration

`ZoneSmithCore` / `ZoneSmith::Core` owns the hooks, layout configuration, overlays, snapping and window cycling. It has no Dashboard or ImGui dependency. Dashboard pins this repo as `third_party/zonesmith`, links the core into its ZoneSmith module DLL, and owns the settings/plugin adapter. Set `ZONESMITH_BUILD_STANDALONE=OFF` when embedding.

Use either Dashboard's enabled module or the standalone app as the active hook host to avoid two sets of hooks responding to the same action. Existing user layouts and state are preserved by the current persistent-data implementation.
