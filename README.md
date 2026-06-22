# KoolDock2

A dock for KDE Plasma 6 / Wayland, with macOS-style parabolic icon
magnification on hover. KoolDock2 is a ground-up port of the original
[KoolDock](https://sourceforge.net/projects/kooldock/) (KDE 3, 2003–2007) to
Qt 6, KDE Frameworks 6, QtQuick and `wlr-layer-shell` (via LayerShellQt).

## Features

- Parabolic zoom: icons grow as the cursor approaches, following the same
  magnification curve as the original KoolDock.
- Launchers and running window tasks merged into a single dock.
- Auto-hide, with a configurable show delay.
- Translucent, blurred "glass pill" background that grows/shrinks to hug the
  current icon sizes.
- Anchored to any screen edge (bottom, top, left, right) via the Wayland
  layer-shell protocol.
- Configurable through a GUI preferences dialog (icon sizes, zoom amount and
  speed, spacing, blur, task filtering, tooltips).
- Per-icon context menu (minimize/maximize/close) for running windows.

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

## Credits

- Matias Fernandez (matias.fernandez@gmail.com) — KoolDock2 port
- Francisco Guidi, Blase Stanek — original KoolDock authors/maintainers
- Mauricio Bahamonde — original project webmaster
- Sebastian Sariego Benitez — icon and artwork (original KoolDock)
