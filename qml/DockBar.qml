import QtQuick
import QtQuick.Controls

Item {
    id: bar

    property var kooldock: null
    property int edge: Qt.BottomEdge
    property bool autoHide: false
    // Stable inputs from Main.qml: the window's actual width (set by the
    // compositor/layer-shell, unrelated to anything we compute here) and the
    // cursor's position in that same, never-moving frame. We deliberately do
    // NOT use the bar's own live on-screen position to find the mouse: the
    // bar stays centered at width/2 - contentWidth/2, i.e. its position is
    // itself an output of this function, so using it as an input here would
    // make the two chase each other.
    property real windowWidth: 0
    property real globalMouseX: 0

    property bool containsMouse: false
    property real contentWidth: 0

    readonly property bool vertical: edge === Qt.LeftEdge || edge === Qt.RightEdge
    // Geometry, fed by Main.qml from settings — defaults here only matter
    // before that binding resolves.
    property int smallSize: 48
    property int bigSize: 96
    property int zoomRange: 5
    property int spacing: 10
    property int zoomDuration: 200

    clip: false

    ListModel { id: listModel }

    function refreshItems() {
        if (!kooldock || !kooldock.model) return
        const m = kooldock.model
        listModel.clear()
        for (let i = 0; i < m.count; i++) {
            const d = m.itemData(i)
            listModel.append({name: d.name, iconName: d.iconName, isTask: d.isTask,
                             windowId: d.windowId, itemIndex: d.itemIndex, sz: smallSize, ix: 0})
        }
        layout()
    }

    function layout() {
        const N = listModel.count
        if (N === 0) return
        const W = (smallSize + spacing) * zoomRange / 2
        const H = bigSize - smallSize
        const iDist = smallSize + spacing

        // Project the cursor onto the bar's [0, contentWidth] frame using
        // *last* layout's contentWidth (read here, before it's overwritten
        // below) — see the comment on windowWidth/globalMouseX above for why.
        const barLeftEdge = windowWidth / 2 - bar.contentWidth / 2
        const localMouseX = globalMouseX - barLeftEdge
        const margin = spacing * 2
        bar.containsMouse = localMouseX > -margin && localMouseX < bar.contentWidth + margin

        // Step 1: sizes based on parabola centered at mouse. Gated by both
        // per-icon distance (dx < W) and containsMouse: icons within W of
        // the mouse jump straight to their zoomed size the instant the
        // cursor enters the dock's hover margin, rather than growing
        // gradually as it approaches — intentional, not a smooth fade-in.
        const sizes = []
        for (let i = 0; i < N; i++) {
            const restCenter = spacing + iDist * i + smallSize / 2
            const dx = localMouseX - restCenter
            let sz = smallSize
            if (containsMouse && Math.abs(dx) < W)
                sz = Math.max(smallSize, bigSize - (dx * dx * H) / (W * W))
            sizes.push(sz)
        }

        // Step 2: layout positions using original formula. The row is
        // always centered within [0, contentWidth] by construction (icon 0
        // starts at `spacing`, the last icon ends at `contentWidth - spacing`),
        // so it stays centered relative to the background, which Main.qml
        // sizes to match contentWidth, with no extra recentring needed here:
        // cur_cx[0] = spacing + size[0]/2
        // cur_cx[i] = cur_cx[i-1] + (size[i] + size[i-1])/2 + spacing
        const centers = []
        centers.push(spacing + sizes[0] / 2)
        for (let i = 1; i < N; i++) {
            centers.push(centers[i-1] + (sizes[i] + sizes[i-1]) / 2 + spacing)
        }

        // Total span of the row at its current (possibly zoomed) sizes,
        // including the leading/trailing spacing — used by Main.qml to grow
        // the background pill to hug the icons, macOS-style.
        bar.contentWidth = centers[N - 1] + sizes[N - 1] / 2 + spacing

        for (let i = 0; i < N; i++) {
            const x = centers[i] - sizes[i] / 2
            listModel.setProperty(i, "sz", sizes[i])
            listModel.setProperty(i, "ix", x)
        }
    }

    Connections {
        target: kooldock && kooldock.model ? kooldock.model : null
        function onItemsChanged() { bar.refreshItems() }
        function onCountChanged() { bar.refreshItems() }
    }

    Component.onCompleted: refreshItems()
    onKooldockChanged: refreshItems()
    onGlobalMouseXChanged: layout()
    onWindowWidthChanged: layout()
    // Geometry settings changed (Apply/OK in the preferences dialog) — sizes
    // feed into every icon's rest size in the model, so go through
    // refreshItems() rather than just layout().
    onSmallSizeChanged: refreshItems()
    onBigSizeChanged: refreshItems()
    onZoomRangeChanged: layout()
    onSpacingChanged: refreshItems()

    Repeater {
        model: listModel
        delegate: DockItem {
            name: model.name
            iconName: model.iconName
            isTask: model.isTask
            windowId: model.windowId
            modelIndex: model.itemIndex
            itemSize: model.sz
            itemX: model.ix
            edge: bar.edge
            containsMouse: bar.containsMouse
            maxIconSize: bar.bigSize
            zoomDuration: bar.zoomDuration

            onActivated: {
                if (bar.kooldock && bar.kooldock.model)
                    bar.kooldock.model.activate(model.itemIndex)
            }

            onContextMenuRequested: pt => {
                if (!bar.kooldock || !model.isTask) return
                contextMenuLoader.active = true
                const menu = contextMenuLoader.item
                if (menu) {
                    bar.kooldock.windowActions.currentWindow = model.windowId
                    menu.popup(pt)
                }
            }
        }
    }

    Loader {
        id: contextMenuLoader
        active: false
        sourceComponent: Menu {
            MenuItem { text: i18n("Mi&nimize"); icon.name: "window-minimize"
                onTriggered: { if (bar.kooldock) bar.kooldock.windowActions.minimize() } }
            MenuItem { text: i18n("Ma&ximize"); icon.name: "window-maximize"
                onTriggered: { if (bar.kooldock) bar.kooldock.windowActions.maximize() } }
            MenuItem { text: i18n("&Close"); icon.name: "window-close"
                onTriggered: { if (bar.kooldock) bar.kooldock.windowActions.close() } }
        }
    }

    MouseArea {
        anchors.fill: parent; acceptedButtons: Qt.RightButton
        onClicked: { if (bar.kooldock) dockMenu.popup() }
    }

    Menu {
        id: dockMenu
        MenuItem { text: i18n("Edit &Preferences"); icon.name: "configure"
            onTriggered: { if (bar.kooldock) bar.kooldock.showPreferences() } }
        MenuItem { text: i18n("&Reload Configuration"); icon.name: "view-refresh"
            onTriggered: { if (bar.kooldock) bar.kooldock.reload() } }
        MenuSeparator {}
        MenuItem { text: i18n("&Quit"); icon.name: "application-exit"
            onTriggered: { if (bar.kooldock) bar.kooldock.quit() } }
    }
}
