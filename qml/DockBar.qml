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

    property bool containsMouse: false
    property real contentLength: 0
    // Relayed up from whichever DockItem currently has a tooltip showing
    // (see DockItem.qml's tooltipExtent) — Main.qml forwards this to
    // KoolDock so the real window can grow to fit it. Only one tooltip is
    // ever visible at a time, so "last write wins" is correct here.
    property real tooltipExtent: 0
    // Largest rendered icon size from the *last* layout() pass — used to
    // size the cross-axis "still hovering" margin below. Read before this
    // frame's pass overwrites it, same reasoning as contentLength above:
    // a worst-case (bigSize) margin would stay exactly as wide as the
    // window's reserved overflow even when nothing is actually zoomed
    // that big right now, making blur-out feel like it waits for the
    // cursor to leave the whole window instead of just the current icon.
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
    property int tooltipDelay: 500
    property int tooltipTimeout: 2000
    property int tooltipSize: 12
    property bool tooltipBold: false
    property bool tooltipItalic: false
    property string tooltipFont: "Sans Serif"
    property color tooltipColor: "#f1f1f1"
    property color tooltipShadowColor: "#000000"
    property bool trashIsEmpty: true

    signal emptyTrash()

    clip: false

    // Drag-and-drop state: which item is being dragged.
    property int dragIndex: -1

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

    // True while any menu (context menu or dock-wide right-click menu) is
    // visible.  Main.qml feeds this into containsMouse so the dock stays
    // frozen — no auto-hide — for the entire lifetime of the menu, unlike
    // the dragActive heartbeat which expired after 500ms and broke the
    // freeze while the menu was still open.
    readonly property bool frozen: contextMenu.visible || dockMenu.visible

    function showMenu(item, pt) {
        contextMenuIndex = item.modelIndex
        contextMenuItem = item
        contextMenuState = (item.windowId && bar.kooldock)
            ? bar.kooldock.windowActions.queryState(item.windowId) : ({})
        contextMenuActions = ((item.isLauncher || item.isTask) && bar.kooldock && bar.kooldock.model)
            ? bar.kooldock.model.desktopActions(item.modelIndex) : []
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
                             isRunning: d.isRunning, windowId: d.windowId, itemIndex: d.itemIndex,
                             badgeCount: d.badgeCount, sz: smallSize, ipos: 0})
        }
        layout()
    }

    function layout() {
        const N = listModel.count
        if (N === 0) return
        const W = (smallSize + spacing) * zoomRange / 2
        const H = bigSize - smallSize
        const iDist = smallSize + spacing

        // Project the cursor onto the bar's [0, contentLength] frame using
        // *last* layout's contentLength (read here, before it's overwritten
        // below) — see the comment on windowExtent/globalMousePos above for
        // why. This is axis-neutral: for horizontal edges windowExtent is
        // the window's width and globalMousePos is point.x; for vertical
        // edges they're the window's height and point.y. The 1D math is
        // identical either way (verified by a Node.js geometry sim).
        const barNearEdge = windowExtent / 2 - bar.contentLength / 2
        const localMousePos = globalMousePos - barNearEdge
        // Non-autohide: zoom starts exactly when the cursor touches the pill
        // edge. Autohide: keep a margin so zoom fires promptly when the dock
        // slides in from the edge and the cursor may not be perfectly aligned.
        const margin = autoHide ? (spacing * 2) : 0
        // Long-axis check: cursor must be within the pill's span along the
        // layout direction.
        const inLongAxis = localMousePos > -margin && localMousePos < bar.contentLength + margin
        // Short-axis check: cursor must be within the icon zone (from the
        // screen edge to bigSize + spacing, the tallest zoomed icon). Without
        // this, moving the cursor vertically off the dock while staying within
        // its horizontal span kept containsMouse true and the dock "active".
        const crossMargin = spacing
        const inCrossAxis = vertical
            ? (globalCrossPos > -crossMargin && globalCrossPos < bar.width + crossMargin)
            : (globalCrossPos > -crossMargin && globalCrossPos < bar.height + crossMargin)
        // For the short-axis check, the icon zone extends from the anchored
        // screen edge outward by lastMaxIconSize+spacing — the *currently*
        // tallest rendered icon, not the theoretical bigSize max, so the
        // "still hovering" margin shrinks back along with the icons
        // instead of always reserving room for a full zoom that may not
        // be happening right now. For Top/LeftEdge the edge is at
        // coordinate 0; for Bottom/RightEdge the edge is at
        // windowCrossExtent.
        const atTopOrLeft = edge === Qt.TopEdge || edge === Qt.LeftEdge
        bar.containsMouse = inLongAxis && (
            atTopOrLeft
                ? (globalCrossPos > -crossMargin && globalCrossPos < bar.lastMaxIconSize + spacing + crossMargin)
                : (globalCrossPos > windowCrossExtent - bar.lastMaxIconSize - spacing - crossMargin &&
                   globalCrossPos < windowCrossExtent + crossMargin))

        // Step 1: sizes from a parabola centered at the mouse, iterated a
        // few times against the running center estimate. Gated by both
        // per-icon distance (dx < W) and containsMouse: icons within W of
        // the mouse jump straight to their zoomed size the instant the
        // cursor enters the dock's hover margin, rather than growing
        // gradually as it approaches — intentional, not a smooth fade-in.
        //
        // The iteration is what makes the biggest icon land *under* the
        // cursor instead of one slot to the right. The Step 2 cumulative
        // sum lays icons out left-to-right, so every leftward neighbour
        // that also grew shoves the zoomed icon rightward of its rest slot;
        // sizing off the *rest* slot (a single pass) makes the biggest icon
        // end up just right of the cursor. Evaluating the parabola at each
        // icon's *rendered* center (pass 2+) makes the icon that actually
        // ends up under the cursor be the biggest — without shifting the
        // row, which would un-center it from the background pill and break
        // the edge-overflow guarantee (invariant #5). Converges in 2
        // passes; 3 here for margin. localMousePos is read once above and
        // held fixed across passes, so this stays clear of the bar-position
        // feedback loop of invariants #1–2.
        const sizes = []
        let centersEst = null
        for (let it = 0; it < 3; it++) {
            for (let i = 0; i < N; i++) {
                const ci = centersEst ? centersEst[i]
                                      : (spacing + iDist * i + smallSize / 2)
                const dx = localMousePos - ci
                let sz = smallSize
                if (containsMouse && Math.abs(dx) < W)
                    sz = Math.max(smallSize, bigSize - (dx * dx * H) / (W * W))
                sizes[i] = sz
            }
            centersEst = [spacing + sizes[0] / 2]
            for (let i = 1; i < N; i++)
                centersEst.push(centersEst[i-1] + (sizes[i] + sizes[i-1]) / 2 + spacing)
        }

        // Step 2: final layout positions from the converged sizes. The row
        // is always centered within [0, contentLength] by construction
        // (icon 0 starts at `spacing`, the last icon ends at
        // `contentLength - spacing`), so it stays centered relative to the
        // background, which Main.qml sizes to match contentLength, with no
        // extra recentring needed here:
        // cur_cx[0] = spacing + size[0]/2
        // cur_cx[i] = cur_cx[i-1] + (size[i] + size[i-1])/2 + spacing
        const centers = centersEst

        // Total span of the row at its current (possibly zoomed) sizes,
        // including the leading/trailing spacing — used by Main.qml to grow
        // the background pill to hug the icons, macOS-style.
        bar.contentLength = centers[N - 1] + sizes[N - 1] / 2 + spacing

        // Feeds next frame's cross-axis containsMouse margin above — see
        // the property declaration for why this needs to track the
        // current zoom instead of staying pinned to bigSize.
        bar.lastMaxIconSize = Math.max(smallSize, ...sizes)

        for (let i = 0; i < N; i++) {
            const pos = centers[i] - sizes[i] / 2
            listModel.setProperty(i, "sz", sizes[i])
            listModel.setProperty(i, "ipos", pos)
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
                                  isRunning: d.isRunning, windowId: d.windowId, itemIndex: d.itemIndex,
                                  badgeCount: d.badgeCount, sz: smallSize, ipos: 0})
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

        onEntered: (drop) => {
            if (bar.kooldock) bar.kooldock.setDragActive(true)
        }
        onPositionChanged: (drop) => {
            if (bar.kooldock) bar.kooldock.setDragActive(true)
        }
        onDropped: (drop) => {
            if (drop.hasUrls) {
                const urls = drop.urls
                for (let i = 0; i < urls.length; i++) {
                    const url = urls[i]
                    if (url.toString().endsWith(".desktop")) {
                        const localFile = url.toString().replace("file://", "")
                        if (localFile.length > 0 && bar.kooldock && bar.kooldock.model)
                            bar.kooldock.model.addLauncher(localFile)
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
            badgeCount: model.badgeCount
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
            tooltipDelay: bar.tooltipDelay
            tooltipTimeout: bar.tooltipTimeout
            tooltipSize: bar.tooltipSize
            tooltipBold: bar.tooltipBold
            tooltipItalic: bar.tooltipItalic
            tooltipFont: bar.tooltipFont
            tooltipColor: bar.tooltipColor
            tooltipShadowColor: bar.tooltipShadowColor
            trashIsEmpty: bar.trashIsEmpty
            barFrozen: bar.frozen

            onActivated: {
                if (bar.kooldock && bar.kooldock.model)
                    bar.kooldock.model.activate(model.itemIndex)
            }

            onContextMenuRequested: pt => {
                if (model.isAppMenu) return
                if (model.isTrash) {
                    bar.showTrashMenu(pt)
                    return
                }
                bar.showMenu(delegateItem, pt)
            }

            onTrashDropped: (urls) => {
                if (bar.kooldock) bar.kooldock.trashFiles(urls)
            }

            onTooltipExtentChanged: bar.tooltipExtent = delegateItem.tooltipExtent

            // Drag-and-drop: notify the bar when a drag starts, moves, or
            // ends. The bar handles reordering (drag within the dock) and
            // removal (drag outside the dock) with the poof animation.
            onDragStarted: (idx) => {
                removeTimer.stop()
                removeTimer.idx = -1
                bar.dragIndex = idx
                // Expand the window so the DragHandler keeps tracking the
                // cursor as the icon moves outside the dock's visible area.
                if (bar.kooldock) bar.kooldock.setDragExpanded(true)
            }
            onDragMoved: (longPos, crossPos) => {
                // Live feedback: tell the item whether it's in the
                // removal zone so it can show the semi-transparent state.
                delegateItem.willRemove = bar.isOutsideDock(longPos, crossPos)
            }
            onDragEnded: (idx, longPos, crossPos) => {
                if (bar.dragIndex < 0 || bar.dragIndex != idx) return
                // Restore the window size now that the drag is over.
                if (bar.kooldock) bar.kooldock.setDragExpanded(false)
                const isOutside = bar.isOutsideDock(longPos, crossPos)
                if (isOutside) {
                    // Dragged out of the dock: play poof, then remove.
                    delegateItem.playDestroyAnimation()
                    removeTimer.idx = bar.dragIndex
                    removeTimer.start()
                } else {
                    // Dropped inside: reorder — find the target position
                    // and tell the C++ model to move the launcher.
                    const barPos = bar.mapToItem(null, 0, 0)
                    const barScenePos = vertical ? barPos.y : barPos.x
                    const relPos = longPos - barScenePos
                    // Find which icon slot the drop lands on.
                    let targetIdx = -1
                    for (let i = 0; i < listModel.count; i++) {
                        const ipos = listModel.get(i).ipos
                        const isz = listModel.get(i).sz
                        if (relPos < ipos + isz / 2) { targetIdx = i; break }
                    }
                    if (targetIdx < 0) targetIdx = listModel.count - 1
                    if (targetIdx !== bar.dragIndex && targetIdx >= 0) {
                        if (bar.kooldock && bar.kooldock.model)
                            bar.kooldock.model.moveLauncher(bar.dragIndex, targetIdx)
                    }
                }
                bar.dragIndex = -1
            }
        }
    }

    // Timer to delay the actual model removal until the poof animation
    // has played. The index is stashed in a custom property.
    Timer {
        id: removeTimer
        property int idx: -1
        interval: 450
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
