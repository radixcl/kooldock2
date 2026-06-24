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

    // Drag-and-drop state. When dragging, the item is lifted (scale +
    // shadow), follows the cursor, and the bar handles reordering or
    // removal on release.
    property bool dragging: false
    property bool dragActive: false
    property real dragOffsetX: 0
    property real dragOffsetY: 0
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

    Behavior on width  { enabled: !barFrozen; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on height { enabled: !barFrozen; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on x      { enabled: !dragActive && !barFrozen; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on y      { enabled: !dragActive && !barFrozen; NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }

    // Lift effect while dragging: scale up slightly + drop shadow.
    // beingDestroyed drives the poof scale-down via destroyScale.
    QtObject {
        id: destroyScale
        property real scale: 1.0
    }
    scale: dragActive ? 1.3 : (beingDestroyed ? destroyScale.scale : 1.0)
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
        enabled: !beingDestroyed
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
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            if (dragActive) return
            if (mouse.button === Qt.RightButton)
                item.contextMenuRequested(mapToItem(null, mouse.x, mouse.y))
            else {
                item.playClickBounce()
                item.activated()
            }
        }
    }

    // Trash drop area: accept file drops to move files to trash.
    // Only active for trash items. Accepts any URI (files, folders).
    DropArea {
        anchors.fill: parent
        enabled: isTrash
        keys: ["text/uri-list"]
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
                item.dragActive = true
                item.dragStarted(modelIndex)
            } else if (item.dragActive) {
                const scenePos = dragHandler.centroid.scenePosition
                const longPos = vertical ? scenePos.y : scenePos.x
                const crossPos = vertical ? scenePos.x : scenePos.y
                item.dragActive = false
                item.dragOffsetX = 0
                item.dragOffsetY = 0
                item.willRemove = false
                item.dragEnded(modelIndex, longPos, crossPos)
            }
        }

        onCentroidChanged: {
            if (dragHandler.active && item.dragActive) {
                // Follow the cursor: offset from the item's rest position
                // to where the drag centroid is, in parent coordinates.
                const scenePos = dragHandler.centroid.scenePosition
                const parentPos = item.mapToItem(parent, 0, 0)
                const targetX = scenePos.x - parentPos.x - itemSize / 2
                const targetY = scenePos.y - parentPos.y - itemSize / 2
                item.dragOffsetX = targetX - (vertical ? (edge === Qt.LeftEdge ? 0 : (parent.width - itemSize)) : itemPos)
                item.dragOffsetY = targetY - (vertical ? itemPos : (edge === Qt.TopEdge ? 0 : (parent.height - itemSize)))
                const longPos = vertical ? scenePos.y : scenePos.x
                const crossPos = vertical ? scenePos.x : scenePos.y
                item.dragMoved(longPos, crossPos)
            }
        }
    }

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
        color: taskDotColor; visible: (isTask || isRunning) && !isAppMenu && !isTrash && windowCount <= 1; opacity: taskDotOpacity
    }

    // Window count badge for grouped windows (>1)
    Text {
        visible: windowCount > 1 && !isAppMenu && !isTrash
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
    // The geometry is in root-window coordinates, which matches the
    // panel surface since the window is full-screen and screen-anchored.
    Timer {
        id: geomTimer
        interval: 150
        repeat: true
        running: minimizeAnimation && (isTask || isRunning) && windowId
        onTriggered: {
            if (!windowId || !kooldock) return
            const pt = mapToItem(null, 0, 0)
            kooldock.setMinimizedGeometry(windowId, pt.x, pt.y, width, height)
        }
    }

    SequentialAnimation {
        id: destroyAnim
        ParallelAnimation {
            NumberAnimation { target: destroyScale; property: "scale"; from: 1.3; to: 0.0; duration: 400; easing.type: Easing.InQuad }
            NumberAnimation { target: item; property: "opacity"; from: 1.0; to: 0.0; duration: 400; easing.type: Easing.InQuad }
            NumberAnimation { target: item; property: "rotation"; from: 0; to: vertical ? 180 : -180; duration: 400; easing.type: Easing.OutQuad }
        }
    }
}
