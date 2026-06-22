import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root

    property var settings: null
    property var kooldock: null

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
                        Label { text: i18n("Opacity (%):") }
                        SpinBox {
                            from: 0; to: 100; stepSize: 5
                            value: settings ? settings.backgroundOpacity : 70
                            onValueModified: { if (settings) settings.backgroundOpacity = value }
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
                        Label { text: i18n("Font size:") }
                        SpinBox {
                            from: 6; to: 72; stepSize: 1
                            value: settings ? settings.tooltipSize : 12
                            onValueModified: { if (settings) settings.tooltipSize = value }
                        }
                    }
                    CheckBox {
                        text: i18n("Bold")
                        checked: settings ? settings.tooltipBold : false
                        onToggled: { if (settings) settings.tooltipBold = checked }
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
