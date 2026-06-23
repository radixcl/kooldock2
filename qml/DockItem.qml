import QtQuick
import QtQuick.Controls

Item {
    id: item

    property string name: ""
    property string iconName: ""
    property bool isTask: false
    property var windowId: 0
    property int modelIndex: -1
    property real itemSize: 48
    property real itemX: 0
    property int edge: Qt.BottomEdge
    property bool containsMouse: false
    // The biggest itemSize this icon will ever be asked to render at
    // (DockBar's bigSize). Used to request the icon pixmap once, instead of
    // re-requesting it from the icon theme on every animation frame.
    property int maxIconSize: 80
    property int zoomDuration: 200

    signal activated()
    signal contextMenuRequested(var pt)

    readonly property bool vertical: edge === Qt.LeftEdge || edge === Qt.RightEdge
    readonly property real iconPad: 4

    width:  itemSize
    height: itemSize

    x: vertical ? (parent.width - itemSize) : itemX
    // On BottomEdge the icon sits at the bottom of the bar and grows
    // upward; on TopEdge it sits at the top and grows downward — so the
    // zoom overflow extends away from the screen edge in both cases.
    y: vertical ? itemX : (edge === Qt.TopEdge ? 0 : (parent.height - itemSize))

    Behavior on width  { NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on height { NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on x      { NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }
    Behavior on y      { NumberAnimation { duration: zoomDuration; easing.type: Easing.OutQuad } }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                item.contextMenuRequested(mapToItem(null, mouse.x, mouse.y))
            else
                item.activated()
        }
    }

    Image {
        anchors.centerIn: parent
        readonly property real maxDim: Math.min(parent.width, parent.height) - iconPad * 2
        // Display size tracks the zoom continuously, but sourceSize is
        // fixed: the image provider re-renders the icon from the theme on
        // every sourceSize change, so binding it to maxDim (which changes
        // every animation frame) was firing a flood of async re-renders
        // mid-zoom — visible as a flicker while loads raced/landed out of
        // order. Requesting the largest size once and letting Image scale
        // it down smoothly avoids re-fetching at all.
        width: maxDim; height: maxDim
        source: item.iconName.length > 0 ? ("image://kicon/" + item.iconName) : "image://kicon/application-x-executable"
        sourceSize.width: maxIconSize; sourceSize.height: maxIconSize
        fillMode: Image.PreserveAspectFit
        smooth: true; mipmap: true; asynchronous: true
    }

    Rectangle {
        // Position with y instead of dynamic anchors (see Main.qml's bg
        // for why): TopEdge puts the dot at the top of the icon, BottomEdge
        // at the bottom.
        y: edge === Qt.TopEdge ? 2 : (parent.height - height - 2)
        anchors.horizontalCenter: parent.horizontalCenter
        width: 4; height: 4; radius: 2
        color: "#aaffffff"; visible: isTask; opacity: 0.6
    }

    ToolTip {
        visible: ma.containsMouse && item.name.length > 0
        text: item.name; delay: 500; timeout: 2000
    }
}
