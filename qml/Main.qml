import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var kooldock: null
    clip: false

    readonly property int edge: kooldock ? kooldock.screenEdge : Qt.BottomEdge
    readonly property bool vertical: edge === Qt.LeftEdge || edge === Qt.RightEdge
    readonly property bool autoHide: kooldock ? kooldock.autoHide : false
    readonly property bool debugBounds: kooldock ? kooldock.debugBounds : false

    readonly property int smallSize: settings ? settings.smallIconSize : 48
    readonly property int bigSize: settings ? settings.bigIconSize : 96
    readonly property int zoomRange: settings ? settings.bigIconAmount : 5
    readonly property int spacing: settings ? settings.iconSpacing : 10
    readonly property int zoomDuration: settings ? settings.zoomSpeed : 200
    readonly property int count: kooldock && kooldock.model ? kooldock.model.count : 0
    readonly property real bgOpacity: settings ? settings.backgroundOpacity / 100 : 0.7
    readonly property color bgColor: settings ? settings.backgroundColor : "#1e1e2e"
    readonly property int cornerRadius: settings ? settings.cornerRadius : 18
    readonly property bool showBorders: settings ? settings.showBorders : false
    readonly property color borderColor: settings ? settings.borderColor : "#b1c4de"
    readonly property real borderWidth: settings ? settings.borderWidth : 0.5
    readonly property bool showNames: settings ? settings.showNames : true
    readonly property int iconPadding: settings ? settings.iconPadding : 4
    readonly property int taskDotSize: settings ? settings.taskIndicatorSize : 4
    readonly property color taskDotColor: settings ? settings.taskIndicatorColor : "#aaffffff"
    readonly property real taskDotOpacity: settings ? settings.taskIndicatorOpacity : 0.6
    readonly property bool showWindowCountBadge: settings ? settings.showWindowCountBadge : true
    readonly property int tooltipDelay: settings ? settings.tooltipDelay : 500
    readonly property int tooltipTimeout: settings ? settings.tooltipTimeout : 2000
    readonly property int tooltipSize: settings ? settings.tooltipSize : 12
    readonly property bool tooltipBold: settings ? settings.tooltipBold : false
    readonly property bool tooltipItalic: settings ? settings.tooltipItalic : false
    readonly property string tooltipFont: settings ? settings.tooltipFont : "Sans Serif"
    readonly property color tooltipColor: settings ? settings.tooltipColor : "#f1f1f1"
    readonly property color tooltipShadowColor: settings ? settings.tooltipShadowColor : "#000000"
    readonly property bool minimizeAnimation: settings ? settings.minimizeAnimation : true

    // Dock geometry along its long and short axes. The long axis is the one
    // icons lay out on (horizontal for Top/BottomEdge, vertical for
    // Left/RightEdge); the short axis is the pill's fixed bgHeight band.
    readonly property int longContentLength: count > 0 ? (spacing + count * (smallSize + spacing)) : 500
    readonly property int longSize: Math.max(longContentLength + 32, 64)
    readonly property int bgHeight: settings ? settings.dockHeight : 60
    readonly property int shortSize: Math.max(bgHeight, spacing + bigSize + 4)
    // Window dimensions swap with orientation: horizontal edges get a wide
    // short window; vertical edges get a narrow tall one. Mirrors
    // KoolDock::maxDockWidth()/maxDockHeight() in kooldock.cpp.
    readonly property int dockWidth: vertical ? shortSize : longSize
    readonly property int dockHeight: vertical ? longSize : shortSize

    implicitWidth:  dockWidth
    implicitHeight: dockHeight

    // The HoverHandler tracks the cursor within the window.  In autohide
    // mode the input mask (wl_surface::set_input_region) restricts events
    // to the trigger strip, so the handler only fires at the screen edge.
    // In non-autohide mode the cross-axis check in DockBar.layout() filters
    // positions outside the pill/icon zone.
    HoverHandler { id: hoverHandler }
    readonly property bool containsMouse: dockBar.containsMouse || dockBar.frozen
                                            || (kooldock ? kooldock.dragActive : false)
    onContainsMouseChanged: { if (kooldock) kooldock.setContainsMouse(containsMouse) }
    // Grow the real window to fit whichever tooltip is currently showing
    // (see DockItem.qml's tooltipExtent) instead of clipping long names —
    // KoolDock handles the grow-now/shrink-after-fade-out timing.
    Connections {
        target: dockBar
        function onTooltipExtentChanged() {
            if (kooldock) kooldock.setTooltipExtent(dockBar.tooltipExtent)
        }
    }
    // Pointer's distance from the screen-anchored edge along the dock's
    // short axis — unlike a raw local coordinate, this stays correct
    // regardless of which edge the window's resize-driven repositioning
    // moves (see KoolDock::setPointerDistanceFromEdge()'s use in the
    // tooltip shrink timer, which needs exactly this to avoid shrinking
    // the surface out from under a still-present pointer).
    readonly property bool nearAnchor: edge === Qt.TopEdge || edge === Qt.LeftEdge
    readonly property real pointerDistanceFromEdge: hoverHandler.hovered
        ? (nearAnchor ? (vertical ? hoverHandler.point.position.x : hoverHandler.point.position.y)
                      : (vertical ? root.width - hoverHandler.point.position.x
                                  : root.height - hoverHandler.point.position.y))
        : -1
    onPointerDistanceFromEdgeChanged: { if (kooldock) kooldock.setPointerDistanceFromEdge(pointerDistanceFromEdge) }

    // Suppress the auto-hide animation on the very first binding pass.
    // `kooldock` is set after setSource() (see kooldock.cpp), so on the first
    // evaluation autoHide is false and the pill reads as fully shown
    // (opacity 1, scale 1, slide 0); once kooldock arrives autoHide becomes
    // true and those bindings snap to the hidden state. Without this gate the
    // Behaviors animate that snap and the pill flashes on screen at launch.
    // `ready` flips on after the first event-loop spin, by which point the
    // hidden state has already been applied instantly; subsequent hover
    // show/hide animates normally.
    property bool ready: false
    Component.onCompleted: readyTimer.start()
    Timer { id: readyTimer; interval: 0; onTriggered: root.ready = true }

    // When auto-hide is on and the dock is hidden (trigger strip),
    // a drag-and-drop from another app (e.g. dragging a .desktop file
    // from Dolphin) doesn't generate hover events — Wayland drag
    // operations don't fire enter/leave like a normal cursor. This
    // DropArea covers the full window and expands the dock
    // on drag-enter so the user can drop onto the now-visible dock. The
    // actual file drop is handled by DockBar's own DropArea.
    DropArea {
        anchors.fill: parent
        enabled: autoHide
        keys: ["text/uri-list"]
        onEntered: (drop) => {
            if (kooldock) kooldock.setDragActive(true)
        }
        onPositionChanged: (drop) => {
            // Keep the heartbeat alive while the drag moves.
            if (kooldock) kooldock.setDragActive(true)
        }
        onDropped: (drop) => {
            // Let DockBar's DropArea handle the actual drop — propagate.
            drop.accepted = false
        }
    }

    // macOS glass background — transparent, blur via KWindowEffects.
    // The pill hugs the icons' current (possibly zoomed) total span along
    // the dock's long axis and stays centered along that axis, so it
    // grows/shrinks with the zoom while the icons (always centered within
    // it, see DockBar.qml) never drift. On horizontal edges the pill sits
    // at the window's top or bottom and icons grow away from that edge; on
    // vertical edges the pill sits at the window's left or right and icons
    // grow away similarly. All positioning uses explicit x/y (no dynamic
    // anchors): switching anchors at runtime can leave both set for a
    // frame and stretch the Rectangle over the overflow area.
    Rectangle {
        id: bg
        // Long-axis dimension (tracks the zoomed icon span); short-axis
        // dimension is fixed at bgHeight.
        width:  vertical ? bgHeight : Math.max(dockBar.contentLength + root.spacing * 2, 64)
        height: vertical ? Math.max(dockBar.contentLength + root.spacing * 2, 64) : bgHeight
        // Position: centered along the long axis, flush to the screen edge
        // on the short axis.
        x: vertical ? (edge === Qt.LeftEdge ? 0 : (parent.width - width))
                    : (parent.width - width) / 2
        y: vertical ? (parent.height - height) / 2
                    : (edge === Qt.TopEdge ? 0 : (parent.height - height))
        radius: root.cornerRadius
        color: Qt.rgba(root.bgColor.r, root.bgColor.g, root.bgColor.b, root.bgOpacity)
        border.color: root.borderColor
        border.width: root.showBorders ? root.borderWidth : 0
        clip: false

        opacity: autoHide ? (containsMouse ? 1.0 : 0.0) : 1.0
        // macOS-style slide: the pill translates off the screen edge when
        // hidden and slides back in when shown. Combined with the opacity
        // fade above, this gives the smooth "grow from the edge" feel.
        // transformOrigin stays anchored to the edge for the subtle scale.
        scale: autoHide ? (containsMouse ? 1.0 : 0.85) : 1.0
        transformOrigin: edge === Qt.TopEdge ? Item.Top
                         : edge === Qt.BottomEdge ? Item.Bottom
                         : edge === Qt.LeftEdge ? Item.Left
                         : Item.Right
        transform: Translate {
            id: slideTransform
            x: autoHide && !containsMouse
               ? (edge === Qt.LeftEdge ? -bg.width : edge === Qt.RightEdge ? bg.width : 0)
               : 0
            y: autoHide && !containsMouse
               ? (edge === Qt.TopEdge ? -bg.height : edge === Qt.BottomEdge ? bg.height : 0)
               : 0
            Behavior on x { enabled: root.ready; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            Behavior on y { enabled: root.ready; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            // Update the blur region every frame as the pill slides, so the
            // blur stays aligned with the visible pill and never shows a
            // static blurred rectangle before/after the animation.
            onXChanged: bg.updateBlur()
            onYChanged: bg.updateBlur()
        }

        Behavior on width   { enabled: !dockBar.frozen; NumberAnimation { duration: root.zoomDuration; easing.type: Easing.OutQuad } }
        Behavior on height  { enabled: !dockBar.frozen; NumberAnimation { duration: root.zoomDuration; easing.type: Easing.OutQuad } }
        Behavior on opacity { enabled: root.ready; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on scale   { enabled: root.ready; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        // Keep the desktop blur region matched to the pill's actual current
        // bounds — including its rounded corners — so neither the side dead
        // space (reserved for zoom growth) nor the area just outside the
        // rounded corners is ever blurred. Pass the long-axis position and
        // length, plus the short-axis slide offset, so the blur follows the
        // pill during the auto-hide animation (otherwise a static blurred
        // rectangle is visible before the pill arrives and after it leaves).
        //
        // bg.x / bg.y depend on bg.width / bg.height. When width changes
        // (zoom animation), onWidthChanged fires first, then x re-evaluates
        // and onXChanged fires — all in the same call stack. Calling
        // updateBlur() from onXChanged/onYChanged ensures the blur sees
        // settled values (the composition of width + x is consistent).
        // Calling from onWidthChanged/onHeightChanged would send a
        // momentarily-inconsistent region (new width, stale x), which KWin
        // may treat as empty and drop the blur for a frame.
        function updateBlur() {
            if (!kooldock) return
            const longPos = vertical ? (bg.y + slideTransform.y) : (bg.x + slideTransform.x)
            const length = vertical ? bg.height : bg.width
            // The slide happens along the short axis (perpendicular to the
            // edge), so pass that offset separately — C++ adds it to its
            // computed short-axis base position.
            const shortOffset = vertical ? slideTransform.x : slideTransform.y
            kooldock.updateBlurRegion(longPos, length, shortOffset, bg.radius)
        }
        onXChanged: bg.updateBlur()
        onYChanged: bg.updateBlur()

        DockBar {
            id: dockBar
            // Fill the pill along its long axis (minus margins); sit at the
            // edge side on the short axis. Explicit x/y/width/height (no
            // dynamic anchors — same reasoning as bg above).
            x: vertical ? (edge === Qt.LeftEdge ? root.spacing : (parent.width - width - root.spacing))
                        : root.spacing
            y: vertical ? root.spacing
                        : (edge === Qt.TopEdge ? root.spacing : (parent.height - height - root.spacing))
            width:  vertical ? (bgHeight - root.spacing) : (parent.width - 2 * root.spacing)
            height: vertical ? (parent.height - 2 * root.spacing) : (bgHeight - root.spacing)
            clip: false

            kooldock: root.kooldock
            edge: root.edge
            autoHide: root.autoHide
            smallSize: root.smallSize
            bigSize: root.bigSize
            zoomRange: root.zoomRange
            spacing: root.spacing
            zoomDuration: root.zoomDuration
            showNames: root.showNames
            iconPadding: root.iconPadding
            taskDotSize: root.taskDotSize
            taskDotColor: root.taskDotColor
            taskDotOpacity: root.taskDotOpacity
            showWindowCountBadge: root.showWindowCountBadge
            tooltipDelay: root.tooltipDelay
            tooltipTimeout: root.tooltipTimeout
            tooltipSize: root.tooltipSize
            tooltipBold: root.tooltipBold
            tooltipItalic: root.tooltipItalic
            tooltipFont: root.tooltipFont
            tooltipColor: root.tooltipColor
            tooltipShadowColor: root.tooltipShadowColor
            minimizeAnimation: root.minimizeAnimation
            trashIsEmpty: kooldock ? kooldock.trashIsEmpty : true
            onEmptyTrash: { if (kooldock) kooldock.emptyTrash() }
            // Stable inputs from Main.qml: the window's extent along the dock's
            // long axis (width for Top/BottomEdge, height for Left/RightEdge — set
            // by the compositor/layer-shell, unrelated to anything we compute here)
            // and the cursor's position along that same axis in the window's
            // never-moving frame. We deliberately do NOT use the bar's own live
            // on-screen position to find the mouse: the bar stays centered at
            // extent/2 - contentLength/2, i.e. its position is itself an output of
            // this function, so using it as an input here would make the two chase
            // each other.
            windowExtent: vertical ? root.height : root.width
            // Short-axis extent of the window (height for horizontal edges,
            // width for vertical edges). Used by layout() to check whether the
            // cursor is still within the icon zone vertically.
            windowCrossExtent: vertical ? root.width : root.height
            // Once the pointer leaves the window, point.position freezes at
            // its last value and never changes again, so nothing would
            // re-trigger layout() to notice the mouse is gone. Force a
            // clearly out-of-range value instead, so containsMouse properly
            // drops to false and the dock shrinks back.
            // point.position is relative to root — the HoverHandler sits
            // on the root item directly.
            globalMousePos: hoverHandler.hovered
                            ? (vertical ? hoverHandler.point.position.y : hoverHandler.point.position.x)
                            : -100000
            globalCrossPos: hoverHandler.hovered
                            ? (vertical ? hoverHandler.point.position.x : hoverHandler.point.position.y)
                            : -100000
        }
    }

    // Debug aid (-d/--debug-bounds CLI flag): the actual Wayland surface is
    // normally invisible — only the "glass pill" (bg, above) is painted —
    // which makes it hard to tell whether a grow/shrink is the window
    // itself resizing or just the pill animating inside a window that
    // hasn't changed size yet. Draws the live window edge directly, with
    // no Behavior, so growth/shrink timing is exactly what's on screen.
    Rectangle {
        visible: root.debugBounds
        anchors.fill: parent
        color: "transparent"
        border.color: "#ff00ff"
        border.width: 2
        z: 1000

        Text {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 4
            color: "#ff00ff"
            font.pixelSize: 10
            font.bold: true
            text: parent.width.toFixed(0) + "x" + parent.height.toFixed(0)
        }
    }
}
