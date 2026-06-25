import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root

    // `settings` and `kooldock` are injected as context properties by
    // KoolDock::showPreferences() (kooldock.cpp).  Do NOT redeclare them
    // here — a local property of the same name would shadow the context
    // property, leaving it null.
    implicitWidth: 560
    implicitHeight: 500

    SystemPalette { id: sysPalette }

    Rectangle {
        anchors.fill: parent
        color: sysPalette.window
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 0

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton { text: i18n("Behavior") }
            TabButton { text: i18n("Appearance") }
            TabButton { text: i18n("Icons") }
            TabButton { text: i18n("Tasks") }
            TabButton { text: i18n("Tooltips") }
            TabButton { text: i18n("About") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // =====================================================
            //  BEHAVIOR
            // =====================================================
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width

                    Label { text: i18n("Position"); font.bold: true; Layout.topMargin: 8; Layout.bottomMargin: 4 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Monitor:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        ComboBox {
                            model: kooldock ? kooldock.screenNames : []
                            currentIndex: {
                                if (!kooldock) return 0
                                const idx = kooldock.screenNames.indexOf(kooldock.screenName)
                                return idx >= 0 ? idx : 0
                            }
                            onActivated: {
                                if (kooldock && currentIndex < kooldock.screenNames.length)
                                    kooldock.screenName = kooldock.screenNames[currentIndex]
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Screen edge:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        ComboBox {
                            model: [i18n("Bottom"), i18n("Left"), i18n("Top"), i18n("Right")]
                            currentIndex: settings ? settings.orientation : 0
                            onActivated: { if (settings) settings.orientation = currentIndex }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Edge margin:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: -300; to: 300; stepSize: 1
                            value: settings ? settings.edgeMargin : 0
                            onValueModified: { if (settings) settings.edgeMargin = value }
                        }
                        Label { text: i18n("px — negative = below other panels"); opacity: 0.45; Layout.fillWidth: true }
                    }

                    Label { text: i18n("Visibility"); font.bold: true; Layout.topMargin: 14; Layout.bottomMargin: 4 }
                    CheckBox {
                        text: i18n("Auto-hide — show only when pointer touches screen edge")
                        Layout.leftMargin: 10
                        checked: settings ? settings.autoHide : false
                        onToggled: { if (settings) settings.autoHide = checked }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Show delay:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 0; to: 2000; stepSize: 50
                            value: settings ? settings.hideTimer : 125
                            onValueModified: { if (settings) settings.hideTimer = value }
                            enabled: settings ? settings.autoHide : false
                        }
                        Label { text: i18n("ms"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Show/hide speed:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Slider {
                            id: showHideSpeedSlider; from: 50; to: 1000; stepSize: 50
                            value: settings ? settings.showHideSpeed : 200
                            onMoved: { if (settings) settings.showHideSpeed = value }
                            enabled: settings ? settings.autoHide : false
                            Layout.preferredWidth: 150
                        }
                        SpinBox {
                            from: 50; to: 1000; stepSize: 50
                            value: showHideSpeedSlider.value
                            onValueModified: { showHideSpeedSlider.value = value; if (settings) settings.showHideSpeed = value }
                            enabled: settings ? settings.autoHide : false
                        }
                        Label { text: i18n("ms"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    CheckBox {
                        text: i18n("Hide after clicking an item")
                        Layout.leftMargin: 10
                        checked: settings ? settings.hideOnClick : false
                        onToggled: { if (settings) settings.hideOnClick = checked }
                    }
                    CheckBox {
                        text: i18n("Reserve screen space — maximized windows won't cover the dock")
                        Layout.leftMargin: 10
                        // Reserving space is meaningless while auto-hiding.
                        enabled: settings ? !settings.autoHide : true
                        checked: settings ? settings.reserveSpace : false
                        onToggled: { if (settings) settings.reserveSpace = checked }
                    }

                    Label { text: i18n("Extras"); font.bold: true; Layout.topMargin: 14; Layout.bottomMargin: 4 }
                    CheckBox {
                        text: i18n("Show application launcher as first dock item")
                        Layout.leftMargin: 10
                        checked: settings ? settings.showKMenu : false
                        onToggled: { if (settings) settings.showKMenu = checked }
                    }
                    CheckBox {
                        text: i18n("Start automatically on login")
                        Layout.leftMargin: 10
                        checked: kooldock ? kooldock.autostart : false
                        onToggled: { if (kooldock) kooldock.autostart = checked }
                    }
                }
            }

            // =====================================================
            //  APPEARANCE
            // =====================================================
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width

                    Label { text: i18n("Background"); font.bold: true; Layout.topMargin: 8; Layout.bottomMargin: 4 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Color:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Rectangle {
                            id: bgSwatch
                            implicitWidth: 22; implicitHeight: 22; radius: 4
                            color: bgColorField.text
                            border.color: Qt.rgba(0, 0, 0, 0.15)
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: bgColorDialog.open()
                            }
                            ColorDialog {
                                id: bgColorDialog
                                selectedColor: bgColorField.text
                                title: i18n("Select Background Color")
                                onAccepted: {
                                    bgColorField.text = selectedColor
                                    if (settings) settings.backgroundColor = selectedColor
                                }
                            }
                        }
                        TextField {
                            id: bgColorField
                            Layout.preferredWidth: 110
                            text: settings ? settings.backgroundColor : "#1e1e2e"
                            onEditingFinished: { if (settings) settings.backgroundColor = text }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Opacity:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Slider {
                            id: bgOpacSlider; from: 0; to: 100; stepSize: 1
                            value: settings ? settings.backgroundOpacity : 70
                            onMoved: { if (settings) settings.backgroundOpacity = value }
                            Layout.preferredWidth: 150
                        }
                        SpinBox {
                            from: 0; to: 100
                            value: bgOpacSlider.value
                            onValueModified: { bgOpacSlider.value = value; if (settings) settings.backgroundOpacity = value }
                        }
                        Label { text: "%"; opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Corner radius:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Slider {
                            id: cornerSlider; from: 0; to: 60; stepSize: 1
                            value: settings ? settings.cornerRadius : 18
                            onMoved: { if (settings) settings.cornerRadius = value }
                            Layout.preferredWidth: 150
                        }
                        SpinBox {
                            from: 0; to: 60
                            value: cornerSlider.value
                            onValueModified: { cornerSlider.value = value; if (settings) settings.cornerRadius = value }
                        }
                        Label { text: "px"; opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Pill height:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 32; to: 200; stepSize: 2
                            value: settings ? settings.dockHeight : 60
                            onValueModified: { if (settings) settings.dockHeight = value }
                        }
                        Label { text: "px"; opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    CheckBox {
                        text: i18n("Blur behind the dock (requires compositor)")
                        Layout.leftMargin: 10; Layout.topMargin: 8
                        checked: settings ? settings.blurBackground : true
                        onToggled: { if (settings) settings.blurBackground = checked }
                    }

                    Label { text: i18n("Border"); font.bold: true; Layout.topMargin: 14; Layout.bottomMargin: 4 }
                    CheckBox {
                        text: i18n("Show border")
                        Layout.leftMargin: 10
                        checked: settings ? settings.showBorders : false
                        onToggled: { if (settings) settings.showBorders = checked }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        enabled: settings ? settings.showBorders : false
                        Label { text: i18n("Border color:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Rectangle {
                            implicitWidth: 22; implicitHeight: 22; radius: 4
                            color: borderColorField.text
                            border.color: Qt.rgba(0, 0, 0, 0.15)
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: borderColorDialog.open()
                            }
                            ColorDialog {
                                id: borderColorDialog
                                selectedColor: borderColorField.text
                                title: i18n("Select Border Color")
                                onAccepted: {
                                    borderColorField.text = selectedColor
                                    if (settings) settings.borderColor = selectedColor
                                }
                            }
                        }
                        TextField {
                            id: borderColorField
                            Layout.preferredWidth: 110
                            text: settings ? settings.borderColor : "#b1c4de"
                            onEditingFinished: { if (settings) settings.borderColor = text }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        enabled: settings ? settings.showBorders : false
                        Label { text: i18n("Border width:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 0; to: 20; stepSize: 1
                            value: settings ? Math.round(settings.borderWidth * 2) : 1
                            onValueModified: { if (settings) settings.borderWidth = value / 2 }
                        }
                        Label { text: i18n("× 0.5 px"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // =====================================================
            //  ICONS
            // =====================================================
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width

                    Label { text: i18n("Sizes"); font.bold: true; Layout.topMargin: 8; Layout.bottomMargin: 4 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Resting size:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 16; to: 128; stepSize: 4
                            value: settings ? settings.smallIconSize : 48
                            onValueModified: { if (settings) settings.smallIconSize = value }
                        }
                        Label { text: "px"; opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Zoomed size:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 32; to: 256; stepSize: 4
                            value: settings ? settings.bigIconSize : 96
                            onValueModified: { if (settings) settings.bigIconSize = value }
                        }
                        Label { text: "px"; opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Icon padding:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 0; to: 32; stepSize: 1
                            value: settings ? settings.iconPadding : 4
                            onValueModified: { if (settings) settings.iconPadding = value }
                        }
                        Label { text: i18n("px — inside margin"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Spacing:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 0; to: 64; stepSize: 2
                            value: settings ? settings.iconSpacing : 10
                            onValueModified: { if (settings) settings.iconSpacing = value }
                        }
                        Label { text: i18n("px — gap between icons"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }

                    Label { text: i18n("Parabolic Zoom"); font.bold: true; Layout.topMargin: 14; Layout.bottomMargin: 4 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Zoom range:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Slider {
                            id: zoomRangeSlider; from: 4; to: 10; stepSize: 1
                            value: settings ? settings.bigIconAmount : 5
                            onMoved: { if (settings) settings.bigIconAmount = value }
                            Layout.preferredWidth: 150
                        }
                        SpinBox {
                            from: 4; to: 10
                            value: zoomRangeSlider.value
                            onValueModified: { zoomRangeSlider.value = value; if (settings) settings.bigIconAmount = value }
                        }
                        Label { text: i18n("neighbours"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Animation speed:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Slider {
                            id: zoomSpeedSlider; from: 50; to: 1000; stepSize: 50
                            value: settings ? settings.zoomSpeed : 200
                            onMoved: { if (settings) settings.zoomSpeed = value }
                            Layout.preferredWidth: 150
                        }
                        SpinBox {
                            from: 50; to: 1000; stepSize: 50
                            value: zoomSpeedSlider.value
                            onValueModified: { zoomSpeedSlider.value = value; if (settings) settings.zoomSpeed = value }
                        }
                        Label { text: "ms"; opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // =====================================================
            //  TASKS
            // =====================================================
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width

                    Label { text: i18n("Window List"); font.bold: true; Layout.topMargin: 8; Layout.bottomMargin: 4 }
                    CheckBox {
                        text: i18n("Show running windows in the dock")
                        Layout.leftMargin: 10
                        checked: settings ? settings.showTaskbar : true
                        onToggled: { if (settings) settings.showTaskbar = checked }
                    }
                    CheckBox {
                        text: i18n("Only show minimized windows")
                        Layout.leftMargin: 10
                        checked: settings ? settings.minimizedOnly : false
                        onToggled: { if (settings) settings.minimizedOnly = checked }
                    }
                    CheckBox {
                        text: i18n("Only show windows from current virtual desktop")
                        Layout.leftMargin: 10
                        checked: settings ? settings.currentDesktopOnly : false
                        onToggled: { if (settings) settings.currentDesktopOnly = checked }
                    }
                    CheckBox {
                        text: i18n("Highlight task when window notifies")
                        Layout.leftMargin: 10
                        checked: settings ? settings.showNotifications : true
                        onToggled: { if (settings) settings.showNotifications = checked }
                    }
                    CheckBox {
                        text: i18n("Animate windows toward the dock icon when minimizing")
                        Layout.leftMargin: 10
                        checked: settings ? settings.minimizeAnimation : true
                        onToggled: { if (settings) settings.minimizeAnimation = checked }
                    }

                    Label { text: i18n("Running Indicator"); font.bold: true; Layout.topMargin: 14; Layout.bottomMargin: 4 }
                    CheckBox {
                        text: i18n("Show window count badge on grouped tasks")
                        Layout.leftMargin: 10
                        checked: settings ? settings.showWindowCountBadge : true
                        onToggled: { if (settings) settings.showWindowCountBadge = checked }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Dot size:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 0; to: 16; stepSize: 1
                            value: settings ? settings.taskIndicatorSize : 4
                            onValueModified: { if (settings) settings.taskIndicatorSize = value }
                        }
                        Label { text: i18n("px — 0 = hidden"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Dot color:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Rectangle {
                            implicitWidth: 22; implicitHeight: 22; radius: 4
                            color: dotColorField.text
                            border.color: Qt.rgba(0, 0, 0, 0.15)
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dotColorDialog.open()
                            }
                            ColorDialog {
                                id: dotColorDialog
                                selectedColor: dotColorField.text
                                title: i18n("Select Indicator Color")
                                onAccepted: {
                                    dotColorField.text = selectedColor
                                    if (settings) settings.taskIndicatorColor = selectedColor
                                }
                            }
                        }
                        TextField {
                            id: dotColorField
                            Layout.preferredWidth: 110
                            text: settings ? settings.taskIndicatorColor : "#aaffffff"
                            onEditingFinished: { if (settings) settings.taskIndicatorColor = text }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Dot opacity:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Slider {
                            id: dotOpacSlider; from: 0; to: 100; stepSize: 5
                            value: settings ? Math.round(settings.taskIndicatorOpacity * 100) : 60
                            onMoved: { if (settings) settings.taskIndicatorOpacity = value / 100 }
                            Layout.preferredWidth: 150
                        }
                        SpinBox {
                            from: 0; to: 100; stepSize: 5
                            value: dotOpacSlider.value
                            onValueModified: { dotOpacSlider.value = value; if (settings) settings.taskIndicatorOpacity = value / 100 }
                        }
                        Label { text: "%"; opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // =====================================================
            //  TOOLTIPS
            // =====================================================
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width

                    CheckBox {
                        text: i18n("Show icon name on hover")
                        Layout.topMargin: 8; Layout.leftMargin: 10
                        checked: settings ? settings.showNames : true
                        onToggled: { if (settings) settings.showNames = checked }
                    }

                    Label { text: i18n("Text"); font.bold: true; Layout.topMargin: 10; Layout.bottomMargin: 4 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Font:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Button {
                            id: fontButton
                            // FontDialog works in points; show the point
                            // size here so the displayed number matches
                            // what the user sees in the picker.  The
                            // setting is stored in logical pixels.
                            text: (settings ? settings.tooltipFont : "Sans Serif") +
                                  " " + (settings ? Math.round(settings.tooltipSize * 3 / 4) : 9) + " pt"
                            onClicked: fontDialog.open()
                        }
                        FontDialog {
                            id: fontDialog
                            title: i18n("Select Tooltip Font")
                            currentFont.family: settings ? settings.tooltipFont : "Sans Serif"
                            // QFontDialog works in point sizes internally;
                            // selectedFont.pixelSize is always -1.
                            currentFont.pointSize: settings ? settings.tooltipSize * 3 / 4 : 9
                            onAccepted: {
                                if (settings) {
                                    settings.tooltipFont = selectedFont.family
                                    // Convert points back to logical pixels (96 dpi).
                                    settings.tooltipSize = Math.round(selectedFont.pointSize * 4 / 3)
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Text color:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Rectangle {
                            implicitWidth: 22; implicitHeight: 22; radius: 4
                            color: ttColorField.text
                            border.color: Qt.rgba(0, 0, 0, 0.15)
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ttColorDialog.open()
                            }
                            ColorDialog {
                                id: ttColorDialog
                                selectedColor: ttColorField.text
                                title: i18n("Select Tooltip Text Color")
                                onAccepted: {
                                    ttColorField.text = selectedColor
                                    if (settings) settings.tooltipColor = selectedColor
                                }
                            }
                        }
                        TextField {
                            id: ttColorField
                            Layout.preferredWidth: 110
                            text: settings ? settings.tooltipColor : "#f1f1f1"
                            onEditingFinished: { if (settings) settings.tooltipColor = text }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Shadow color:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        Rectangle {
                            implicitWidth: 22; implicitHeight: 22; radius: 4
                            color: ttShadowField.text
                            border.color: Qt.rgba(0, 0, 0, 0.15)
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ttShadowDialog.open()
                            }
                            ColorDialog {
                                id: ttShadowDialog
                                selectedColor: ttShadowField.text
                                title: i18n("Select Tooltip Shadow Color")
                                onAccepted: {
                                    ttShadowField.text = selectedColor
                                    if (settings) settings.tooltipShadowColor = selectedColor
                                }
                            }
                        }
                        TextField {
                            id: ttShadowField
                            Layout.preferredWidth: 110
                            text: settings ? settings.tooltipShadowColor : "#000000"
                            onEditingFinished: { if (settings) settings.tooltipShadowColor = text }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.leftMargin: 10; spacing: 20; Layout.topMargin: 2
                        CheckBox {
                            text: i18n("Bold")
                            checked: settings ? settings.tooltipBold : false
                            onToggled: { if (settings) settings.tooltipBold = checked }
                        }
                        CheckBox {
                            text: i18n("Italic")
                            checked: settings ? settings.tooltipItalic : false
                            onToggled: { if (settings) settings.tooltipItalic = checked }
                        }
                    }

                    Label { text: i18n("Timing"); font.bold: true; Layout.topMargin: 14; Layout.bottomMargin: 4 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Show after:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 0; to: 5000; stepSize: 50
                            value: settings ? settings.tooltipDelay : 500
                            onValueModified: { if (settings) settings.tooltipDelay = value }
                        }
                        Label { text: i18n("ms"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Label { text: i18n("Hide after:"); Layout.preferredWidth: 140; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.75 }
                        SpinBox {
                            from: 0; to: 10000; stepSize: 100
                            value: settings ? settings.tooltipTimeout : 2000
                            onValueModified: { if (settings) settings.tooltipTimeout = value }
                        }
                        Label { text: i18n("ms — 0 = never"); opacity: 0.45 }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // =====================================================
            //  ABOUT
            // =====================================================
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    Item { Layout.preferredHeight: 20 }
                    Image {
                        source: "image://kicon/kooldock2"
                        sourceSize.width: 96; sourceSize.height: 96
                        Layout.preferredWidth: 96; Layout.preferredHeight: 96
                        Layout.alignment: Qt.AlignHCenter
                        fillMode: Image.PreserveAspectFit
                    }
                    Label {
                        text: i18n("KoolDock2")
                        font.pixelSize: 24; font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: i18n("Version") + " " + (kooldock ? kooldock.version : "0.5.0")
                        Layout.alignment: Qt.AlignHCenter
                        opacity: 0.7
                    }
                    Label {
                        text: i18n("A macOS-style dock for KDE Plasma 6")
                        Layout.alignment: Qt.AlignHCenter
                        opacity: 0.7
                    }

                    Item { Layout.preferredHeight: 20 }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: sysPalette.mid; Layout.leftMargin: 40; Layout.rightMargin: 40 }

                    Item { Layout.preferredHeight: 12 }
                    Label {
                        text: i18n("© 2026 Matias Fernandez")
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "matias.fernandez@gmail.com"
                        Layout.alignment: Qt.AlignHCenter
                        opacity: 0.7
                    }

                    Item { Layout.preferredHeight: 16 }
                    Label {
                        text: i18n("Originally created by the KoolDock team for KDE 3")
                        Layout.alignment: Qt.AlignHCenter
                        opacity: 0.6
                    }
                    Label {
                        text: i18n("Ported to Qt 6 / KDE Frameworks 6 / Wayland")
                        Layout.alignment: Qt.AlignHCenter
                        opacity: 0.6
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // ── Separator ───────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 8
            implicitHeight: 1
            color: sysPalette.mid
        }

        // ── Footer ──────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Item { Layout.fillWidth: true }
            Button {
                text: i18n("Reset")
                onClicked: {
                    if (settings) settings.load()
                    if (kooldock) kooldock.reload()
                }
            }
            Button {
                text: i18n("Apply")
                onClicked: {
                    if (settings) settings.save()
                    if (kooldock) kooldock.reload()
                }
            }
            Button {
                text: i18n("OK")
                onClicked: {
                    if (settings) settings.save()
                    if (kooldock) kooldock.reload()
                    root.Window.window.close()
                }
            }
        }
    }
}
