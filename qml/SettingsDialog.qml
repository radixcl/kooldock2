import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root

    // `settings` and `kooldock` are injected as context properties by
    // KoolDock::showPreferences() (kooldock.cpp). Do NOT redeclare them
    // here: a local `property var` of the same name would shadow the
    // context property in QML's scope chain, leaving it null and silently
    // breaking every read/write (the bug that made Apply/OK do nothing).
    implicitWidth: 520
    implicitHeight: 440

    SystemPalette { id: sysPalette }

    Rectangle {
        anchors.fill: parent
        color: sysPalette.window
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton { text: i18n("General") }
            TabButton { text: i18n("Appearance") }
            TabButton { text: i18n("Sizes") }
            TabButton { text: i18n("Tasks") }
            TabButton { text: i18n("Tooltips") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // == General ==
            Flickable {
                contentHeight: genCol.implicitHeight
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: genCol
                    width: parent.width
                    CheckBox {
                        text: i18n("Auto-hide")
                        checked: settings ? settings.autoHide : false
                        onToggled: { if (settings) settings.autoHide = checked }
                    }
                    RowLayout {
                        Label { text: i18n("Show delay (ms):") }
                        SpinBox {
                            from: 0; to: 2000; stepSize: 50
                            value: settings ? settings.hideTimer : 125
                            onValueModified: { if (settings) settings.hideTimer = value }
                        }
                    }
                    CheckBox {
                        text: i18n("Hide on click")
                        checked: settings ? settings.hideOnClick : false
                        onToggled: { if (settings) settings.hideOnClick = checked }
                    }
                    RowLayout {
                        Label { text: i18n("Edge:") }
                        ComboBox {
                            model: [i18n("Bottom"), i18n("Left"), i18n("Top"), i18n("Right")]
                            currentIndex: settings ? settings.orientation : 0
                            onActivated: { if (settings) settings.orientation = currentIndex }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Edge margin (px):") }
                        SpinBox {
                            from: -300; to: 300; stepSize: 1
                            value: settings ? settings.edgeMargin : 0
                            onValueModified: { if (settings) settings.edgeMargin = value }
                        }
                        Label { text: i18n("(negative = below other panels)") ; opacity: 0.6 }
                    }
                    CheckBox {
                        text: i18n("Show app menu as first item")
                        checked: settings ? settings.showKMenu : false
                        onToggled: { if (settings) settings.showKMenu = checked }
                    }
                }
            }

            // == Appearance ==
            Flickable {
                contentHeight: appCol.implicitHeight
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: appCol
                    width: parent.width
                    RowLayout {
                        Label { text: i18n("Background color:") }
                        TextField {
                            text: settings ? settings.backgroundColor : "#1e1e2e"
                            onEditingFinished: { if (settings) settings.backgroundColor = text }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Opacity (%):") }
                        SpinBox {
                            from: 0; to: 100; stepSize: 5
                            value: settings ? settings.backgroundOpacity : 70
                            onValueModified: { if (settings) settings.backgroundOpacity = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Corner radius:") }
                        SpinBox {
                            from: 0; to: 60; stepSize: 1
                            value: settings ? settings.cornerRadius : 18
                            onValueModified: { if (settings) settings.cornerRadius = value }
                        }
                    }
                    CheckBox {
                        text: i18n("Blur behind dock")
                        checked: settings ? settings.blurBackground : true
                        onToggled: { if (settings) settings.blurBackground = checked }
                    }
                    CheckBox {
                        text: i18n("Show borders")
                        checked: settings ? settings.showBorders : false
                        onToggled: { if (settings) settings.showBorders = checked }
                    }
                    RowLayout {
                        Label { text: i18n("Border color:") }
                        TextField {
                            text: settings ? settings.borderColor : "#b1c4de"
                            onEditingFinished: { if (settings) settings.borderColor = text }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Border width:") }
                        SpinBox {
                            from: 0; to: 10; stepSize: 1
                            value: settings ? settings.borderWidth * 2 : 1
                            onValueModified: { if (settings) settings.borderWidth = value / 2 }
                        }
                        Label { text: i18n("(0.5 px steps)") ; opacity: 0.6 }
                    }
                }
            }

            // == Sizes ==
            Flickable {
                contentHeight: szCol.implicitHeight
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: szCol
                    width: parent.width
                    RowLayout {
                        Label { text: i18n("Small icon:") }
                        SpinBox {
                            from: 16; to: 128; stepSize: 4
                            value: settings ? settings.smallIconSize : 48
                            onValueModified: { if (settings) settings.smallIconSize = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Big icon:") }
                        SpinBox {
                            from: 32; to: 256; stepSize: 4
                            value: settings ? settings.bigIconSize : 96
                            onValueModified: { if (settings) settings.bigIconSize = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Zoomed icons:") }
                        SpinBox {
                            from: 4; to: 10; stepSize: 1
                            value: settings ? settings.bigIconAmount : 5
                            onValueModified: { if (settings) settings.bigIconAmount = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Spacing:") }
                        SpinBox {
                            from: 0; to: 64; stepSize: 2
                            value: settings ? settings.iconSpacing : 10
                            onValueModified: { if (settings) settings.iconSpacing = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Icon padding:") }
                        SpinBox {
                            from: 0; to: 32; stepSize: 1
                            value: settings ? settings.iconPadding : 4
                            onValueModified: { if (settings) settings.iconPadding = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Dock height:") }
                        SpinBox {
                            from: 32; to: 200; stepSize: 2
                            value: settings ? settings.dockHeight : 60
                            onValueModified: { if (settings) settings.dockHeight = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Zoom speed (ms):") }
                        SpinBox {
                            from: 50; to: 1000; stepSize: 50
                            value: settings ? settings.zoomSpeed : 200
                            onValueModified: { if (settings) settings.zoomSpeed = value }
                        }
                    }
                }
            }

            // == Tasks ==
            Flickable {
                contentHeight: tskCol.implicitHeight
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: tskCol
                    width: parent.width
                    CheckBox {
                        text: i18n("Show running windows")
                        checked: settings ? settings.showTaskbar : true
                        onToggled: { if (settings) settings.showTaskbar = checked }
                    }
                    CheckBox {
                        text: i18n("Only minimized")
                        checked: settings ? settings.minimizedOnly : false
                        onToggled: { if (settings) settings.minimizedOnly = checked }
                    }
                    CheckBox {
                        text: i18n("Current desktop only")
                        checked: settings ? settings.currentDesktopOnly : false
                        onToggled: { if (settings) settings.currentDesktopOnly = checked }
                    }
                    RowLayout {
                        Label { text: i18n("Indicator size:") }
                        SpinBox {
                            from: 0; to: 16; stepSize: 1
                            value: settings ? settings.taskIndicatorSize : 4
                            onValueModified: { if (settings) settings.taskIndicatorSize = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Indicator color:") }
                        TextField {
                            text: settings ? settings.taskIndicatorColor : "#aaffffff"
                            onEditingFinished: { if (settings) settings.taskIndicatorColor = text }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Indicator opacity:") }
                        SpinBox {
                            from: 0; to: 100; stepSize: 5
                            value: settings ? settings.taskIndicatorOpacity * 100 : 60
                            onValueModified: { if (settings) settings.taskIndicatorOpacity = value / 100 }
                        }
                    }
                }
            }

            // == Tooltips ==
            Flickable {
                contentHeight: ttCol.implicitHeight
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: ttCol
                    width: parent.width
                    CheckBox {
                        text: i18n("Show names on hover")
                        checked: settings ? settings.showNames : true
                        onToggled: { if (settings) settings.showNames = checked }
                    }
                    RowLayout {
                        Label { text: i18n("Font family:") }
                        TextField {
                            text: settings ? settings.tooltipFont : "Sans Serif"
                            onEditingFinished: { if (settings) settings.tooltipFont = text }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Font size:") }
                        SpinBox {
                            from: 6; to: 72; stepSize: 1
                            value: settings ? settings.tooltipSize : 12
                            onValueModified: { if (settings) settings.tooltipSize = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Text color:") }
                        TextField {
                            text: settings ? settings.tooltipColor : "#f1f1f1"
                            onEditingFinished: { if (settings) settings.tooltipColor = text }
                        }
                    }
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
                    RowLayout {
                        Label { text: i18n("Delay (ms):") }
                        SpinBox {
                            from: 0; to: 5000; stepSize: 50
                            value: settings ? settings.tooltipDelay : 500
                            onValueModified: { if (settings) settings.tooltipDelay = value }
                        }
                    }
                    RowLayout {
                        Label { text: i18n("Timeout (ms):") }
                        SpinBox {
                            from: 0; to: 10000; stepSize: 100
                            value: settings ? settings.tooltipTimeout : 2000
                            onValueModified: { if (settings) settings.tooltipTimeout = value }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
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
