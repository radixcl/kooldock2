import QtQuick
import QtQuick.Controls
import QtQml

Item {
    id: bar

    property var kooldock: null
    property int edge: Qt.BottomEdge
    property bool autoHide: false
    // Stable inputs from Main.qml: the window's extent along the dock's
    // long axis (width for Top/BottomEdge, height for Left/RightEdge — set
    // by the compositor/layer-shell, unrelated to anything we compute here)
    // and the cursor's position along that same axis in the window's
    // never-moving frame. We deliberately do NOT use the bar's own live
    // on-screen position to find the mouse: the bar stays centered at
    // extent/2 - contentLength/2, i.e. its position is itself an output of
    // this function, so using it as an input here would make the two chase
    // each other.
    property real windowExtent: 0
    property real windowCrossExtent: 0
    property real globalMousePos: 0
    property real globalCrossPos: 0

    // Cached copy of windowCrossExtent from the last time globalCrossPos
    // was updated. point.position only changes on actual pointer motion
    // (a compositor event), but windowCrossExtent (root.height/width)
    // changes on window resize — e.g. when a tooltip grows the window.
    // Between the resize and the next pointer event, globalCrossPos is
    // stale (old window-local coordinate) while windowCrossExtent is new.
    // Using them together in the cross-axis containsMouse check makes the
    // bound shift (newExtent - hoverSize) while the position doesn't,
    // producing a false "cursor left the dock" for a frame — which
    // triggers auto-hide/re-grow flicker. cachedCrossExtent stays
    // consistent with the (possibly stale) globalCrossPos, so the check
    // is stable across resizes. See layout() for the update logic.
    property real lastGlobalCrossPos: -100001
    property real cachedCrossExtent: 0

    property bool containsMouse: false
    property real contentLength: 0
    // Relayed up from whichever DockItem currently has a tooltip showing
    // (see DockItem.qml's tooltipExtent) — Main.qml forwards this to
    // KoolDock so the real window can grow to fit it. Only one tooltip is
    // ever visible at a time, so "last write wins" is correct here.
    property real tooltipExtent: 0
    // Largest rendered icon size from the *last* layout() pass — used to
    // size the cross-axis *entry* margin in layout() (the zone the cursor
    // must be within to trigger hover when not already hovering). Read
    // before this frame's pass overwrites it, same reasoning as
    // contentLength above. The "stay" margin (once already hovering) uses
    // bigSize instead — see the hysteresis comment in layout().
    property real lastMaxIconSize: smallSize

    readonly property bool vertical: edge === Qt.LeftEdge || edge === Qt.RightEdge
    // Geometry, fed by Main.qml from settings — defaults here only matter
    // before that binding resolves.
    property int smallSize: 48
    property int bigSize: 96
    property int zoomRange: 5
    property int spacing: 10
    property int zoomDuration: 200
    property bool showNames: true
    property int iconPadding: 4
    property int taskDotSize: 4
    property color taskDotColor: "#aaffffff"
    property real taskDotOpacity: 0.6
    property bool showWindowCountBadge: true
    property int tooltipDelay: 500
    property int tooltipTimeout: 2000
    property int tooltipSize: 12
    property bool tooltipBold: false
    property bool tooltipItalic: false
    property string tooltipFont: "Sans Serif"
    property color tooltipColor: "#f1f1f1"
    property color tooltipShadowColor: "#000000"
    property bool minimizeAnimation: true
    property bool windowPeekEnabled: true
    property int windowPeekDelay: 2000
    property bool trashIsEmpty: true

    signal emptyTrash()

    clip: false

    // Drag-and-drop state: which item is being dragged.
    property int dragIndex: -1
    // While reordering, the row index where the dragged item would land —
    // layout() reserves an empty slot there so the neighbours slide aside
    // (macOS "make space"). -1 means no reserved gap (idle, or the dragged
    // item is in the removal zone, in which case the rest close ranks).
    property int dropTarget: -1

    // External .desktop drag (dragging a new app onto the dock): while one is
    // hovering, layout() reserves an empty slot at externalDropTarget so the
    // icons part to preview where the new launcher will land — same "make
    // space" feel as reordering. -1 / false = no external drag in progress.
    property bool externalDrag: false
    property int externalDropTarget: -1

    // Open the insertion-preview gap for an external .desktop drag at the
    // given cursor position along the long axis (bar-local). Clamped to the
    // launcher block so the gap only appears among launchers.
    function externalDragMove(longPosLocal) {
        const iDist = bar.smallSize + bar.spacing
        let target = Math.round((longPosLocal - bar.spacing - bar.smallSize / 2) / iDist)
        let firstL = -1, lastL = -1
        for (let i = 0; i < listModel.count; i++) {
            if (listModel.get(i).isLauncher) { if (firstL < 0) firstL = i; lastL = i }
        }
        let lo, hi
        if (firstL < 0) {
            // No launchers yet: insert right after a leading AppMenu (or at 0).
            lo = 0
            for (let i = 0; i < listModel.count; i++)
                if (listModel.get(i).isAppMenu) lo = i + 1
            hi = lo
        } else {
            lo = firstL; hi = lastL + 1
        }
        target = Math.max(lo, Math.min(hi, target))
        externalClearTimer.stop()
        bar.externalDrag = true
        if (target !== bar.externalDropTarget) { bar.externalDropTarget = target; bar.layout() }
    }
    // Debounced: moving the drag between adjacent icons fires the old icon's
    // exit right before the new icon's enter; clearing immediately would flash
    // the gap shut for a frame. A short timer (cancelled by the next move)
    // collapses the gap only once the drag has really left the dock.
    function externalDragClear() { externalClearTimer.restart() }
    function externalDragReset() {
        externalClearTimer.stop()
        if (bar.externalDrag) {
            bar.externalDrag = false
            bar.externalDropTarget = -1
            bar.layout()
        }
    }
    Timer { id: externalClearTimer; interval: 120; onTriggered: bar.externalDragReset() }
    // True if a drag carries at least one .desktop URL (an app to add, as
    // opposed to a data file to open-with). Needs the URLs to be readable
    // mid-drag; if the platform withholds them until drop, this returns false
    // during motion and the gap simply won't preview (the drop still adds).
    function dragHasDesktop(drop) {
        if (!drop.hasUrls) return false
        const urls = drop.urls
        for (let i = 0; i < urls.length; i++)
            if (urls[i].toString().endsWith(".desktop")) return true
        return false
    }

    // Threshold (px) past the bar's edge for a drag to count as "outside"
    // the dock. Kept within the window's overflow area so removal triggers
    // before the cursor leaves the Wayland surface (where DragHandler stops
    // tracking it). On the short axis (perpendicular to the screen edge),
    // the overflow area is ~bigIconSize+spacing tall, so 30px is well
    // inside. On the long axis, 20px matches the previous behavior.
    readonly property int dragOutsideMarginLong: 20
    readonly property int dragOutsideMarginShort: 30

    // Returns true if the given scene coordinates (longPos along the dock's
    // long axis, crossPos along the short axis) are outside the bar's
    // bounds expanded by the margins above. Checks BOTH axes — the old
    // code only checked the long axis, so dragging an icon upward
    // (perpendicular to a BottomEdge dock) never triggered removal.
    function isOutsideDock(longPos, crossPos) {
        const barPos = bar.mapToItem(null, 0, 0)
        const barLong = vertical ? barPos.y : barPos.x
        const barCross = vertical ? barPos.x : barPos.y
        const barLen = vertical ? bar.height : bar.width
        const barCrossLen = vertical ? bar.width : bar.height

        const outsideLong = (longPos < barLong - dragOutsideMarginLong) ||
                            (longPos > barLong + barLen + dragOutsideMarginLong)
        const outsideCross = (crossPos < barCross - dragOutsideMarginShort) ||
                             (crossPos > barCross + barCrossLen + dragOutsideMarginShort)
        return outsideLong || outsideCross
    }

    ListModel { id: listModel }

    // Context menu state: which item triggered the menu, so the menu
    // items can show/hide based on the item type.
    property int contextMenuIndex: -1
    property var contextMenuItem: null
    // Point-in-time window state (maximized/keepAbove/.../onAllDesktops)
    // and per-app Desktop Actions, fetched fresh each time the menu opens
    // (see showMenu()) rather than kept as live model state — these are
    // only ever read while the menu is visible.
    property var contextMenuState: ({})
    property var contextMenuActions: []
    property var contextWindowList: []

    // True while any menu (context menu or dock-wide right-click menu) is
    // visible.  Main.qml feeds this into containsMouse so the dock stays
    // frozen — no auto-hide — for the entire lifetime of the menu, unlike
    // the dragActive heartbeat which expired after 500ms and broke the
    // freeze while the menu was still open.
    readonly property bool frozen: contextMenu.visible || dockMenu.visible || trashMenu.visible

    function showMenu(item, pt) {
        contextMenuIndex = item.modelIndex
        contextMenuItem = item
        contextMenuState = (item.windowId && bar.kooldock)
            ? bar.kooldock.windowActions.queryState(item.windowId) : ({})
        contextMenuActions = ((item.isLauncher || item.isTask) && bar.kooldock && bar.kooldock.model)
            ? bar.kooldock.model.desktopActions(item.modelIndex) : []
        contextWindowList = (item.windowId && bar.kooldock && bar.kooldock.model)
            ? bar.kooldock.model.windowListForRow(item.modelIndex) : []
        contextMenu.popup(pt)
    }
    function hideMenu() {
        contextMenuItem = null
        contextMenuIndex = -1
    }
    function showTrashMenu(pt) {
        trashMenu.popup(pt)
    }

    function refreshItems() {
        if (!kooldock || !kooldock.model) return
        const m = kooldock.model
        listModel.clear()
        for (let i = 0; i < m.count; i++) {
            const d = m.itemData(i)
            listModel.append({name: d.name, iconName: d.iconName, isTask: d.isTask,
                             isLauncher: d.isLauncher, isAppMenu: d.isAppMenu, isTrash: d.isTrash,
                             isRunning: d.isRunning, windowId: d.windowId, windowCount: d.windowCount,
                             itemIndex: d.itemIndex, badgeCount: d.badgeCount, sz: smallSize, ipos: 0})
        }
        layout()
    }

    function layout() {
        const N = listModel.count
        if (N === 0) return

        // While an item is being dragged for reorder, lay the row out at
        // rest size (no magnification — keeps the slot grid stable so the
        // drop target doesn't chase the cursor) and reserve an empty slot
        // at dropTarget. Only the *other* items are repositioned; the
        // dragged item keeps its ipos so its cursor-relative dragOffset
        // stays consistent and it doesn't jitter as the gap opens. It
        // floats over the gap and animates into it on release.
        if (bar.dragIndex >= 0) {
            // Keep the dock from auto-hiding mid-drag (the window is also
            // force-expanded via setDragExpanded for the same reason).
            bar.containsMouse = true
            const dIDist = smallSize + spacing
            const dt = bar.dropTarget   // -1 => removal zone, no gap
            let vp = 0
            for (let r = 0; r < N; r++) {
                // Rest size for every icon during a drag — including the
                // dragged one, so it lifts at a consistent size that matches
                // the gap it drops into. Only its *position* is left alone
                // (it floats under the cursor via dragOffset); repositioning
                // it here would fight the cursor follow.
                listModel.setProperty(r, "sz", smallSize)
                if (r === bar.dragIndex) continue
                if (vp === dt) vp++     // skip the reserved gap slot
                listModel.setProperty(r, "ipos", spacing + vp * dIDist)
                vp++
            }
            // Hold contentLength fixed for the whole drag (always N slots,
            // even in the removal zone) so the bar never re-centres mid-drag
            // — a moving bar drags the floating icon off the cursor between
            // pointer events.
            bar.contentLength = spacing + N * dIDist
            bar.lastMaxIconSize = smallSize
            return
        }

        // External .desktop drag: lay all N icons out at rest size and reserve
        // one extra empty slot at externalDropTarget so the row parts to show
        // where the dropped app will land. N+1 slots, so the pill grows by one.
        if (bar.externalDrag) {
            bar.containsMouse = true
            const dIDist = smallSize + spacing
            const dt = bar.externalDropTarget
            let vp = 0
            for (let r = 0; r < N; r++) {
                listModel.setProperty(r, "sz", smallSize)
                if (vp === dt) vp++   // leave the gap slot empty for the incoming icon
                listModel.setProperty(r, "ipos", spacing + vp * dIDist)
                vp++
            }
            bar.contentLength = spacing + (N + 1) * dIDist
            bar.lastMaxIconSize = smallSize
            return
        }

        const W = (smallSize + spacing) * zoomRange / 2
        const H = bigSize - smallSize
        const iDist = smallSize + spacing

        // The bar stays centred within the window.  When the mouse first
        // enters and icons zoom, contentLength grows, shifting the bar
        // left by half the growth.  Computing localMousePos with rest
        // contentLength makes the cursor appear left of where it really
        // is, so the zoom peak lands one slot to the left.  Save the
        // pre-transition hover state so we can re-run with the corrected
        // bar position after contentLength is known (see end of layout()).
        const wasHovering = bar.containsMouse

        // Project the cursor onto the bar's [0, contentLength] frame using
        // *last* layout's contentLength (read here, before it's overwritten
        // below) — see the comment on windowExtent/globalMousePos above for
        // why. This is axis-neutral: for horizontal edges windowExtent is
        // the window's width and globalMousePos is point.x; for vertical
        // edges they're the window's height and point.y. The 1D math is
        // identical either way (verified by a Node.js geometry sim).
        const barNearEdge = windowExtent / 2 - bar.contentLength / 2
        const localMousePos = globalMousePos - barNearEdge
        // Long-axis check: the input mask (trigger strip) already restricts
        // the hover area to the pill's unzoomed footprint along this axis.
        // A small margin around the bar lets the cursor dip slightly past
        // the edges without the dock immediately hiding.
        const longAxisMargin = autoHide ? (spacing * 2) : 0
        const inLongAxis = localMousePos > -longAxisMargin && localMousePos < bar.contentLength + longAxisMargin
        // Short-axis check: cursor must be within the icon zone (from the
        // screen edge to bigSize + spacing, the tallest zoomed icon). Without
        // this, moving the cursor vertically off the dock while staying within
        // its horizontal span kept containsMouse true and the dock "active".
        const crossMargin = spacing
        const inCrossAxis = vertical
            ? (globalCrossPos > -crossMargin && globalCrossPos < bar.width + crossMargin)
            : (globalCrossPos > -crossMargin && globalCrossPos < bar.height + crossMargin)
        // For the short-axis check, the icon zone extends from the anchored
        // screen edge outward by crossHoverSize+spacing. Hysteresis on the
        // hover margin: once containsMouse is true, use bigSize (the
        // maximum possible icon size) so the zone can't shrink below the
        // cursor when icons unzoom — e.g. when the cursor moves between
        // icons along the long axis, the previously-zoomed icon collapses
        // and lastMaxIconSize drops, which without hysteresis makes the
        // hover zone retract past a cursor that was near the top of that
        // icon. containsMouse flips false for a few frames until the cursor
        // reaches the next icon and zooms it again, producing a visible
        // up/down flicker (and, in autohide, triggering the hide timer).
        // When containsMouse is false, keep lastMaxIconSize for a
        // responsive entry zone — the cursor must be within the current
        // icon height to trigger hover, not the theoretical max. For
        // Top/LeftEdge the edge is at coordinate 0; for Bottom/RightEdge
        // the edge is at cachedCrossExtent.
        //
        // cachedCrossExtent (not the live windowCrossExtent) is used here
        // because a tooltip grow resizes the window along this axis: the
        // compositor grows the surface away from the anchored edge, so
        // root.height updates immediately but point.position.y — the
        // cursor's local coordinate — doesn't change until the next
        // pointer-motion event arrives. Using the new extent with the
        // stale position shifts the bound upward, making the check fail
        // for a frame even though the cursor never moved relative to the
        // anchored edge. cachedCrossExtent is only refreshed when
        // globalCrossPos actually changes (pointer moved), so it stays
        // consistent with the (possibly stale) position.
        const atTopOrLeft = edge === Qt.TopEdge || edge === Qt.LeftEdge
        if (globalCrossPos !== bar.lastGlobalCrossPos) {
            bar.lastGlobalCrossPos = globalCrossPos
            bar.cachedCrossExtent = windowCrossExtent
        }
        const crossHoverSize = bar.containsMouse ? bar.bigSize : bar.lastMaxIconSize
        bar.containsMouse = inLongAxis && (
            atTopOrLeft
                ? (globalCrossPos > -crossMargin && globalCrossPos < crossHoverSize + spacing + crossMargin)
                : (globalCrossPos > bar.cachedCrossExtent - crossHoverSize - spacing - crossMargin &&
                   globalCrossPos < bar.cachedCrossExtent + crossMargin))

        // ---- Continuous (macOS-style) magnification ----------------------
        // The obvious approach — sample the parabola at each icon's centre
        // and cumulatively sum the discrete sizes to get positions and the
        // total width — ripples: a parabola sampled at discrete points that
        // move relative to the cursor does not sum to a constant. As the
        // peak passes between two icons the sum wobbles (period = iDist),
        // which shows up as the pill's edges twitching and the surrounding
        // icons jittering. The parabola's *continuous integral*, by
        // contrast, is translation-invariant.
        //
        // So treat magnification as a continuous field: take icon sizes from
        // the parabola at each icon's FIXED rest centre, and take positions
        // from the analytic integral of that field (the extra width to the
        // left of each icon), not from a discrete sum. Both are smooth
        // functions of the cursor, so the pill and the icons stop wobbling
        // while the curve and the zoom feel stay the same.

        // Parabola bump (extra px over smallSize) at rest-coordinate x for a
        // peak at rest-coordinate c. Zero outside [c-W, c+W].
        const bumpAt = (x, c) => {
            const d = (x - c) / W
            return Math.abs(d) < 1 ? H * (1 - d * d) : 0
        }
        // Analytic ∫ bump dt from 0 to x (the bump is non-zero only on
        // [c-W, c+W]; clamp the window to that and to t >= 0). Primitive of
        // H(1 - ((t-c)/W)^2) is H[(t-c) - (t-c)^3/(3W^2)].
        const bumpIntegral = (x, c) => {
            const lo = Math.max(0, c - W)
            const hi = Math.min(x, c + W)
            if (hi <= lo) return 0
            const prim = t => { const u = t - c; return H * (u - (u * u * u) / (3 * W * W)) }
            return prim(hi) - prim(lo)
        }

        const u0 = spacing + smallSize / 2          // icon 0's rest centre
        const restCenter = i => u0 + i * iDist       // fixed rest centres

        // The cursor's position in the *rest* frame, c. It maps to the
        // rendered frame (where localMousePos lives) by adding the extra
        // width to its left; the row is then re-pinned to `spacing`, which
        // shifts everything by -leftExtra(u0). Solve c for
        // rendered(c) = localMousePos by a few fixed-point passes — the
        // extra-width terms are smooth and mild, so this converges fast.
        // (Per-icon density: the continuous integral is divided by iDist to
        // match what the discrete per-icon sum would have totalled.)
        let c = localMousePos
        if (containsMouse) {
            for (let it = 0; it < 4; it++)
                c = localMousePos - (bumpIntegral(c, c) - bumpIntegral(u0, c)) / iDist
        }

        // Sizes from the field at fixed rest centres; centres from u_i plus
        // the extra width to the left. leftExtra integrates the bump up to
        // the icon's own centre, so by the midpoint rule it already includes
        // half of this icon's own growth — adding e/2 on top would double-
        // count it, pushing icons right by an amount that grows on the
        // ascending side of the peak and shrinks on the descending side,
        // which crowded the right-hand icons into a visible dip.
        const sizes = []
        const centers = []
        for (let i = 0; i < N; i++) {
            const u = restCenter(i)
            const e = containsMouse ? bumpAt(u, c) : 0
            const leftExtra = containsMouse ? bumpIntegral(u, c) / iDist : 0
            sizes[i] = smallSize + e
            centers[i] = u + leftExtra
        }

        // Re-pin the row flush at `spacing` from the pill's left edge so it
        // stays centred within contentLength (Main.qml centres the pill on
        // screen), same guarantee the old cumulative layout gave for free.
        const shift = spacing + sizes[0] / 2 - centers[0]
        for (let i = 0; i < N; i++) centers[i] += shift

        // Total span (smooth — from the integral, not a rippling sum), used
        // by Main.qml to grow the background pill to hug the icons.
        bar.contentLength = centers[N - 1] + sizes[N - 1] / 2 + spacing

        // First frame of hover: barNearEdge was computed with the rest
        // contentLength, which makes localMousePos appear ~(Δlength/2) px
        // left of the true position.  Re-run with the now-correct zoomed
        // contentLength so the biggest icon lands under the cursor.  Only
        // one extra pass — wasHovering is already true in the re-entrant
        // call so there's no infinite recursion.
        if (!wasHovering && bar.containsMouse) {
            layout()
            return
        }

        // Feeds next frame's cross-axis entry margin above — see the
        // property declaration for why this needs to track the current
        // zoom instead of staying pinned to bigSize.
        bar.lastMaxIconSize = Math.max(smallSize, ...sizes)

        for (let i = 0; i < N; i++) {
            listModel.setProperty(i, "sz", sizes[i])
            listModel.setProperty(i, "ipos", centers[i] - sizes[i] / 2)
        }
    }

    // Set by onItemRemoved/onItemChanged/onItemInserted/onItemMoved once
    // they've already applied their update to listModel in place. Each of
    // those is always followed by itemsChanged (see dockmodel.cpp) —
    // without this guard, that itemsChanged would immediately call
    // refreshItems(), clearing and rebuilding the whole list. New
    // delegates start out at itemPos 0 before layout() lands them, so the
    // rebuild made every icon visibly fly in from the left instead of
    // just sliding over to fill a gap/make room, updating in place (e.g.
    // an icon's running-task dot toggling when its window opens/closes —
    // the common case for pinned launchers, which doesn't touch row
    // count at all), or reordering on an internal drag.
    property bool partialUpdateHandled: false

    Connections {
        target: kooldock && kooldock.model ? kooldock.model : null
        function onItemsChanged() {
            if (bar.partialUpdateHandled) { bar.partialUpdateHandled = false; return }
            bar.refreshItems()
        }
        function onCountChanged() {
            // If count went down by one, itemRemoved already handled it.
            // For other cases (reload, settings change), do a full refresh.
            if (listModel.count !== kooldock.model.count) bar.refreshItems()
        }
        function onItemRemoved(row) {
            // Remove just the one row from the ListModel without recreating
            // every delegate — avoids the "reappear from left to right" flash.
            if (row >= 0 && row < listModel.count) {
                listModel.remove(row)
                // Refresh itemIndex for the shifted items.
                for (let i = row; i < listModel.count; i++) {
                    const d = bar.kooldock.model.itemData(i)
                    listModel.setProperty(i, "itemIndex", d.itemIndex)
                }
                bar.layout()
                bar.partialUpdateHandled = true
            }
        }
        function onItemChanged(row) {
            // A single item's properties changed in place (fuse/unfuse on
            // window open/close) — row count is unaffected, so just patch
            // that one row's fields instead of rebuilding every delegate.
            if (row >= 0 && row < listModel.count) {
                const d = bar.kooldock.model.itemData(row)
                listModel.setProperty(row, "name", d.name)
                listModel.setProperty(row, "iconName", d.iconName)
                listModel.setProperty(row, "isTask", d.isTask)
                listModel.setProperty(row, "isLauncher", d.isLauncher)
                listModel.setProperty(row, "isAppMenu", d.isAppMenu)
                listModel.setProperty(row, "isTrash", d.isTrash)
                listModel.setProperty(row, "isRunning", d.isRunning)
                listModel.setProperty(row, "windowId", d.windowId)
                listModel.setProperty(row, "windowCount", d.windowCount)
                listModel.setProperty(row, "itemIndex", d.itemIndex)
                listModel.setProperty(row, "badgeCount", d.badgeCount)
                bar.partialUpdateHandled = true
            }
        }
        function onItemInserted(row) {
            // A new task icon appeared (window opened for an app that
            // isn't already a pinned launcher) — insert just that row so
            // existing icons slide over to make room instead of every
            // delegate being torn down and rebuilt.
            if (row >= 0 && row <= listModel.count) {
                const d = bar.kooldock.model.itemData(row)
                listModel.insert(row, {name: d.name, iconName: d.iconName, isTask: d.isTask,
                                  isLauncher: d.isLauncher, isAppMenu: d.isAppMenu, isTrash: d.isTrash,
                                  isRunning: d.isRunning, windowId: d.windowId, windowCount: d.windowCount,
                                  itemIndex: d.itemIndex, badgeCount: d.badgeCount, sz: smallSize, ipos: 0})
                // Refresh itemIndex for the items shifted after the new one.
                for (let i = row + 1; i < listModel.count; i++) {
                    const dd = bar.kooldock.model.itemData(i)
                    listModel.setProperty(i, "itemIndex", dd.itemIndex)
                }
                bar.layout()
                bar.partialUpdateHandled = true
            }
        }
        function onItemMoved(from, to) {
            // Drag-to-reorder within the dock — same row count, just a
            // different order, so move the one row instead of rebuilding.
            if (from >= 0 && from < listModel.count && to >= 0 && to < listModel.count && from !== to) {
                listModel.move(from, to, 1)
                const lo = Math.min(from, to)
                const hi = Math.max(from, to)
                for (let i = lo; i <= hi; i++) {
                    const d = bar.kooldock.model.itemData(i)
                    listModel.setProperty(i, "itemIndex", d.itemIndex)
                }
                bar.layout()
                bar.partialUpdateHandled = true
            }
        }
    }

    Component.onCompleted: refreshItems()
    onKooldockChanged: refreshItems()
    onGlobalMousePosChanged: { if (!bar.frozen) layout() }
    onWindowExtentChanged: { if (!bar.frozen) layout() }
    // Geometry settings changed (Apply/OK in the preferences dialog) — sizes
    // feed into every icon's rest size in the model, so go through
    // refreshItems() rather than just layout().
    onSmallSizeChanged: refreshItems()
    onBigSizeChanged: refreshItems()
    onZoomRangeChanged: layout()
    onSpacingChanged: refreshItems()

    // DropArea for external .desktop file drops to add launchers.  Placed
    // before the Repeater so it sits below the DockItem delegates in the
    // visual stacking order — this lets the trash DropArea (inside DockItem)
    // capture drops first, while non-trash areas fall through to this one.
    DropArea {
        anchors.fill: parent
        enabled: true
        keys: ["text/uri-list"]

        // Keep the dock from auto-hiding while a drag hovers the empty bar area
        // (held still has no movement to refresh the dragActive heartbeat).
        onContainsDragChanged: { if (bar.kooldock) bar.kooldock.setIconDragOver(containsDrag) }
        onEntered: (drop) => {
            if (bar.kooldock) bar.kooldock.setDragActive(true)
            if (bar.dragHasDesktop(drop)) bar.externalDragMove(vertical ? drop.y : drop.x)
        }
        onPositionChanged: (drop) => {
            if (bar.kooldock) bar.kooldock.setDragActive(true)
            if (bar.dragHasDesktop(drop)) bar.externalDragMove(vertical ? drop.y : drop.x)
        }
        onExited: bar.externalDragClear()
        onDropped: (drop) => {
            const target = bar.externalDropTarget   // capture before clearing
            bar.externalDragReset()
            if (drop.hasUrls) {
                const urls = drop.urls
                for (let i = 0; i < urls.length; i++) {
                    const url = urls[i]
                    if (url.toString().endsWith(".desktop")) {
                        const localFile = url.toString().replace("file://", "")
                        if (localFile.length > 0 && bar.kooldock && bar.kooldock.model)
                            bar.kooldock.model.addLauncherAt(localFile, target)
                    }
                }
                drop.accept()
            }
        }
    }

    Repeater {
        model: listModel
        delegate: DockItem {
            id: delegateItem
            name: model.name
            iconName: model.iconName
            isTask: model.isTask
            isLauncher: model.isLauncher
            isAppMenu: model.isAppMenu
            isTrash: model.isTrash
            isRunning: model.isRunning
            windowId: model.windowId
            windowCount: model.windowCount
            badgeCount: model.badgeCount
            kooldock: bar.kooldock
            modelIndex: model.itemIndex
            itemSize: model.sz
            itemPos: model.ipos
            edge: bar.edge
            containsMouse: bar.containsMouse
            maxIconSize: bar.bigSize
            zoomDuration: bar.zoomDuration
            showNames: bar.showNames
            iconPadding: bar.iconPadding
            taskDotSize: bar.taskDotSize
            taskDotColor: bar.taskDotColor
            taskDotOpacity: bar.taskDotOpacity
            showWindowCountBadge: bar.showWindowCountBadge
            tooltipDelay: bar.tooltipDelay
            tooltipTimeout: bar.tooltipTimeout
            tooltipSize: bar.tooltipSize
            tooltipBold: bar.tooltipBold
            tooltipItalic: bar.tooltipItalic
            tooltipFont: bar.tooltipFont
            tooltipColor: bar.tooltipColor
            tooltipShadowColor: bar.tooltipShadowColor
            minimizeAnimation: bar.minimizeAnimation
            windowPeekEnabled: bar.windowPeekEnabled
            windowPeekDelay: bar.windowPeekDelay
            trashIsEmpty: bar.trashIsEmpty
            barFrozen: bar.frozen

            onActivated: {
                if (bar.kooldock && bar.kooldock.model)
                    bar.kooldock.model.activate(model.itemIndex)
            }

            onContextMenuRequested: pt => {
                // pt arrives in scene (root window) coordinates; the menus
                // are children of `bar`, and Menu.popup() positions relative
                // to its parent's coordinate system. Map scene -> bar-local,
                // otherwise the menu lands far off (clamped to a screen
                // corner) now that the surface is full-screen and `bar` sits
                // at the screen edge rather than at the scene origin.
                const p = bar.mapFromItem(null, pt.x, pt.y)
                if (model.isAppMenu) {
                    // The application-launcher icon has no per-item actions;
                    // right-clicking it opens the dock-wide menu (Edit
                    // Preferences / Reload / Quit), same as the background.
                    dockMenu.popup(p)
                    return
                }
                if (model.isTrash) {
                    bar.showTrashMenu(p)
                    return
                }
                if (model.isLauncher && !model.isRunning) {
                    dockMenu.popup(p)
                    return
                }
                bar.showMenu(delegateItem, p)
            }

            onTrashDropped: (urls) => {
                if (bar.kooldock) bar.kooldock.trashFiles(urls)
            }

            // A .desktop dragged over this icon is an app to add, not an
            // open-with target: forward to the bar's insertion-gap preview
            // (the bar's own DropArea sits below the icons and never sees it).
            onExternalDesktopDragMoved: (scenePt) => {
                const p = bar.mapFromItem(null, scenePt.x, scenePt.y)
                bar.externalDragMove(vertical ? p.y : p.x)
            }
            onExternalDesktopDragExited: bar.externalDragClear()
            onExternalDesktopDropped: (localFile) => {
                const target = bar.externalDropTarget
                bar.externalDragReset()
                if (localFile.length > 0 && bar.kooldock && bar.kooldock.model)
                    bar.kooldock.model.addLauncherAt(localFile, target)
            }

            onTooltipExtentChanged: bar.tooltipExtent = delegateItem.tooltipExtent

            // Drag-and-drop: notify the bar when a drag starts, moves, or
            // ends. The bar handles reordering (drag within the dock) and
            // removal (drag outside the dock) with the poof animation.
            onDragStarted: (idx) => {
                removeTimer.stop()
                removeTimer.idx = -1
                bar.dragIndex = idx
                // Reserve the gap at the item's own slot so nothing shifts
                // until the cursor actually moves to another slot.
                bar.dropTarget = idx
                // Expand the window so the DragHandler keeps tracking the
                // cursor as the icon moves outside the dock's visible area.
                if (bar.kooldock) bar.kooldock.setDragExpanded(true)
                bar.layout()
            }
            onDragMoved: (longPos, crossPos) => {
                if (bar.dragIndex < 0) return
                // Live feedback: tell the item whether it's in the
                // removal zone so it can show the semi-transparent state.
                const outside = bar.isOutsideDock(longPos, crossPos)
                delegateItem.willRemove = outside
                if (outside) {
                    // Leaving the dock: drop the reserved gap so the rest
                    // of the row closes ranks behind the departing icon.
                    if (bar.dropTarget !== -1) { bar.dropTarget = -1; bar.layout() }
                    return
                }
                // Inside: find the launcher slot the cursor is over (using
                // the stable rest grid, not the live shifting positions, so
                // the target doesn't chase the gap it just opened) and move
                // the reserved gap there. Clamp to the launcher range —
                // only launchers can be reordered.
                const barPos = bar.mapToItem(null, 0, 0)
                const barScenePos = vertical ? barPos.y : barPos.x
                const relPos = longPos - barScenePos
                const iDist = bar.smallSize + bar.spacing
                let target = Math.round((relPos - bar.spacing - bar.smallSize / 2) / iDist)
                let firstL = -1, lastL = -1
                for (let i = 0; i < listModel.count; i++) {
                    if (listModel.get(i).isLauncher) { if (firstL < 0) firstL = i; lastL = i }
                }
                if (firstL < 0) return
                target = Math.max(firstL, Math.min(lastL, target))
                if (target !== bar.dropTarget) { bar.dropTarget = target; bar.layout() }
            }
            onDragEnded: (idx, longPos, crossPos) => {
                if (bar.dragIndex < 0 || bar.dragIndex != idx) return
                // Restore the window size now that the drag is over.
                if (bar.kooldock) bar.kooldock.setDragExpanded(false)
                const isOutside = bar.isOutsideDock(longPos, crossPos)
                const from = bar.dragIndex
                const target = bar.dropTarget
                // Clear drag state *before* the model move so the signals it
                // emits (onItemMoved -> layout()) rebuild the normal zoomed
                // layout instead of the drag reflow.
                bar.dragIndex = -1
                bar.dropTarget = -1
                if (isOutside) {
                    // Dropped outside the dock: freeze the icon at the release
                    // point by pinning x/y/size (breaks their bindings so the
                    // imminent layout() can't drag it back to its slot), poof
                    // it there, then remove. Without this it springs home and
                    // the burst plays inside the dock instead of where the user
                    // let go. The window is full-screen, so the drop point is
                    // always on-surface.
                    delegateItem.x = delegateItem.x
                    delegateItem.y = delegateItem.y
                    delegateItem.width = delegateItem.width
                    delegateItem.height = delegateItem.height
                    delegateItem.playDestroyAnimation()
                    removeTimer.idx = from
                    removeTimer.start()
                } else {
                    // Dropped inside: zero the offset so the icon settles into
                    // its slot (the item didn't reset it on release).
                    delegateItem.dragOffsetX = 0
                    delegateItem.dragOffsetY = 0
                    delegateItem.willRemove = false
                    if (target >= 0 && target !== from) {
                        // Persist the move. The dragged delegate is already
                        // floating over the gap at `target`, so once the model
                        // reorders, normal layout() assigns it that same slot
                        // and it animates in place with no jump.
                        if (bar.kooldock && bar.kooldock.model)
                            bar.kooldock.model.moveLauncher(from, target)
                    }
                }
                // Recompute the normal layout (restores zoom / settles the
                // dragged icon into its slot when no model move happened).
                bar.layout()
            }
        }
    }

    // Timer to delay the actual model removal until the poof animation
    // has played. The index is stashed in a custom property.
    Timer {
        id: removeTimer
        property int idx: -1
        // Outlast the poof burst (poofHideTimer 500ms / the 400ms anims) so
        // the delegate isn't torn down mid-burst, cutting it off.
        interval: 500
        onTriggered: {
            if (idx >= 0 && bar.kooldock && bar.kooldock.model)
                bar.kooldock.model.removeLauncher(idx)
            idx = -1
        }
    }

    // Context-sensitive menu for dock items (launchers and tasks).
    // Items are shown/hidden based on the context menu item's type
    // (launcher vs task, running vs not, fused vs standalone).
    //
    // popupType: Popup.Window forces this to render as its own top-level
    // window. Without it, Qt defaults to rendering the popup as a plain
    // Item clipped to *this* window's own surface — which for kooldock2
    // is just tall enough for the icon row + zoom overflow (tens of px),
    // nowhere near enough for a menu with this many rows, so it rendered
    // squashed into a tiny scrollable box instead of its natural size.
    Menu {
        id: contextMenu
        popupType: Popup.Window
        onClosed: bar.hideMenu()

        // "New Window" — generic fallback to launch a new instance
        // alongside any running one. Only shown when the app doesn't
        // already define its own Desktop Actions (below): apps that do
        // (Konsole, LibreWolf, ...) include their own "Open a New
        // Window"-equivalent action, and showing both duplicated the
        // same entry twice.
        MenuItem {
            text: i18n("&New Window")
            icon.name: "window-new"
            visible: contextMenuItem && contextMenuItem.isLauncher && bar.contextMenuActions.length === 0
            onTriggered: {
                if (bar.kooldock && bar.kooldock.model && contextMenuIndex >= 0)
                    bar.kooldock.model.newWindow(contextMenuIndex)
            }
        }

        // "Open" — for launchers that aren't running.
        MenuItem {
            text: i18n("&Open")
            icon.name: "document-open"
            visible: contextMenuItem && contextMenuItem.isLauncher && !contextMenuItem.isRunning
            onTriggered: {
                if (bar.kooldock && bar.kooldock.model && contextMenuIndex >= 0)
                    bar.kooldock.model.launch(contextMenuIndex)
            }
        }

        // Per-app Desktop Actions (e.g. LibreWolf's "Open a New Private
        // Window") — dynamic, populated in showMenu() right before the
        // menu opens. Instantiator is the standard way to add a variable
        // number of MenuItems to a Menu in QML.
        Instantiator {
            model: bar.contextMenuActions
            delegate: MenuItem {
                required property var modelData
                text: modelData.name
                icon.name: modelData.iconName.length > 0 ? modelData.iconName : ""
                onTriggered: {
                    if (bar.kooldock && bar.kooldock.model && contextMenuIndex >= 0)
                        bar.kooldock.model.triggerDesktopAction(contextMenuIndex, modelData.id)
                }
            }
            onObjectAdded: (index, object) => contextMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => contextMenu.removeItem(object)
        }

        MenuSeparator {
            visible: contextMenuItem && (contextMenuItem.isLauncher || bar.contextMenuActions.length > 0)
        }

        // Window list — shown when multiple windows are grouped under one
        // icon (KDE taskbar-style grouping). Each item activates that
        // specific window.
        Instantiator {
            model: bar.contextWindowList
            delegate: MenuItem {
                required property var modelData
                text: modelData.title || ""
                onTriggered: {
                    if (bar.kooldock && bar.kooldock.model)
                        bar.kooldock.model.activateSpecificWindow(modelData.windowId)
                }
            }
            onObjectAdded: (index, object) => contextMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => contextMenu.removeItem(object)
        }

        MenuSeparator {
            visible: contextMenuItem && bar.contextWindowList.length > 0
        }

        // Window management — only for running tasks or fused launchers.
        MenuItem {
            text: i18n("Mi&nimize")
            icon.name: "window-minimize"
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId)
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId,
                    bar.kooldock.windowActions.minimize()
            }
        }
        MenuItem {
            text: i18n("Ma&ximize")
            icon.name: "window-maximize"
            checkable: true
            checked: !!bar.contextMenuState.maximized
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId) {
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId
                    bar.kooldock.windowActions.maximize()
                }
            }
        }

        // "More Actions" — window states the Wayland protocol exposes via
        // set_state (plasma-window-management.xml), wired up through
        // WindowActions just like Minimize/Maximize/Close above.
        //
        // Deliberately flat (no nested submenu Menu): binding `visible`
        // on a Menu used as another Menu's child crashes inside
        // QQuickMenu::setVisible() the moment contextMenuItem changes and
        // that binding re-evaluates while the parent menu hasn't been
        // popped yet (reproduced twice — confirmed via coredumpctl, both
        // times segfaulting in QQuickMenu::setVisible()). Plain MenuItems
        // with the same visible binding (proven safe by Minimize/Maximize/
        // Close already using it) don't have this problem.
        MenuSeparator {
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
        }
        MenuItem {
            text: i18n("Keep &Above Others")
            checkable: true
            checked: !!bar.contextMenuState.keepAbove
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId) {
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId
                    bar.kooldock.windowActions.toggleKeepAbove()
                }
            }
        }
        MenuItem {
            text: i18n("Keep &Below Others")
            checkable: true
            checked: !!bar.contextMenuState.keepBelow
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId) {
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId
                    bar.kooldock.windowActions.toggleKeepBelow()
                }
            }
        }
        MenuItem {
            text: i18n("&Fullscreen")
            checkable: true
            checked: !!bar.contextMenuState.fullscreen
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId) {
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId
                    bar.kooldock.windowActions.toggleFullscreen()
                }
            }
        }
        MenuItem {
            text: i18n("Sh&ade")
            checkable: true
            checked: !!bar.contextMenuState.shaded
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId) {
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId
                    bar.kooldock.windowActions.shade()
                }
            }
        }
        MenuItem {
            text: i18n("&On All Desktops")
            checkable: true
            checked: !!bar.contextMenuState.onAllDesktops
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId) {
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId
                    bar.kooldock.windowActions.toggleOnAllDesktops()
                }
            }
        }

        MenuItem {
            text: i18n("&Close")
            icon.name: "window-close"
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
            onTriggered: {
                if (bar.kooldock && contextMenuItem && contextMenuItem.windowId) {
                    bar.kooldock.windowActions.currentWindow = contextMenuItem.windowId
                    bar.kooldock.windowActions.close()
                }
            }
        }

        MenuSeparator {
            visible: contextMenuItem && (contextMenuItem.isTask || (contextMenuItem.isLauncher && contextMenuItem.isRunning))
        }

        // "Keep in Dock" — for standalone tasks (not fused with a launcher).
        // Pins the running app as a permanent launcher.
        MenuItem {
            text: i18n("&Keep in Dock")
            icon.name: "pin"
            visible: contextMenuItem && contextMenuItem.isTask && !contextMenuItem.isLauncher
            onTriggered: {
                if (bar.kooldock && bar.kooldock.model && contextMenuItem && contextMenuItem.windowId)
                    bar.kooldock.model.pinTask(contextMenuItem.windowId)
            }
        }

        // "Remove from Dock" — for launchers (fused or not).
        MenuItem {
            text: i18n("&Remove from Dock")
            icon.name: "edit-delete"
            visible: contextMenuItem && contextMenuItem.isLauncher
            onTriggered: {
                if (bar.kooldock && bar.kooldock.model && contextMenuIndex >= 0)
                    bar.kooldock.model.removeLauncher(contextMenuIndex)
            }
        }
    }

    // Trash right-click menu: open folder + empty trash.
    Menu {
        id: trashMenu
        popupType: Popup.Window
        MenuItem {
            text: i18n("&Open Trash")
            icon.name: "user-trash"
            onTriggered: {
                if (bar.kooldock) bar.kooldock.openTrash()
            }
        }
        MenuItem {
            text: i18n("Emp&ty Trash")
            icon.name: "trash-empty"
            enabled: !bar.trashIsEmpty
            onTriggered: {
                bar.emptyTrash()
            }
        }
    }

    MouseArea {
        anchors.fill: parent; acceptedButtons: Qt.RightButton
        onClicked: {
            dockMenu.popup()
        }
    }

    Menu {
        id: dockMenu
        popupType: Popup.Window
        MenuItem { text: i18n("Edit &Preferences"); icon.name: "configure"
            onTriggered: { if (bar.kooldock) bar.kooldock.showPreferences() } }
        MenuItem { text: i18n("&Reload Configuration"); icon.name: "view-refresh"
            onTriggered: { if (bar.kooldock) bar.kooldock.reload() } }
        MenuSeparator {}
        MenuItem { text: i18n("&Quit"); icon.name: "application-exit"
            onTriggered: { if (bar.kooldock) bar.kooldock.quit() } }
    }
}
