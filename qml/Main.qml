import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var kooldock: null
    clip: false

    readonly property int edge: kooldock ? kooldock.screenEdge : Qt.BottomEdge
    readonly property bool vertical: edge === Qt.LeftEdge || edge === Qt.RightEdge
    readonly property bool autoHide: kooldock ? kooldock.autoHide : false

    readonly property int smallSize: settings ? settings.smallIconSize : 48
    readonly property int bigSize: settings ? settings.bigIconSize : 96
    readonly property int zoomRange: settings ? settings.bigIconAmount : 5
    readonly property int spacing: settings ? settings.iconSpacing : 10
    readonly property int zoomDuration: settings ? settings.zoomSpeed : 200
    readonly property int count: kooldock && kooldock.model ? kooldock.model.count : 0

    // Total width at rest + extra for zoom expansion
    readonly property int contentWidth: count > 0 ? (spacing + count * (smallSize + spacing)) : 500
    readonly property int dockWidth: Math.max(contentWidth + 32, 64)
    readonly property int bgHeight: 60
    // Matches KoolDock::maxDockHeight() in kooldock.cpp, which actually
    // drives the window size (this view uses SizeRootObjectToView, so
    // implicitHeight itself has no effect) — kept in sync just so this
    // doesn't silently lie if anyone reads it.
    readonly property int dockHeight: Math.max(bgHeight, spacing + bigSize + 4)

    implicitWidth:  dockWidth
    implicitHeight: dockHeight

    // Track hover in window coordinates and hand the bar only stable inputs
    // (this width, this position) — DockBar projects them onto its own
    // frame internally. See the comment in DockBar.qml for why that has to
    // happen there rather than here: the bar's on-screen position is itself
    // derived from its content width, so computing "mouse relative to the
    // bar's position" up here would make the two chase each other.
    HoverHandler { id: hoverHandler }
    readonly property bool containsMouse: dockBar.containsMouse
    onContainsMouseChanged: { if (kooldock) kooldock.setContainsMouse(containsMouse) }

    // macOS glass background — transparent, blur via KWindowEffects.
    // Width hugs the icons' current (possibly zoomed) total span and stays
    // horizontally centered, so the pill grows/shrinks with the zoom while
    // the icons (always centered within it, see DockBar.qml) never drift.
    Rectangle {
        id: bg
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        width: Math.max(dockBar.contentWidth + root.spacing * 2, 64)
        height: bgHeight
        radius: 18
        color: "#1affffff"
        border.color: "#33ffffff"
        border.width: 0.5
        clip: false

        opacity: autoHide ? (containsMouse ? 1.0 : 0.0) : 1.0
        scale: autoHide ? (containsMouse ? 1.0 : 0.7) : 1.0
        transformOrigin: Item.Bottom

        Behavior on width   { NumberAnimation { duration: root.zoomDuration; easing.type: Easing.OutQuad } }
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on scale   { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        // Keep the desktop blur region matched to the pill's actual current
        // bounds — including its rounded corners — so neither the side dead
        // space (reserved for zoom growth, like the space above the bar) nor
        // the area just outside the rounded corners is ever blurred.
        onXChanged: if (kooldock) kooldock.updateBlurRegion(x, width, radius)
        onWidthChanged: if (kooldock) kooldock.updateBlurRegion(x, width, radius)

        DockBar {
            id: dockBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: root.spacing
            height: bgHeight - root.spacing
            clip: false

            kooldock: root.kooldock
            edge: root.edge
            autoHide: root.autoHide
            smallSize: root.smallSize
            bigSize: root.bigSize
            zoomRange: root.zoomRange
            spacing: root.spacing
            zoomDuration: root.zoomDuration
            windowWidth: root.width
            // Once the pointer leaves the window, point.position freezes at
            // its last value and never changes again, so nothing would
            // re-trigger layout() to notice the mouse is gone. Force a
            // clearly out-of-range value instead, so containsMouse properly
            // drops to false and the dock shrinks back.
            globalMouseX: hoverHandler.hovered ? hoverHandler.point.position.x : -100000
        }
    }
}
