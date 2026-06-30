import QtQuick
import QtQuick.Controls

Item {
    id: item

    property string name: ""
    property string iconName: ""
    property bool isTask: false
    property bool isLauncher: false
    property bool isAppMenu: false
    property bool isTrash: false
    property bool isRunning: false
    property var windowId: 0
    property int windowCount: 0
    property int badgeCount: 0
    property int modelIndex: -1
    property real itemSize: 48
    property real itemPos: 0
    property int edge: Qt.BottomEdge
    property bool containsMouse: false
    property int maxIconSize: 80
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
    property bool trashIsEmpty: true
    property bool barFrozen: false
    property var kooldock: null
    property bool minimizeAnimation: true
    property bool windowPeekEnabled: true
    property int windowPeekDelay: 2000
    // Set true when a press-and-hold fired the Window View peek, so the click
    // emitted on release doesn't also activate/cycle the window.
    property bool peekFired: false

    // Drag-and-drop state. When dragging, the item is lifted (scale +
    // shadow), follows the cursor, and the bar handles reordering or
    // removal on release.
    property bool dragging: false
    property bool dragActive: false
    property real dragOffsetX: 0
    property real dragOffsetY: 0
    // Last known cursor position (scene/root coords) during a drag. Kept so
    // the icon can be re-centred under the cursor not only when the pointer
    // moves, but also when the bar itself shifts (its x/y track
    // contentLength, which changes when the drag layout turns magnification
    // off) or when this item's own slot (itemPos) / size (itemSize) change.
    // Without that, the icon lags behind the cursor by however far the bar
    // moved since the last pointer event — a constant offset to the side.
    property real dragSceneX: 0
    property real dragSceneY: 0
    property bool beingDestroyed: false
    // Set by DockBar during dragMoved to indicate the icon is in the
    // removal zone (far enough from the bar on either axis). Drives the
    // semi-transparent visual feedback.
    property bool willRemove: false

    signal activated()
    signal contextMenuRequested(var pt)
    signal dragStarted(int index)
    signal dragMoved(real longPos, real crossPos)
    signal dragEnded(int index, real longPos, real crossPos)
    signal trashDropped(var urls)
    // A .desktop drag (a new app) is over this icon — forward to the bar so it
    // shows the insertion gap / adds it, rather than open-with. scenePt is in
    // scene coords; the bar maps it to its own frame.
    signal externalDesktopDragMoved(var scenePt)
    signal externalDesktopDragExited()
    signal externalDesktopDropped(string localFile)

    readonly property bool vertical: edge === Qt.LeftEdge || edge === Qt.RightEdge
    readonly property real iconPad: iconPadding

    width:  itemSize
    height: itemSize
    // Raise the whole item above siblings while its tooltip is showing so
    // the tooltip (a child, see below) renders on top of neighbouring
    // zoomed icons. Reverts to 0 when hidden — no effect on normal z-order.
    z: tooltipBox.opacity > 0.01 ? 100 : 0

    // Position along the dock's long axis (itemPos) and short axis (edge
    // side). On BottomEdge icons sit at the bottom and grow upward; on
    // TopEdge at the top growing downward; on LeftEdge at the left growing
    // rightward; on RightEdge at the right growing leftward — the zoom
    // overflow always extends away from the screen edge.
    // When dragging, the icon follows the cursor via dragOffset.
    x: (vertical ? (edge === Qt.LeftEdge ? 0 : (parent.width - itemSize)) : itemPos) + dragOffsetX
    y: (vertical ? itemPos : (edge === Qt.TopEdge ? 0 : (parent.height - itemSize))) + dragOffsetY

    Behavior on width  { enabled: !barFrozen && !dragActive; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on height { enabled: !barFrozen && !dragActive; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on x      { enabled: !dragActive && !barFrozen; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on y      { enabled: !dragActive && !barFrozen; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }

    // Lift effect while dragging: scale up slightly + drop shadow.
    // destroyScale drives the icon's scale-down during the poof, but it's
    // applied to the inner `content` wrapper (below), NOT to the root. The
    // root must stay untransformed so poofContainer — a sibling of content
    // — bursts outward at full size/opacity while the icon collapses.
    // Applying the destroy scale/fade/spin to the root collapsed the poof
    // along with the icon, which is why the burst was never visible.
    QtObject {
        id: destroyScale
        property real scale: 1.0
    }
    scale: dragActive ? 1.3 : 1.0
    transformOrigin: Item.Center
    // Visual feedback during drag: when in the removal zone, the icon
    // becomes semi-transparent to indicate it will be removed on release.
    opacity: (dragActive && willRemove) ? 0.4 : 1.0
    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    // Click feedback bounce (macOS-style squash & pop). Implemented as a
    // transform so it composes with the `scale` property above (used for
    // drag/destroy) without touching it, and doesn't perturb the layout
    // inputs (x/y/width/height) that DockBar.layout() relies on — transforms
    // are render-only. Origin is anchored to the screen-edge base of the
    // icon so the squash presses into the dock and the bounce pops away from
    // the edge, matching the zoom's grow-away-from-edge direction.
    transform: Scale {
        id: clickBounce
        origin.x: vertical ? (edge === Qt.LeftEdge ? 0 : item.width) : item.width / 2
        origin.y: vertical ? item.height / 2 : (edge === Qt.TopEdge ? 0 : item.height)
        xScale: 1.0
        yScale: 1.0
    }

    Behavior on scale {
        NumberAnimation { duration: 200; easing.type: Easing.OutBack }
    }

    // Drop shadow that appears on lift.
    Rectangle {
        id: shadow
        anchors.centerIn: parent
        width: parent.width * 0.9; height: parent.height * 0.9
        radius: width / 2
        color: "#66000000"
        opacity: dragActive ? 0.5 : 0.0
        scale: dragActive ? 1.1 : 0.8
        z: -1
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Behavior on scale   { NumberAnimation { duration: 150 } }
        visible: opacity > 0.01
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        // Press-and-hold arms the peek timer for grouped icons.
        // The peek fires on *release* after the hold duration, so the
        // release event is delivered normally — no phantom button state,
        // no stuck MouseArea grab, no input-mask timing issues.
        // Middle-button click fires the peek immediately.
        onPressed: mouse => {
            item.peekFired = false
            peekTimer.peekArmed = false
            if (mouse.button === Qt.LeftButton && windowPeekEnabled
                    && (isTask || isRunning) && windowCount > 1)
                peekTimer.restart()
        }
        onReleased: mouse => {
            if (peekTimer.peekArmed) {
                peekTimer.peekArmed = false
                item.peekFired = true
                if (item.kooldock && item.kooldock.model)
                    item.kooldock.peekWindows(item.kooldock.model.windowUuidsForRow(modelIndex))
            }
            peekTimer.stop()
        }
        onCanceled: { peekTimer.peekArmed = false; peekTimer.stop() }
        onClicked: mouse => {
            if (dragActive || item.peekFired) return
            if (mouse.button === Qt.RightButton)
                item.contextMenuRequested(mapToItem(null, mouse.x, mouse.y))
            else if (mouse.button === Qt.MiddleButton && windowPeekEnabled
                     && (isTask || isRunning) && windowCount > 1) {
                item.peekFired = true
                if (item.kooldock && item.kooldock.model)
                    item.kooldock.peekWindows(item.kooldock.model.windowUuidsForRow(modelIndex))
            } else {
                item.playClickBounce()
                item.activated()
            }
        }
    }

    // Press-and-hold timer for the Window View peek. Arms a flag after
    // windowPeekDelay ms of holding the left button. The actual peek fires
    // on *release* (see onReleased above), so the release event is delivered
    // to Qt normally and no phantom button state results.
    Timer {
        id: peekTimer
        property bool peekArmed: false
        interval: windowPeekDelay
        repeat: false
        onTriggered: { peekArmed = true }
    }

    // Trash drop area: accept file drops to move files to trash.
    // Only active for trash items. Accepts any URI (files, folders).
    DropArea {
        anchors.fill: parent
        enabled: isTrash
        keys: ["text/uri-list"]
        onContainsDragChanged: { if (item.kooldock) item.kooldock.setIconDragOver(containsDrag) }
        onEntered: (drop) => {
            drop.accepted = true
            item.scale = 1.3
        }
        onDropped: (drop) => {
            if (drop.hasUrls) {
                const urls = []
                for (let i = 0; i < drop.urls.length; i++)
                    urls.push(drop.urls[i])
                item.trashDropped(urls)
            }
            drop.accepted = true
            item.scale = 1.0
        }
        onExited: { item.scale = 1.0 }
    }

    // Open-with drop: drag a file onto a launcher icon to open it with that
    // app (e.g. a .txt onto KWrite). Active for launchers (incl. fused/
    // running); trash/appmenu/tasks are excluded. Because this child DropArea
    // accepts the drop, it doesn't fall through to the bar's add-launcher one.
    DropArea {
        id: openWithDrop
        anchors.fill: parent
        enabled: isLauncher && !isTrash && !isAppMenu
        keys: ["text/uri-list"]
        // True when the hovering drag is a .desktop (a new app to add) rather
        // than a data file to open-with. A .desktop is routed up to the bar
        // (insertion gap + add); the open-with ring is suppressed for it.
        property bool desktopDrag: false
        function dragHasDesktop(drop) {
            if (drop.hasUrls)
                for (let i = 0; i < drop.urls.length; i++)
                    if (drop.urls[i].toString().endsWith(".desktop")) return true
            return false
        }
        // Keep the dock from auto-hiding while a file is held over this icon
        // (a nested DropArea steals containsDrag from the surface-level one).
        onContainsDragChanged: { if (item.kooldock) item.kooldock.setIconDragOver(containsDrag) }
        onEntered: (drop) => {
            desktopDrag = dragHasDesktop(drop)
            if (desktopDrag) item.externalDesktopDragMoved(mapToItem(null, drop.x, drop.y))
        }
        onPositionChanged: (drop) => {
            desktopDrag = dragHasDesktop(drop)
            if (desktopDrag) item.externalDesktopDragMoved(mapToItem(null, drop.x, drop.y))
        }
        onExited: { desktopDrag = false; item.externalDesktopDragExited() }
        onDropped: (drop) => {
            if (drop.hasUrls) {
                const dataUrls = []
                for (let i = 0; i < drop.urls.length; i++) {
                    const u = drop.urls[i].toString()
                    if (u.endsWith(".desktop"))
                        item.externalDesktopDropped(u.replace("file://", ""))
                    else
                        dataUrls.push(drop.urls[i])
                }
                if (dataUrls.length > 0 && item.kooldock && item.kooldock.model)
                    item.kooldock.model.openUrlsWith(modelIndex, dataUrls)
            }
            desktopDrag = false
            drop.accepted = true
        }
    }

    // Highlight ring while a data file hovers over an open-with target. Bound
    // to containsDrag (no imperative scale assignment, so it can't strand the
    // zoom's scale binding the way the trash highlight does). Suppressed for a
    // .desktop drag, which previews as the bar's insertion gap instead.
    Rectangle {
        anchors.fill: parent
        radius: width * 0.2
        color: "transparent"
        border.color: "#cc4d94ff"
        border.width: 3
        visible: openWithDrop.containsDrag && !openWithDrop.desktopDrag
        z: 6
    }

    // DragHandler: starts on press+move (after a small threshold so
    // clicks aren't interpreted as drags). Only launchers are draggable
    // — tasks come and go with windows and can't be reordered.
    DragHandler {
        id: dragHandler
        target: null
        acceptedButtons: Qt.LeftButton
        enabled: !isTask && !isAppMenu && !isTrash && !beingDestroyed
        dragThreshold: 8

        onActiveChanged: {
            if (dragHandler.active) {
                peekTimer.stop()  // a real drag cancels the press-and-hold peek
                item.dragActive = true
                item.dragStarted(modelIndex)
            } else if (item.dragActive) {
                const scenePos = dragHandler.centroid.scenePosition
                const longPos = vertical ? scenePos.y : scenePos.x
                const crossPos = vertical ? scenePos.x : scenePos.y
                item.dragActive = false
                // Don't reset the offset/willRemove here — the bar decides on
                // release. If the icon was dropped outside the dock it must
                // stay where it was released and poof there, not spring back
                // to its slot; for an in-dock drop the bar zeroes the offset
                // so it settles into place.
                item.dragEnded(modelIndex, longPos, crossPos)
            }
        }

        onCentroidChanged: {
            if (dragHandler.active && item.dragActive) {
                const scenePos = dragHandler.centroid.scenePosition
                item.dragSceneX = scenePos.x
                item.dragSceneY = scenePos.y
                item.recenterDrag()
                const longPos = vertical ? scenePos.y : scenePos.x
                const crossPos = vertical ? scenePos.x : scenePos.y
                item.dragMoved(longPos, crossPos)
            }
        }
    }

    // Pin the icon's centre to the last-known cursor position. Offset is the
    // delta from the item's rest slot to where the cursor is, in the item's
    // *parent* (the bar) coordinate system. scenePosition is in scene (root
    // window) coords, so map it into parent coords first — mixing the two
    // breaks once the surface is full-screen and the bar sits far from the
    // scene origin. Recomputed whenever any input that affects the mapping
    // changes (cursor, bar position, this item's slot or size), so the icon
    // stays glued to the cursor even when the bar re-centres mid-drag.
    function recenterDrag() {
        if (!dragActive) return
        const localPos = parent.mapFromItem(null, dragSceneX, dragSceneY)
        const targetX = localPos.x - itemSize / 2
        const targetY = localPos.y - itemSize / 2
        const restX = vertical ? (edge === Qt.LeftEdge ? 0 : (parent.width - itemSize)) : itemPos
        const restY = vertical ? itemPos : (edge === Qt.TopEdge ? 0 : (parent.height - itemSize))
        item.dragOffsetX = targetX - restX
        item.dragOffsetY = targetY - restY
    }

    onItemPosChanged: if (dragActive) recenterDrag()
    onItemSizeChanged: if (dragActive) recenterDrag()

    // The bar's x/y track contentLength (Main.qml centres it), which changes
    // when the drag layout drops magnification — recentre so the dragged
    // icon doesn't drift with the bar between pointer events.
    Connections {
        target: item.parent
        enabled: item.dragActive
        function onXChanged() { item.recenterDrag() }
        function onYChanged() { item.recenterDrag() }
    }

    // Icon visuals (image + indicators) live in this wrapper so the destroy
    // ("poof") transform — scale-to-zero via destroyScale, fade, and spin —
    // applies to the icon alone. poofContainer is a sibling on the root, so
    // the burst expands at full size/opacity while the icon collapses.
    Item {
        id: content
        anchors.fill: parent
        scale: beingDestroyed ? destroyScale.scale : 1.0
        transformOrigin: Item.Center

        Image {
            anchors.centerIn: parent
            readonly property real maxDim: Math.min(parent.width, parent.height) - iconPad * 2
            width: maxDim; height: maxDim
            readonly property string activeIcon: item.isTrash && !item.trashIsEmpty ? "user-trash-full" : item.iconName
            source: item.iconName.length > 0 ? ("image://kicon/" + activeIcon) : "image://kicon/application-x-executable"
            sourceSize.width: maxIconSize; sourceSize.height: maxIconSize
            fillMode: Image.PreserveAspectFit
            smooth: true; mipmap: true; asynchronous: true
        }

        Rectangle {
            x: vertical ? (edge === Qt.LeftEdge ? 2 : (parent.width - width - 2))
                        : (parent.width - width) / 2
            y: vertical ? (parent.height - height) / 2
                        : (edge === Qt.TopEdge ? 2 : (parent.height - height - 2))
            width: taskDotSize; height: taskDotSize; radius: taskDotSize / 2
            // Shown for single-window tasks, and also for grouped tasks when the
            // numeric count badge is turned off — so a grouped icon never ends up
            // with no running indicator at all.
            color: taskDotColor; visible: (isTask || isRunning) && !isAppMenu && !isTrash && (windowCount <= 1 || !showWindowCountBadge); opacity: taskDotOpacity
        }

        // Window count badge for grouped windows (>1)
        Text {
            visible: showWindowCountBadge && windowCount > 1 && !isAppMenu && !isTrash
            text: windowCount
            font.pixelSize: Math.max(9, item.itemSize * 0.2)
            font.weight: Font.Bold
            color: taskDotColor
            opacity: taskDotOpacity
            x: vertical ? (edge === Qt.LeftEdge ? 2 : (parent.width - width - 2))
                        : (parent.width - width) / 2
            y: vertical ? (parent.height - height) / 2
                        : (edge === Qt.TopEdge ? 2 : (parent.height - height - 2))
        }

        // Notification badge (unread count via the Unity LauncherEntry DBus
        // API — Telegram, Betterbird, etc.). Always top-right of the icon,
        // independent of dock edge: that's the universal badge convention
        // every taskbar/dock uses, unlike the task dot above which hugs
        // whichever side faces away from the screen edge.
        Rectangle {
            id: badge
            readonly property real d: Math.max(14, item.itemSize * 0.32)
            visible: badgeCount > 0
            width: Math.max(d, badgeText.implicitWidth + 6)
            height: d
            radius: height / 2
            color: "#e74c3c"
            border.color: "#33000000"
            border.width: 1
            x: parent.width - width * 0.7
            y: -height * 0.3
            z: 10

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: badgeCount > 99 ? "99+" : String(badgeCount)
                color: "white"
                font.pixelSize: badge.height * 0.62
                font.bold: true
            }
        }
    }

    // True while the cursor is over the icon and a tooltip is eligible to
    // show — shared by tooltipShowTimer and tooltipResizeTimer below.
    readonly property bool tooltipHoverActive: ma.containsMouse && showNames && item.name.length > 0 && !dragActive

    // Flips true tooltipResizeLead ms before the tooltip is actually due to
    // show — see tooltipExtent for why the window resize needs this early
    // warning instead of reacting to tooltipBox.opacity directly. It can't
    // simply start at hover-enter either: that would request a resize on
    // every brief pass over an icon, even ones whose tooltip never ends up
    // showing — exactly the thrashing tooltipShowTimer's own delay exists
    // to avoid.
    readonly property int tooltipResizeLead: 200
    property bool tooltipAboutToShow: false
    Timer {
        id: tooltipResizeTimer
        interval: Math.max(tooltipDelay - tooltipResizeLead, 0)
        repeat: false
        running: tooltipHoverActive
        onTriggered: tooltipAboutToShow = true
    }

    // How far beyond the icon's own footprint the tooltip below currently
    // needs, along the dock's short axis (its width on vertical edges,
    // height on horizontal ones since that's the axis it grows along
    // there) — 0 when not showing. DockBar relays this up to KoolDock so
    // the real window can grow to fit it instead of clipping the text.
    //
    // Gated on tooltipAboutToShow rather than tooltipBox.opacity directly:
    // the window resize this drives is an async Wayland round-trip, but
    // opacity starts animating the instant the show timer fires. Tying the
    // resize to opacity alone raced it — the tooltip's fade-in began in a
    // surface that hadn't grown yet, clipping it mid-fade (visible as a
    // flick into the wrong place before snapping to its real position).
    // tooltipBox.width/height stay valid while invisible (only rendering is
    // gated on opacity), so reading them here ahead of time is safe.
    readonly property real tooltipExtent: (tooltipAboutToShow || tooltipBox.opacity > 0.01)
        ? (vertical ? tooltipBox.width + 8 : tooltipBox.height + 8) : 0

    // Custom in-scene tooltip (macOS-style). Rendered as a child of this
    // item, NOT a QtQuick.Controls ToolTip popup — so it's part of the same
    // Wayland surface and the root HoverHandler never loses hover (the dock
    // doesn't auto-hide when the cursor is over the tooltip). Positioned in
    // the overflow area above the icon, away from the screen edge. Has no
    // MouseArea so it doesn't intercept pointer events or block neighbours.
    Rectangle {
        id: tooltipBox
        visible: opacity > 0.01
        opacity: 0
        radius: 6
        color: Qt.rgba(0.1, 0.1, 0.15, 0.9)
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1

        width: tooltipText.width + 16
        height: tooltipText.height + 8

        // Centered on the icon along the long axis; in the overflow area
        // (away from the screen edge) along the short axis with an 8px gap.
        x: vertical ? (edge === Qt.LeftEdge ? itemSize + 8 : -width - 8)
                    : (itemSize - width) / 2
        y: vertical ? (itemSize - height) / 2
                    : (edge === Qt.TopEdge ? itemSize + 8 : -height - 8)

        Text {
            id: tooltipText
            anchors.centerIn: parent
            text: item.name
            font.family: tooltipFont
            font.pixelSize: tooltipSize
            font.bold: tooltipBold
            font.italic: tooltipItalic
            color: tooltipColor
            style: Text.Raised
            styleColor: tooltipShadowColor
        }

        Behavior on opacity {
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }

        // Show after tooltipDelay; the Timer's `running` binding starts it
        // when the mouse enters and stops/resets it when the mouse leaves
        // (so a quick pass doesn't flash the tooltip).
        Timer {
            id: tooltipShowTimer
            interval: tooltipDelay
            repeat: false
            running: tooltipHoverActive
            onTriggered: {
                tooltipBox.opacity = 1
                if (tooltipTimeout > 0) tooltipHideTimer.restart()
            }
        }

        // Auto-hide after tooltipTimeout ms.
        Timer {
            id: tooltipHideTimer
            interval: tooltipTimeout
            repeat: false
            onTriggered: tooltipBox.opacity = 0
        }

        // Hide immediately when the mouse leaves the item.
        Connections {
            target: ma
            function onContainsMouseChanged() {
                if (!ma.containsMouse) {
                    tooltipShowTimer.stop()
                    tooltipHideTimer.stop()
                    tooltipBox.opacity = 0
                    tooltipAboutToShow = false
                }
            }
        }
    }

    // Poof burst: expanding ring + particles.
    Item {
        id: poofContainer
        anchors.centerIn: parent
        visible: false
        z: 100

        function start() {
            visible = true
            poofRing.opacity = 0.8
            poofRing.scale = 0.3
            poofRingAnim.start()
            for (let i = 0; i < particleRepeater.count; i++) {
                particleRepeater.itemAt(i).start()
            }
            poofHideTimer.start()
        }

        Timer {
            id: poofHideTimer
            interval: 500
            onTriggered: poofContainer.visible = false
        }

        // Expanding ring
        Rectangle {
            id: poofRing
            anchors.centerIn: parent
            width: 48; height: 48
            radius: 24
            color: "transparent"
            border.color: "#ccffffff"
            border.width: 2
            opacity: 0
            scale: 0.3

            ParallelAnimation {
                id: poofRingAnim
                NumberAnimation { target: poofRing; property: "scale"; from: 0.3; to: 2.0; duration: 400; easing.type: Easing.OutQuad }
                NumberAnimation { target: poofRing; property: "opacity"; from: 0.8; to: 0.0; duration: 400; easing.type: Easing.OutQuad }
            }
        }

        // Particle burst — 8 small dots flying outward in all directions.
        Repeater {
            id: particleRepeater
            model: 8
            delegate: Rectangle {
                id: particle
                required property int index
                anchors.centerIn: parent
                width: 4; height: 4
                radius: 2
                color: "#ffffff"
                opacity: 0
                scale: 1

                readonly property real angle: index * 45
                readonly property real distance: 42

                function start() {
                    const rad = angle * Math.PI / 180
                    const tx = Math.cos(rad) * distance
                    const ty = Math.sin(rad) * distance
                    pxAnim.to = tx
                    pyAnim.to = ty
                    particleAnim.start()
                }

                ParallelAnimation {
                    id: particleAnim
                    NumberAnimation { id: pxAnim; target: particle; property: "x"; from: 0; to: 0; duration: 400; easing.type: Easing.OutQuad }
                    NumberAnimation { id: pyAnim; target: particle; property: "y"; from: 0; to: 0; duration: 400; easing.type: Easing.OutQuad }
                    NumberAnimation { target: particle; property: "opacity"; from: 1.0; to: 0.0; duration: 400; easing.type: Easing.OutQuad }
                    NumberAnimation { target: particle; property: "scale"; from: 1.0; to: 0.2; duration: 400; easing.type: Easing.OutQuad }
                }
            }
        }
    }

    function playDestroyAnimation() {
        beingDestroyed = true
        destroyScale.scale = 1.3
        poofContainer.start()
        destroyAnim.start()
    }

    function playClickBounce() {
        clickBounceAnim.stop()
        clickBounce.xScale = 1.0
        clickBounce.yScale = 1.0
        clickBounceAnim.start()
    }

    // macOS-style click feedback: a quick squash toward the dock edge
    // followed by a springy pop back to rest. OutBack overshoots past 1.0
    // from the edge-base origin, so the icon visibly lifts off the dock
    // before settling — the bounce.
    SequentialAnimation {
        id: clickBounceAnim

        ParallelAnimation {
            NumberAnimation { target: clickBounce; property: "xScale"; to: 0.88; duration: 100; easing.type: Easing.InQuad }
            NumberAnimation { target: clickBounce; property: "yScale"; to: 0.88; duration: 100; easing.type: Easing.InQuad }
        }
        ParallelAnimation {
            NumberAnimation { target: clickBounce; property: "xScale"; to: 1.0; duration: 380; easing.type: Easing.OutBack }
            NumberAnimation { target: clickBounce; property: "yScale"; to: 1.0; duration: 380; easing.type: Easing.OutBack }
        }
    }

    // Minimize animation target: tell KWin where this icon is so the
    // window minimize animation can fly toward it. Update periodically
    // while the item has a window (position changes with zoom).
    // The geometry is computed from the base (untransformed) layout
    // chain — bg.x/y + bar.x/y + item.x/y — so it always reflects the
    // icon's shown position on screen, even when the dock is hidden and
    // bg has scale/translate transforms applied (which mapToItem would
    // include, making the minimize target drift off the icon's actual
    // screen footprint).
    //
    // `windowId` only ever names the icon's current *primary* window
    // (the one cycling/activation last selected) — KWin tracks the
    // minimize target per-window, not per-icon, so the other windows
    // grouped under this same icon never get a target unless we set one
    // for each of them too. Without this, only the primary window's
    // minimize animation flies to the dock; the rest fall back to KWin's
    // default (no directed animation).
    Timer {
        id: geomTimer
        interval: 150
        repeat: true
        running: minimizeAnimation && (isTask || isRunning) && windowId
        onTriggered: {
            if (!windowId || !kooldock) return
            // `parent` in a Timer handler resolves to QObject::parent()
            // (the DockItem itself), NOT the visual parent (the bar).
            // Use explicit id chain: item -> bar -> bg -> root.
            const theBar = item.parent
            const theBg = theBar.parent
            const ptX = theBg.x + theBar.x + x
            const ptY = theBg.y + theBar.y + y
            if (windowCount > 1 && kooldock.model) {
                const wins = kooldock.model.windowListForRow(modelIndex)
                for (let i = 0; i < wins.length; i++) {
                    kooldock.setMinimizedGeometry(wins[i].windowId, ptX, ptY, width, height)
                }
            } else {
                kooldock.setMinimizedGeometry(windowId, ptX, ptY, width, height)
            }
        }
    }

    SequentialAnimation {
        id: destroyAnim
        ParallelAnimation {
            NumberAnimation { target: destroyScale; property: "scale"; from: 1.3; to: 0.0; duration: 400; easing.type: Easing.InQuad }
            NumberAnimation { target: content; property: "opacity"; from: 1.0; to: 0.0; duration: 400; easing.type: Easing.InQuad }
            NumberAnimation { target: content; property: "rotation"; from: 0; to: vertical ? 180 : -180; duration: 400; easing.type: Easing.OutQuad }
        }
    }
}
