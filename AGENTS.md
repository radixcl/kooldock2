# AGENTS.md

Guidance for AI coding agents working in this repository. Read the
"Critical invariants" section before touching `DockBar.qml` or `Main.qml` —
this codebase has already shipped several subtle, hard-to-spot regressions
in the zoom/centering math, each one found and fixed (or deliberately
reverted) the hard way.

## What this project is

KoolDock2: a Plasma 6 / Wayland dock with macOS-style parabolic icon zoom,
ported from a KDE 3-era C++/Qt 3 app to Qt 6 + KDE Frameworks 6 + QtQuick.
See [README.md](README.md) for features and build instructions.

## Build & verify

```sh
cmake -B build && cmake --build build
./build/bin/kooldock2          # run it
./build/bin/kooldock2 --options # jump straight to the preferences dialog
```

There is no automated test suite. For changes to the zoom/layout math in
`DockBar.qml`, prefer reasoning through (or quickly simulating in plain
Node.js) the geometry before editing QML — visual hover bugs are easy to
introduce and easy to miss just by reading the code. Several of the bugs
below were only caught this way.

## Architecture map

| File | Purpose |
|---|---|
| `src/main.cpp` | App entrypoint, `KAboutData`, CLI parsing, D-Bus single-instance setup |
| `src/kooldock.cpp`/`.h` | Singleton controller: owns the `QQuickView`, the `LayerShellQt::Window`, window sizing (`maxDockWidth()`/`maxDockHeight()`), blur region updates |
| `src/dockmodel.cpp`/`.h` | `QAbstractListModel` merging launchers (`LauncherItems`) and running windows (`WindowTasks`) for QML |
| `src/windowtasks.cpp`/`.h` | Task tracking facade: `KX11Extras`/`KWindowSystem` on X11, delegates to `WaylandWindowTasks` on Wayland |
| `src/waylandwindowtasks.cpp`/`.h` | Wayland task tracking via the `org_kde_plasma_window_management` protocol (qtwaylandscanner-generated client bindings) — see invariant #9 below before assuming this "just works" |
| `src/windowactions.cpp`/`.h` | minimize/maximize/close/etc. for the currently-targeted task |
| `src/launcheritems.cpp`/`.h` | Reads `.desktop` launcher files |
| `src/item.h` | `QObject` wrapper for one dock entry (launcher or task) |
| `data/kooldock.kcfg` + `src/kooldocksettings.kcfgc` | KConfigXT schema → generates `KoolDockSettings` |
| `data/org.kde.kooldock2.desktop.cmake` + `data/CMakeLists.txt` | Authorizes the privileged `org_kde_plasma_window_management` Wayland protocol for the installed binary — see invariant #9 |
| `qml/Main.qml` | Top-level window content: hover tracking, the "glass pill" background, window-level sizing |
| `qml/DockBar.qml` | The parabolic zoom math and per-icon layout |
| `qml/DockItem.qml` | One icon: size/position animation, icon image, tooltip, context-menu trigger |
| `qml/SettingsDialog.qml` | Preferences window |

## Critical invariants

1. **Hover position is read from `root` (`Main.qml`), never from the bar
   itself.** `DockBar`'s own on-screen position is *derived from*
   `contentWidth`, which depends on icon sizes, which depend on the mouse
   position. Reading the mouse relative to the bar's (or the background
   pill's) *live* position creates a feedback loop: moving the bar changes
   the local mouse coordinate, which changes the computed sizes/layout,
   which moves the bar again. This was tried and produced a measured,
   non-converging oscillation. `DockBar` must only ever receive stable
   inputs: `windowWidth` (the real window width — never affected by
   anything `DockBar` computes) and `globalMouseX` (position relative to
   `root`, which never moves).

2. **`contentWidth` must stay translation-invariant.** It's computed from
   the un-shifted cumulative-sum layout in `DockBar.layout()`. Don't fold
   any recentring/offset logic into it: `Main.qml` uses it to size the
   background pill, and `layout()` itself uses *last frame's* value to
   project the mouse into the bar's local coordinate frame. If it depended
   on something derived from the projected mouse position, that's the same
   feedback loop as #1, one step removed.

3. **The size parabola is gated by `containsMouse`, not just per-icon
   distance.** Icons jump straight to their zoomed size the instant the
   cursor enters the dock's hover margin, rather than fading in smoothly as
   it approaches. This is an intentional product decision (confirmed twice
   after being "fixed" into a smooth fade-in) — don't change it without
   checking first.

