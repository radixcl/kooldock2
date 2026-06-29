# KoolDock2

A dock for KDE Plasma 6 / Wayland, with macOS-style parabolic icon
magnification on hover. KoolDock2 is a ground-up port of the original
[KoolDock](https://sourceforge.net/projects/kooldock/) (KDE 3, 2003–2007) to
Qt 6, KDE Frameworks 6, QtQuick and `wlr-layer-shell` (via LayerShellQt).

## Features

### Core

- **Parabolic icon zoom** on hover, matching the original KoolDock
  magnification curve. Uses continuous integral-based sizing (no discrete
  wobble) with iterative re-centering so the biggest icon lands under the
  cursor.
- **Unified dock model**: pinned launchers and running window tasks merged
  into a single row.
- **All four screen edges** (bottom, top, left, right) via Wayland
  `wlr-layer-shell` (LayerShellQt).
- **Per-monitor placement** with configurable edge margin (positive to push
  away from the edge, negative to slide past other panels like the KDE
  taskbar).
- **Position** configurable as a percentage along the screen edge.
- **Single-instance** D-Bus service — launching again opens the running
  instance.

### Appearance

- **Translucent pill** with configurable background color, opacity, corner
  radius, and height.
- **Blur behind** the pill (KWin blur effect), with a configurable margin
  for a soft halo beyond the edge.
- **Optional border** with configurable color and width.
- **Glass pill resizes dynamically** to hug the current (possibly zoomed)
  icon span.
- **macOS-style auto-hide**: slides off the screen edge with scale + opacity
  + translate animation. Configurable show delay and animation speed.
- **Reserve screen space** like a panel when auto-hide is off (maximized
  windows won't cover the dock).

### Icons

- **Configurable rest size and zoomed size** (16–128 px / 32–256 px range).
- **Configurable zoom range**: 4–10 neighbouring icons participate in
  magnification.
- **Configurable icon spacing** (0–64 px) and **inner padding** (0–32 px).
- **Configurable zoom animation speed** (50–1000 ms).
- **macOS-style click bounce**: icons squash toward the screen edge on click
  and spring back.

### Tasks & Window Management

- **Show running windows** in the dock alongside launchers (toggleable).
- **Window grouping**: multiple windows of the same app stacked under one
  icon, with an indicator dot or numeric badge.
- **Configurable task dot**: size (0 = hidden, up to 16 px), color, and
  opacity.
- **Per-window context menu**: Minimize, Maximize, Keep Above/Below,
  Fullscreen, Shade, On All Desktops, Close.
- **New Window** and **per-app Desktop Actions** (e.g. "New Private Window"
  from LibreWolf).
- **Press-and-hold grouped icons** to peek windows via KWin's Window View.
- **Cycle through windows** by repeatedly clicking the icon.
- **Pin** standalone running tasks to the dock ("Keep in Dock").
- **Window title submenu** for grouped icons to target a specific window.
- **Minimize-to-dock animation**: KWin animates the window shrinking toward
  the dock icon (toggleable).
- **Task filtering**: current desktop only, minimized only, ignore list.
- **Notification highlighting**: icon marks when a background window notifies.
- **Drag file onto a launcher icon** to open it with that app ("open with").

### Launchers

- **Pin/unpin** apps by dragging `.desktop` files onto the dock, or via
  "Keep in Dock" / "Remove from Dock" in the context menu.
- **Drag-to-reorder** launcher icons with live insertion-gap preview.
- **Drag file onto trash icon** to move it to trash.
- **Drag icon off the dock** to remove it, with a poof burst animation.
- **AppMenu** (KDE application launcher) as an optional first dock item.
- **Auto-start on login** via autostart `.desktop` file (configurable).

### Tooltips

- **In-scene tooltips** (macOS-style) that show on hover — part of the dock
  surface, so the dock stays visible while reading them.
- **Configurable delay before show** (0–5000 ms) and **auto-hide timeout**
  (0–10000 ms, 0 = never).
- **Configurable font**, size, bold/italic, text color, and text shadow color.

### Trash

- **Trash icon** shows current state (empty/full).
- **Open Trash** and **Empty Trash** in right-click menu.

### Preferences

- **Tabbed GUI preferences dialog** (Behavior, Appearance, Icons, Tasks,
  Tooltips, About).
- Settings stored via KConfigXT in `~/.config/kooldockrc`.
- **Reload** and **Reset** without restarting.

## Status

This is an active, in-progress port — expect rough edges. Settings,
zoom/centering math, and window sizing have all had real bugs found and
fixed during the port; see [AGENTS.md](AGENTS.md) for the invariants that
keep them working if you're changing this code.

## Requirements

- Qt 6.6 or newer: Core, Gui, Quick, QuickControls2, Widgets, WaylandClient
- KDE Frameworks 6 (KF6): Config, CoreAddons, Crash, DBusAddons,
  WindowSystem, Service, Package, IconThemes, KIO, XmlGui, I18n,
  ColorScheme, Kirigami2
- [LayerShellQt](https://invent.kde.org/plasma/layer-shell-qt)
- Extra CMake Modules (ECM)
- A Wayland compositor with `wlr-layer-shell` support (KWin/Plasma 6 is the
  primary target)

## Building

```sh
cmake -B build
cmake --build build
```

## Running

```sh
./build/bin/kooldock2
```

Under Plasma Wayland, running the binary straight out of `build/bin/` will
show launchers but **no running window tasks**: KWin only grants the
`org_kde_plasma_window_management` protocol to a binary that's installed at
the path declared in `data/org.kde.kooldock2.desktop.cmake`, matched by
exact executable path. To see window tasks, install it first:

```sh
sudo cmake --install build
kbuildsycoca6
kooldock2   # now resolved from $PATH, e.g. /usr/bin/kooldock2
```

See [AGENTS.md](AGENTS.md) invariant #9 for why this is required.

Command-line options:

- `-o`, `--options` — open the preferences window on start.
- `-k`, `--kill` — quit any already-running instance (the app is a
  single-instance D-Bus service).

## Configuring

Right-click anywhere on the dock and choose **Edit Preferences**, or launch
with `--options`. Settings are stored in `~/.config/kooldockrc` using
KConfigXT; the schema lives in `data/kooldock.kcfg`.

## Project layout

```
src/      C++ backend (Qt/KF6): window management, task tracking, settings
qml/      UI (QtQuick): the dock itself and the preferences dialog
data/     KConfigXT schema (kooldock.kcfg)
themes/   Theme assets (work in progress, not yet wired up)
```

See [AGENTS.md](AGENTS.md) for a file-by-file map and the design invariants
behind the zoom/layout code.

## License

GPL-2.0-or-later, per the SPDX headers in each source file.
