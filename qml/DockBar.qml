import QtQuick
import QtQuick.Controls

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
    property real globalMousePos: 0

    property bool containsMouse: false
    property real contentLength: 0

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
                             windowId: d.windowId, itemIndex: d.itemIndex, sz: smallSize, ipos: 0})
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
        const margin = spacing * 2
        bar.containsMouse = localMousePos > -margin && localMousePos < bar.contentLength + margin

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

        for (let i = 0; i < N; i++) {
            const pos = centers[i] - sizes[i] / 2
            listModel.setProperty(i, "sz", sizes[i])
            listModel.setProperty(i, "ipos", pos)
        }
    }

    Connections {
        target: kooldock && kooldock.model ? kooldock.model : null
        function onItemsChanged() { bar.refreshItems() }
        function onCountChanged() { bar.refreshItems() }
    }

    Component.onCompleted: refreshItems()
    onKooldockChanged: refreshItems()
    onGlobalMousePosChanged: layout()
    onWindowExtentChanged: layout()
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
            itemPos: model.ipos
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
