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
   itself.** `DockBar`'s own on-screen position along the dock's long axis
   is *derived from* `contentLength`, which depends on icon sizes, which
   depend on the mouse position. Reading the mouse relative to the bar's
   (or the background pill's) *live* position creates a feedback loop:
   moving the bar changes the local mouse coordinate, which changes the
   computed sizes/layout, which moves the bar again. This was tried and
   produced a measured, non-converging oscillation. `DockBar` must only
   ever receive stable inputs: `windowExtent` (the window's real size
   along the long axis — width for Top/BottomEdge, height for
   Left/RightEdge, never affected by anything `DockBar` computes) and
   `globalMousePos` (cursor position along that same axis, relative to
   `root`, which never moves).

2. **`contentLength` must stay translation-invariant.** It's computed
   from the un-shifted cumulative-sum layout in `DockBar.layout()`. Don't
   fold any recentring/offset logic into it: `Main.qml` uses it to size
   the background pill along the long axis, and `layout()` itself uses
   *last frame's* value to project the mouse into the bar's local
   coordinate frame. If it depended on something derived from the
   projected mouse position, that's the same feedback loop as #1, one
   step removed. The 1D math is axis-neutral (verified by a Node.js
   geometry sim): horizontal and vertical edges feed the same function
   with different window dimensions / mouse coordinates and produce
   identical size/center sequences.

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

5. **Icons can't overflow the background pill along the long axis, by
   construction.** The cumulative sum starts icon 0 at exactly `spacing`
   and ends the last icon at exactly `contentLength - spacing`, regardless
   of any icon's size. This holds because the layout uses **no positional
   shift** (see #4: the drift is fixed by iterating sizes, not by shifting
   positions) — if a corrective shift is reintroduced at the icon level,
   this guarantee breaks and icons hovered at the very first/last position
   can render outside the pill's rounded edge. On the short axis, icons
   grow away from the screen edge into the reserved overflow space (which
   stays transparent); the pill itself is always exactly `bgHeight` wide
   on that axis.

6. **`SizeRootObjectToView` vs `SizeViewToRootObject`.** The main dock view
   (`KoolDock::setupView()`) uses `SizeRootObjectToView`: the *window* size
   — set via `LayerShellQt::Window::setDesiredSize()`, computed in
   `maxDockWidth()`/`maxDockHeight()` — drives QML's `root.width/height`.
   QML's own `implicitWidth`/`implicitHeight` are inert there. The
   preferences dialog (`KoolDock::showPreferences()`) uses the same mode
   (`SizeRootObjectToView`) with an explicit `resize()` call for the
   initial size, so QML layouts that fill the root adapt when the user
   resizes the dialog. Don't assume a
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

10. **`setDesiredSize` is the only resize API that avoids buffer-stretch
    flicker.** The Wayland layer-shell pipeline works as follows:
    `setDesiredSize()` tells the compositor the intended size *without*
    immediately resizing the QWindow.  The compositor sends a configure
    event; QtWayland acks it, renders a new buffer at the new size, and
    commits surface geometry + buffer together.  No frame ever shows a
    mismatched size → buffer stretch.

    Using `m_view->setMinimumSize/setMaximumSize` or `m_view->resize()`
    instead forces the QWindow to its new geometry *before* a matching
    buffer exists.  For one frame the compositor stretches the previous
    buffer to the new surface size — a visible vertical/horizontal
    stretch of every icon on every hover/tooltip/drag resize that was
    reported and fixed twice (commits 56a215f and 2fa6d26).

    On older distros (Ubuntu ≤ 25.04) `setDesiredSize` may not be
    declared in the LayerShellQt header.  The CMake in `src/CMakeLists.txt`
    detects this with `file(STRINGS … REGEX "setDesiredSize")` and
    falls back to `setMinimumSize/setMaximumSize` only on those systems.
    **Do not** remove the `#ifdef` or collapse the two paths into one:
    the fallback is acceptable only for CI artifacts; production builds
    on any distro shipping LayerShellQt ≥ 6.6.4 **must** use
    `setDesiredSize`.

11. **The blur region is pushed to KWin frame-aligned, rate-limited, and
    deduplicated — never from a free-running timer.** The dock window is
    full-screen and transparent except for the pill; if KWin ever blurs
    outside the pill's current bounds you get blurred desktop floating with
    nothing painted on it — a visible flash, near-full-screen on a wide
    dock. Two failure modes both produce this:
    - **Phase drift.** QML animates the pill geometry at 60 fps and pushes
      the region via `updateBlurRegion()`. If `enableBlurBehind()` runs on
      an independent `QTimer`, the timer and vsync drift in and out of
      phase and KWin periodically applies a region that doesn't match the
      committed buffer for a frame. Fix: `flushBlur()` is driven by
      `QQuickWindow::afterAnimating` (gui thread, once per frame, *after*
      QML advanced this frame's geometry and *before* the scene is
      synced/committed), so the region rides the same frame as its buffer.
      The `m_blurTimer` survives only as the trailing-flush fallback for
      when rendering goes idle before another `afterAnimating` fires.
    - **Regeneration churn.** Every `enableBlurBehind()` makes KWin
      regenerate the blurred backbuffer; it can't keep up at 60 fps and
      glitches. `flushBlur()` rate-limits to `kBlurIntervalMs` (~30 fps),
      and `applyBlur()` skips the call entirely when the computed region
      equals the last one pushed (`m_lastBlurRegion`/`m_lastBlurState`).

    Don't move the push back onto a bare timer, don't call
    `enableBlurBehind()` per frame, and don't drop the region dedup.
    `reconfigure()` must reset `m_lastBlurState = -1` because an edge/size
    change makes the cached region describe a different surface. A `<= 0`
    `m_blurLength` (QML hasn't pushed yet) must early-return leaving
    `m_blurDirty` set so a later frame retries — never fall back to
    `m_view->width()`, which blurs the full panel-sized window (commit
    a6c9943).

    **Known remaining limitation (KWin-side, not fixable from our region
    logic — June 2026 investigation).** A residual intermittent full-screen
    blur flash survives all of the above. It was traced to KWin itself, not
    our pushes. Established by experiment, so don't re-derive:
    - **Blur off → no flash; blur on → flash.** It is entirely the blur
      mechanism.
    - Our pushed region is **always sane** — instrumenting every
      `enableBlurBehind()` showed the long axis never exceeds ~0.5 of the
      window and the short axis is fixed. KWin blurs the *whole window* on
      its own; because the window is full-screen, that reads as full-screen.
    - **Freezing the region (no updates at all) eliminates the flash.** So
      the trigger is KWin re-applying the blur of a full-screen window when
      the *region changes* — a one-frame whole-window blur. Happens with
      auto-hide both on and off.
    - **Rate does not matter.** Dropping pushes to ~15 fps did not help (and
      added visible lag), so it is not a "KWin can't keep up" throttle issue.
    - The window **must stay full-screen** (`maxDockWidth/Height` = screen
      size): dynamic tooltips would be clipped (badly on vertical edges) and
      icons must be draggable out of the dock. Shrinking the window to
      confine the flash is therefore not viable.

    Already ruled out — do **not** chase these again: `m_blurTimer`
    phase-drift / stale region (its values are fine); the auto-hide slide
    path (flash occurs with auto-hide off too); push rate.

    **Attempted and failed:** updating the region in place on a persistent
    `KWayland::Client::Blur` (org_kde_kwin_blur) object instead of
    `KWindowEffects` — the clean way to avoid re-applying. The path activated
    and pushed regions but **KWin never rendered the blur** (mixing manual
    KWayland surface/blur proxies with QtWayland's own surface). Diagnosing
    it further needs `WAYLAND_DEBUG=1` protocol tracing. Realistic options
    left: accept the flash, or update the region only when the zoom *settles*
    (no flash, slight lag during active motion).

## Settings wiring pattern

Geometry settings (icon sizes, spacing, zoom amount/speed) flow from the
global `settings` context property → `Main.qml` (`smallSize`, `bigSize`,
`zoomRange`, `spacing`, `zoomDuration`) → passed down explicitly as
properties to `DockBar`/`DockItem`. Leaf components don't read `settings`
directly. Follow this same chain for new geometry settings.

`KoolDock::maxDockLongSize()`/`maxDockShortSize()` (`kooldock.cpp`) mirror
the *same* formulas used in `DockBar.qml`'s `layout()`, to reserve enough
window space for the largest possible zoomed icon. `maxDockWidth()` and
`maxDockHeight()` swap these per orientation (long axis = width on
horizontal edges, height on vertical edges). If you change the zoom or
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
- Don't replace `m_layer->setDesiredSize()` with `m_view->setMinimumSize`/
  `setMaximumSize` or `m_view->resize()` — see invariant #10.  This was
  done once (to fix a CI build on Ubuntu 25.04) and immediately
  reintroduced the buffer-stretch flicker that 56a215f had fixed.
- Don't push commits without an explicit user request.

## Context menu rules

Right-click behaviour by item type and running state:

| Item | Not running | Running |
|---|---|---|
| **Launcher** (pinned) | `dockMenu` — Edit Preferences, Reload Configuration, Quit | `contextMenu` — Desktop Actions, Minimize/Maximize/Close, Keep in Dock/Remove, etc. |
| **Task** (standalone, no launcher) | N/A | `contextMenu` |
| **Trash** | `trashMenu` — Open Trash, Empty Trash | N/A |
| **AppMenu** (KDE launcher) | No menu | N/A |
| **Empty bar area** | `dockMenu` | N/A |

A launcher dynamically switches between `dockMenu` (not running) and
`contextMenu` (running) based on `model.isRunning`. This is wired in
`DockBar.qml`'s `onContextMenuRequested` handler — do not change the
menu assignment without checking this invariant.

Window grouping adds a submenu listing individual window titles when
the right-clicked item has >1 window grouped (`bar.contextWindowList`).
This list is fetched fresh from the model in `showMenu()` via
`model.windowListForRow()`.
