# ZoneSmith

A native Win32 C++ proof of concept inspired by FancyZones.

## Controls

1. Start dragging a normal top-level window with the left mouse button.
2. While still holding the left button, right-click once to show the zone overlay.
   Right-click again before releasing to cancel and hide it.
3. Move over zone 1, 2, or 3 and release the left button to snap.
4. Scroll up over a zone to add it to the selection. Scroll down over a zone to
   remove it. Releasing stretches the window across all selected zones.
5. Drag that window again to restore its size from before the snap.
6. During a window drag, press the backtick key (`` ` ``) to toggle between the
   full-monitor overlay and a compact monitor map near the pointer.
7. Press `Ctrl+Alt+Q` to quit ZoneSmith. Press `Esc` to dismiss the overlay.

ZoneSmith also stays accessible from its notification-area icon. Right-click the
icon for usage instructions or to exit the app.

The three zones use the current monitor's working area, so the taskbar is not covered.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build -G Ninja
cmake --build build --config Release
```

Or run `make` if GNU Make is available. The executable is `build/ZoneSmith.exe`.

## POC limitations

- The layout is fixed to three equal columns.
- Elevated windows cannot be controlled unless ZoneSmith is also run elevated.
- The app uses a startup dialog and a global `Ctrl+Alt+Q` exit hotkey instead of a tray UI.
