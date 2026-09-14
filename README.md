# ZoneSmith

A native Win32 C++ proof of concept inspired by FancyZones.

## Controls

1. Start dragging a normal top-level window with the left mouse button.
2. While still holding the left button, right-click once to enter **Zone Mode** and
   show the zone overlay.
   Right-click again before releasing to cancel and hide it.
3. Move over zone 1, 2, or 3 and release the left button to snap.
4. Scroll up over a zone to add it to the selection. Scroll down over a zone to
   remove it. Releasing stretches the window across all selected zones.
5. Hold `Ctrl` and scroll while Zone Mode is visible to cycle through layouts.
6. Press `1` through `9` on the number row or numpad to snap immediately to that
   numbered zone.
7. Drag that window again to restore its size from before the snap.
8. During a window drag, press the backtick key (`` ` ``) to toggle between the
   full-monitor overlay and a compact monitor map near the pointer.
9. Press `Ctrl+Alt+Q` to quit ZoneSmith. Press `Esc` to dismiss Zone Mode.

ZoneSmith also stays accessible from its notification-area icon. Right-click the
icon for usage instructions, to open `layouts.ini`, or to exit the app.

The three zones use the current monitor's working area, so the taskbar is not covered.

## Layout configuration

Edit `layouts.ini` beside `ZoneSmith.exe` and restart the app. Each section is a
layout, and each `zoneN` value is `left,top,right,bottom` in percentages of the
monitor working area. Layouts may contain up to nine zones.

```ini
[Two Columns]
zone1=0,0,50,100
zone2=50,0,100,100
```

The included file provides Three Columns, Main and Stack, Two Columns, and Grid
2x2 layouts, plus 25/50/25, Quarters 4x2, and Thirds 3x2. CMake copies it into
the build folder automatically.

Hovering near a shared zone border highlights both zones. Releasing there spans
the combined area, and wheel selection adds or removes both zones together.

Outside a drag, hold the Windows key and use the normal vertical wheel anywhere
over a window. Every wheel step brings the next visible overlapping top-level
window to the front and activates it. Release the Windows key to end the cycle.

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