4. **The biggest icon must land under the cursor; this is fixed by
   iterating the size parabola, not by shifting.** The layout's
   `centers[i] = centers[i-1] + (size[i]+size[i-1])/2 + spacing` is a
   left-to-right cumulative sum, so every leftward neighbour that also
   grows shoves the zoomed icon rightward of its rest slot. If sizes are
   computed from the *rest* positions (a single pass), the visually-biggest
   icon ends up one slot to the right of the cursor — a subtle but real
   drift (~80px at default settings) that was noticed and reported.

   `DockBar.layout()` fixes this by **iterating the size computation
   against the running center estimate** (3 passes): pass 1 sizes off the
   rest positions (identical to the old single pass); pass 2+ re-sizes off
   each icon's *rendered* center from the previous pass. At convergence the
   icon that actually ends up under the cursor is the biggest. This needs
   **no positional shift**, so invariant #5 (no pill overflow) and #1–2
   (no bar-position feedback) both still hold — `localMouseX` is read once
   and held fixed across the passes, and `contentWidth` is still just the
   un-shifted cumulative-sum span (in fact slightly smaller than the
   single-pass value, since re-centering reduces the asymmetry). Converges
   in 2 passes; 3 is used for margin. Verified by a Node.js geometry sim
   across edge cases (first/last/middle/between icons) and multi-frame
   stability.

   Two earlier corrective approaches were tried and **reverted** — don't
   reintroduce either:
   - Shifting only the icons' positions → un-centers the icon group from
     the background pill on hover and overflows the pill's rounded edge
     (breaks #5).
   - Shifting the bar and the background pill together, as one rigid unit
     (`anchors.horizontalCenterOffset`) → fixed centering *and* the
     edge-overflow case, but was rejected end-to-end by the user.

   Don't collapse the iteration back to a single pass — that reintroduces
   the drift. Don't add a shift-based recentre either. If you touch the
   size/center math, re-run the geometry sim first (the bug is easy to miss
   by inspection: the size delta is only ~8px, the positional drift is the
   visible symptom).

5. **Icons can't overflow the background pill horizontally, by
   construction.** The cumulative sum starts icon 0 at exactly `spacing`
   and ends the last icon at exactly `contentWidth - spacing`, regardless of
   any icon's size. This holds because the layout uses **no positional
   shift** (see #4: the drift is fixed by iterating sizes, not by shifting
   positions) — if a corrective shift is reintroduced at the icon level,
   this guarantee breaks and icons hovered at the very first/last position
   can render outside the pill's rounded edge.

6. **`SizeRootObjectToView` vs `SizeViewToRootObject`.** The main dock view
   (`KoolDock::setupView()`) uses `SizeRootObjectToView`: the *window* size
   — set via `LayerShellQt::Window::setDesiredSize()`, computed in
   `maxDockWidth()`/`maxDockHeight()` — drives QML's `root.width/height`.
   QML's own `implicitWidth`/`implicitHeight` are inert there. The
   preferences dialog (`KoolDock::showPreferences()`) uses the opposite
   mode, where QML's implicit size *does* drive the window. Don't assume a
   QML width/height property controls the window without checking which
   mode applies to that view.

7. **kcfg settings need `GENERATE_MOC` and `GenerateProperties=true` to be
   usable from QML.** `kconfig_add_kcfg_files()` defaults to
   `SKIP_AUTOMOC`; without `GenerateProperties=true` the generated settings
   class only has `static` getter/setter methods, invisible to QML's
   meta-object system (calls silently fail, every read returns
   `undefined`). Both are already set (`src/CMakeLists.txt`,
   `src/kooldocksettings.kcfgc`). In QML, settings must be read/written via
   **property syntax** — `settings.autoHide = true` — not method calls
   (`settings.setAutoHide(true)`): the generated setters are wired as the
   property's WRITE accessor only, not as a separately invokable method.

8. **Icon `sourceSize` must stay fixed, independent of the live zoom
   size.** In `DockItem.qml`, `Image.sourceSize` is pinned to
   `maxIconSize` (the configured max icon size); only the *displayed*
   `width`/`height` track the animated zoom size. Binding `sourceSize` to
   the live size makes the `image://kicon/` provider re-render the icon
   from the theme on every animation frame — a real, visible flicker under
   load, not just a theoretical one.

9. **`org_kde_plasma_window_management` requires an installed `.desktop` file
   declaring `X-KDE-Wayland-Interfaces`, or KWin silently omits it.** KWin
   blacklists this protocol by default (along with `org_kde_kwin_fake_input`,
   `zkde_screencast_unstable_v1`, and a few others) and only advertises it to
   a client whose **installed** desktop file's `Exec=` resolves — via
   `QFileInfo::canonicalFilePath()`, with no `$PATH` search, so `Exec=` must
   be an absolute path — to that exact running binary, and which lists the
   interface name in `X-KDE-Wayland-Interfaces`. Without a match, the global
   is missing from the Wayland registry entirely (not merely inaccessible):
   `WaylandWindowTasks::start()` logs "not advertised by compositor" and the
   dock silently shows zero window tasks, with no error surfaced by KWin.
   This is why **running `./build/bin/kooldock2` directly never shows window
   tasks under Plasma Wayland** — only a binary installed at the path
   `data/org.kde.kooldock2.desktop.cmake` declares (`cmake --install build`,
   then `kbuildsycoca6`) is authorized. Also: on KF6/KService as packaged by
   at least Ubuntu, `X-KDE-Wayland-Interfaces` values must be comma-separated
   (`a,b`, matching `org.kde.plasmashell.desktop`) — a `;`-separated value is
   *not* split into a real list by `KService::property<QStringList>()` here
   and the check always fails.

## Settings wiring pattern

Geometry settings (icon sizes, spacing, zoom amount/speed) flow from the
global `settings` context property → `Main.qml` (`smallSize`, `bigSize`,
`zoomRange`, `spacing`, `zoomDuration`) → passed down explicitly as
properties to `DockBar`/`DockItem`. Leaf components don't read `settings`
directly. Follow this same chain for new geometry settings.

`KoolDock::maxDockWidth()`/`maxDockHeight()` (`kooldock.cpp`) mirror the
*same* formulas used in `DockBar.qml`'s `layout()`, to reserve enough window
space for the largest possible zoomed icon. If you change the zoom or
spacing math on the QML side, update both — this exact mismatch once
shipped and clipped the tops of fully-zoomed icons.

## Don't

- Don't reintroduce a discrete "snap to nearest icon" for any positional
  offset. An earlier version did this and produced a measured ~80px UI
  jump every time the mouse crossed between icons — the original bug this
  whole effort started from.
- Don't add a recentring/offset mechanism to `DockBar.qml`/`Main.qml`
  without re-deriving the stability argument in invariants #1–2 (ideally
  with a quick numeric simulation, not just inspection).
